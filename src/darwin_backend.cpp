#include "gtlibcpp/darwin_backend.hpp"

#if defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <signal.h>
#include <sys/sysctl.h>
#include <unistd.h>

namespace gtlibcpp {

namespace {

class DarwinBackend final : public IMemoryBackend {
public:
    DarwinBackend(std::uint32_t pid, mach_port_t task_port, const std::string& path, Architecture arch, bool read_only)
        : pid_(pid), task_port_(task_port), image_path_(path), arch_(arch), read_only_(read_only) {}

    ~DarwinBackend() override {
        if (task_port_ != MACH_PORT_NULL) {
            mach_port_deallocate(mach_task_self(), task_port_);
            task_port_ = MACH_PORT_NULL;
        }
    }

    [[nodiscard]] Result<std::vector<std::uint8_t>>
    read(Address address, std::size_t size) override {
        const auto valid = validate_read_request(address, size);
        if (!valid) return Result<std::vector<std::uint8_t>>::failure(valid.error());

        std::vector<std::uint8_t> buffer(size, 0);
        mach_vm_size_t read_bytes = 0;
        kern_return_t kr = mach_vm_read_overwrite(
            task_port_,
            static_cast<mach_vm_address_t>(address),
            static_cast<mach_vm_size_t>(size),
            reinterpret_cast<mach_vm_address_t>(buffer.data()),
            &read_bytes);

        if (kr != KERN_SUCCESS || read_bytes != size) {
            return Result<std::vector<std::uint8_t>>::failure(
                make_error(ErrorCode::read_failed, "mach_vm_read_overwrite failed (kr=" + std::to_string(kr) + ")",
                           "DarwinBackend.read", address, size));
        }
        return Result<std::vector<std::uint8_t>>::success(std::move(buffer));
    }

    [[nodiscard]] Result<std::size_t>
    write(Address address, const std::vector<std::uint8_t>& bytes) override {
        const auto valid = validate_write_request(address, bytes.size());
        if (!valid) return Result<std::size_t>::failure(valid.error());

        if (read_only_) {
            return Result<std::size_t>::failure(
                make_error(ErrorCode::policy_denied, "DarwinBackend is opened read-only",
                           "DarwinBackend.write", address, bytes.size()));
        }

        kern_return_t kr = mach_vm_write(
            task_port_,
            static_cast<mach_vm_address_t>(address),
            reinterpret_cast<vm_offset_t>(bytes.data()),
            static_cast<mach_msg_type_number_t>(bytes.size()));

        if (kr != KERN_SUCCESS) {
            return Result<std::size_t>::failure(
                make_error(ErrorCode::write_failed, "mach_vm_write failed (kr=" + std::to_string(kr) + ")",
                           "DarwinBackend.write", address, bytes.size()));
        }
        return Result<std::size_t>::success(bytes.size());
    }

    [[nodiscard]] bool is_alive() noexcept override {
        if (pid_ == 0) return false;
        return kill(static_cast<pid_t>(pid_), 0) == 0;
    }

    [[nodiscard]] std::string target_id() const override {
        return "darwin:" + std::to_string(pid_);
    }

    [[nodiscard]] TargetIdentity identity() const override {
        TargetIdentity id{};
        id.pid = pid_;
        id.image_path = image_path_;
        id.architecture = arch_;
        return id;
    }

private:
    std::uint32_t pid_{0};
    mach_port_t   task_port_{MACH_PORT_NULL};
    std::string   image_path_{};
    Architecture  arch_{Architecture::unknown};
    bool          read_only_{false};
};

} // namespace

MemoryBackendPtr make_darwin_backend(const DarwinBackendOptions& options) {
    if (options.pid == 0) return nullptr;
    mach_port_t task_port = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), static_cast<int>(options.pid), &task_port);
    if (kr != KERN_SUCCESS || task_port == MACH_PORT_NULL) {
        return nullptr;
    }
    return std::make_shared<DarwinBackend>(options.pid, task_port, options.image_path, options.architecture, options.read_only);
}

std::uint32_t find_darwin_process(const std::vector<std::string>& process_names) {
    int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (bytes <= 0) return 0;
    std::vector<pid_t> pids(static_cast<std::size_t>(bytes) / sizeof(pid_t) + 64);
    bytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    if (bytes <= 0) return 0;
    const int count = bytes / static_cast<int>(sizeof(pid_t));

    for (int i = 0; i < count; ++i) {
        if (pids[i] <= 0) continue;
        char path_buffer[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(pids[i], path_buffer, sizeof(path_buffer)) > 0) {
            std::string path(path_buffer);
            for (const auto& name : process_names) {
                if (path.find(name) != std::string::npos) {
                    return static_cast<std::uint32_t>(pids[i]);
                }
            }
        }
    }
    return 0;
}

std::string get_darwin_process_path(std::uint32_t pid) {
    if (pid == 0) return {};
    char path_buffer[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(static_cast<int>(pid), path_buffer, sizeof(path_buffer)) > 0) {
        return std::string(path_buffer);
    }
    return {};
}

Address get_darwin_module_base(std::uint32_t pid, const std::string& module_name) {
    if (pid == 0) return 0;

    mach_port_t task_port = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), static_cast<int>(pid), &task_port);
    if (kr != KERN_SUCCESS || task_port == MACH_PORT_NULL) {
        return 0;
    }

    std::string target_name = module_name;
    if (target_name.empty()) {
        const auto p = get_darwin_process_path(pid);
        const auto slash = p.find_last_of('/');
        target_name = (slash != std::string::npos) ? p.substr(slash + 1) : p;
    }

    mach_vm_address_t addr = 0x1000;
    Address found_base = 0;

    while (addr < 0x7fffffffffffULL) {
        mach_vm_size_t size = 0;
        vm_region_basic_info_data_64_t info{};
        mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object_name = MACH_PORT_NULL;
        kr = mach_vm_region(task_port, &addr, &size, VM_REGION_BASIC_INFO_64,
                            reinterpret_cast<vm_region_info_t>(&info), &info_count, &object_name);
        if (kr != KERN_SUCCESS) break;

        char path_buffer[PROC_PIDPATHINFO_MAXSIZE] = {0};
        int path_len = proc_regionfilename(static_cast<int>(pid), addr, path_buffer, sizeof(path_buffer));
        if (path_len > 0) {
            std::string reg_path(path_buffer);
            if (reg_path.find(target_name) != std::string::npos) {
                found_base = static_cast<Address>(addr);
                break;
            }
        }
        addr += size;
    }

    mach_port_deallocate(mach_task_self(), task_port);
    return found_base;
}

} // namespace gtlibcpp
#else
namespace gtlibcpp {

MemoryBackendPtr make_darwin_backend(const DarwinBackendOptions&) {
    return nullptr;
}

std::uint32_t find_darwin_process(const std::vector<std::string>&) {
    return 0;
}

std::string get_darwin_process_path(std::uint32_t) {
    return {};
}

Address get_darwin_module_base(std::uint32_t, const std::string&) {
    return 0;
}

} // namespace gtlibcpp
#endif
