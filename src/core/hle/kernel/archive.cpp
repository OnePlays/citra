// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "common/common_types.h"
#include "common/file_util.h"
#include "common/math_util.h"

#include "core/file_sys/archive.h"
#include "core/file_sys/archive_sdmc.h"
#include "core/file_sys/directory.h"
#include "core/hle/kernel/archive.h"
#include "core/hle/result.h"
#include "core/hle/service/service.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Kernel namespace

namespace Kernel {

// Command to access archive file
enum class FileCommand : u32 {
    Dummy1          = 0x000100C6,
    Control         = 0x040100C4,
    OpenSubFile     = 0x08010100,
    Read            = 0x080200C2,
    Write           = 0x08030102,
    GetSize         = 0x08040000,
    SetSize         = 0x08050080,
    GetAttributes   = 0x08060000,
    SetAttributes   = 0x08070040,
    Close           = 0x08080000,
    Flush           = 0x08090000,
};

// Command to access directory
enum class DirectoryCommand : u32 {
    Dummy1          = 0x000100C6,
    Control         = 0x040100C4,
    Read            = 0x08010042,
    Close           = 0x08020000,
};

class Archive : public Object {
public:
    std::string GetTypeName() const override { return "Archive"; }
    std::string GetName() const override { return name; }

    static Kernel::HandleType GetStaticHandleType() { return HandleType::Archive; }
    Kernel::HandleType GetHandleType() const override { return HandleType::Archive; }

    std::string name;           ///< Name of archive (optional)
    FileSys::Archive* backend;  ///< Archive backend interface

    ResultVal<bool> SyncRequest() override {
        u32* cmd_buff = Service::GetCommandBuffer();
        FileCommand cmd = static_cast<FileCommand>(cmd_buff[0]);

        switch (cmd) {
        // Read from archive...
        case FileCommand::Read:
        {
            u64 offset  = cmd_buff[1] | ((u64)cmd_buff[2] << 32);
            u32 length  = cmd_buff[3];
            u32 address = cmd_buff[5];

            // Number of bytes read
            cmd_buff[2] = backend->Read(offset, length, Memory::GetPointer(address));
            break;
        }
        // Write to archive...
        case FileCommand::Write:
        {
            u64 offset  = cmd_buff[1] | ((u64)cmd_buff[2] << 32);
            u32 length  = cmd_buff[3];
            u32 flush   = cmd_buff[4];
            u32 address = cmd_buff[6];

            // Number of bytes written
            cmd_buff[2] = backend->Write(offset, length, flush, Memory::GetPointer(address));
            break;
        }
        case FileCommand::GetSize:
        {
            u64 filesize = (u64) backend->GetSize();
            cmd_buff[2]  = (u32) filesize;         // Lower word
            cmd_buff[3]  = (u32) (filesize >> 32); // Upper word
            break;
        }
        case FileCommand::SetSize:
        {
            backend->SetSize(cmd_buff[1] | ((u64)cmd_buff[2] << 32));
            break;
        }
        case FileCommand::Close:
        {
            DEBUG_LOG(KERNEL, "Close %s %s", GetTypeName().c_str(), GetName().c_str());
            CloseArchive(backend->GetIdCode());
            break;
        }
        // Unknown command...
        default:
        {
            ERROR_LOG(KERNEL, "Unknown command=0x%08X!", cmd);
            return UnimplementedFunction(ErrorModule::FS);
        }
        }
        cmd_buff[1] = 0; // No error
        return MakeResult<bool>(false);
    }

    ResultVal<bool> WaitSynchronization() override {
        // TODO(bunnei): ImplementMe
        ERROR_LOG(OSHLE, "(UNIMPLEMENTED)");
        return UnimplementedFunction(ErrorModule::FS);
    }
};

class File : public Object {
public:
    std::string GetTypeName() const override { return "File"; }
    std::string GetName() const override { return path.DebugStr(); }

    static Kernel::HandleType GetStaticHandleType() { return HandleType::File; }
    Kernel::HandleType GetHandleType() const override { return HandleType::File; }

    FileSys::Path path; ///< Path of the file
    std::unique_ptr<FileSys::File> backend; ///< File backend interface

    ResultVal<bool> SyncRequest() override {
        u32* cmd_buff = Service::GetCommandBuffer();
        FileCommand cmd = static_cast<FileCommand>(cmd_buff[0]);
        switch (cmd) {

        // Read from file...
        case FileCommand::Read:
        {
            u64 offset = cmd_buff[1] | ((u64) cmd_buff[2]) << 32;
            u32 length  = cmd_buff[3];
            u32 address = cmd_buff[5];
            DEBUG_LOG(KERNEL, "Read %s %s: offset=0x%llx length=%d address=0x%x",
                      GetTypeName().c_str(), GetName().c_str(), offset, length, address);
            cmd_buff[2] = backend->Read(offset, length, Memory::GetPointer(address));
            break;
        }

        // Write to file...
        case FileCommand::Write:
        {
            u64 offset  = cmd_buff[1] | ((u64) cmd_buff[2]) << 32;
            u32 length  = cmd_buff[3];
            u32 flush   = cmd_buff[4];
            u32 address = cmd_buff[6];
            DEBUG_LOG(KERNEL, "Write %s %s: offset=0x%llx length=%d address=0x%x, flush=0x%x",
                      GetTypeName().c_str(), GetName().c_str(), offset, length, address, flush);
            cmd_buff[2] = backend->Write(offset, length, flush, Memory::GetPointer(address));
            break;
        }

        case FileCommand::GetSize:
        {
            DEBUG_LOG(KERNEL, "GetSize %s %s", GetTypeName().c_str(), GetName().c_str());
            u64 size = backend->GetSize();
            cmd_buff[2] = (u32)size;
            cmd_buff[3] = size >> 32;
            break;
        }

        case FileCommand::SetSize:
        {
            u64 size = cmd_buff[1] | ((u64)cmd_buff[2] << 32);
            DEBUG_LOG(KERNEL, "SetSize %s %s size=%llu",
                    GetTypeName().c_str(), GetName().c_str(), size);
            backend->SetSize(size);
            break;
        }

        case FileCommand::Close:
        {
            DEBUG_LOG(KERNEL, "Close %s %s", GetTypeName().c_str(), GetName().c_str());
            Kernel::g_object_pool.Destroy<File>(GetHandle());
            break;
        }

        // Unknown command...
        default:
            ERROR_LOG(KERNEL, "Unknown command=0x%08X!", cmd);
            ResultCode error = UnimplementedFunction(ErrorModule::FS);
            cmd_buff[1] = error.raw; // TODO(Link Mauve): use the correct error code for that.
            return error;
        }
        cmd_buff[1] = 0; // No error
        return MakeResult<bool>(false);
    }

    ResultVal<bool> WaitSynchronization() override {
        // TODO(bunnei): ImplementMe
        ERROR_LOG(OSHLE, "(UNIMPLEMENTED)");
        return UnimplementedFunction(ErrorModule::FS);
    }
};

class Directory : public Object {
public:
    std::string GetTypeName() const override { return "Directory"; }
    std::string GetName() const override { return path.DebugStr(); }

    static Kernel::HandleType GetStaticHandleType() { return HandleType::Directory; }
    Kernel::HandleType GetHandleType() const override { return HandleType::Directory; }

    FileSys::Path path; ///< Path of the directory
    std::unique_ptr<FileSys::Directory> backend; ///< File backend interface

    ResultVal<bool> SyncRequest() override {
        u32* cmd_buff = Service::GetCommandBuffer();
        DirectoryCommand cmd = static_cast<DirectoryCommand>(cmd_buff[0]);
        switch (cmd) {

        // Read from directory...
        case DirectoryCommand::Read:
        {
            u32 count = cmd_buff[1];
            u32 address = cmd_buff[3];
            auto entries = reinterpret_cast<FileSys::Entry*>(Memory::GetPointer(address));
            DEBUG_LOG(KERNEL, "Read %s %s: count=%d",
                    GetTypeName().c_str(), GetName().c_str(), count);

            // Number of entries actually read
            cmd_buff[2] = backend->Read(count, entries);
            break;
        }

        case DirectoryCommand::Close:
        {
            DEBUG_LOG(KERNEL, "Close %s %s", GetTypeName().c_str(), GetName().c_str());
            Kernel::g_object_pool.Destroy<Directory>(GetHandle());
            break;
        }

        // Unknown command...
        default:
            ERROR_LOG(KERNEL, "Unknown command=0x%08X!", cmd);
            ResultCode error = UnimplementedFunction(ErrorModule::FS);
            cmd_buff[1] = error.raw; // TODO(Link Mauve): use the correct error code for that.
            return error;
        }
        cmd_buff[1] = 0; // No error
        return MakeResult<bool>(false);
    }

    ResultVal<bool> WaitSynchronization() override {
        // TODO(bunnei): ImplementMe
        ERROR_LOG(OSHLE, "(UNIMPLEMENTED)");
        return UnimplementedFunction(ErrorModule::FS);
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////

std::map<FileSys::Archive::IdCode, Handle> g_archive_map; ///< Map of file archives by IdCode

ResultVal<Handle> OpenArchive(FileSys::Archive::IdCode id_code) {
    auto itr = g_archive_map.find(id_code);
    if (itr == g_archive_map.end()) {
        return ResultCode(ErrorDescription::NotFound, ErrorModule::FS,
                ErrorSummary::NotFound, ErrorLevel::Permanent);
    }

    return MakeResult<Handle>(itr->second);
}

ResultCode CloseArchive(FileSys::Archive::IdCode id_code) {
    auto itr = g_archive_map.find(id_code);
    if (itr == g_archive_map.end()) {
        ERROR_LOG(KERNEL, "Cannot close archive %d, does not exist!", (int)id_code);
        return InvalidHandle(ErrorModule::FS);
    }

    INFO_LOG(KERNEL, "Closed archive %d", (int) id_code);
    return RESULT_SUCCESS;
}

/**
 * Mounts an archive
 * @param archive Pointer to the archive to mount
 */
ResultCode MountArchive(Archive* archive) {
    FileSys::Archive::IdCode id_code = archive->backend->GetIdCode();
    ResultVal<Handle> archive_handle = OpenArchive(id_code);
    if (archive_handle.Succeeded()) {
        ERROR_LOG(KERNEL, "Cannot mount two archives with the same ID code! (%d)", (int) id_code);
        return archive_handle.Code();
    }
    g_archive_map[id_code] = archive->GetHandle();
    INFO_LOG(KERNEL, "Mounted archive %s", archive->GetName().c_str());
    return RESULT_SUCCESS;
}

ResultCode CreateArchive(FileSys::Archive* backend, const std::string& name) {
    Archive* archive = new Archive;
    Handle handle = Kernel::g_object_pool.Create(archive);
    archive->name = name;
    archive->backend = backend;

    ResultCode result = MountArchive(archive);
    if (result.IsError()) {
        return result;
    }
    
    return RESULT_SUCCESS;
}

ResultVal<Handle> OpenFileFromArchive(Handle archive_handle, const FileSys::Path& path, const FileSys::Mode mode) {
    // TODO(bunnei): Binary type files get a raw file pointer to the archive. Currently, we create
    // the archive file handles at app loading, and then keep them persistent throughout execution.
    // Archives file handles are just reused and not actually freed until emulation shut down.
    // Verify if real hardware works this way, or if new handles are created each time
    if (path.GetType() == FileSys::Binary)
        // TODO(bunnei): FixMe - this is a hack to compensate for an incorrect FileSys backend
        // design. While the functionally of this is OK, our implementation decision to separate
        // normal files from archive file pointers is very likely wrong.
        // See https://github.com/citra-emu/citra/issues/205
        return MakeResult<Handle>(archive_handle);

    File* file = new File;
    Handle handle = Kernel::g_object_pool.Create(file);

    Archive* archive = Kernel::g_object_pool.Get<Archive>(archive_handle);
    if (archive == nullptr) {
        return InvalidHandle(ErrorModule::FS);
    }
    file->path = path;
    file->backend = archive->backend->OpenFile(path, mode);

    if (!file->backend) {
        return ResultCode(ErrorDescription::NotFound, ErrorModule::FS,
                ErrorSummary::NotFound, ErrorLevel::Permanent);
    }

    return MakeResult<Handle>(handle);
}

ResultCode DeleteFileFromArchive(Handle archive_handle, const FileSys::Path& path) {
    Archive* archive = Kernel::g_object_pool.GetFast<Archive>(archive_handle);
    if (archive == nullptr)
        return InvalidHandle(ErrorModule::FS);
    if (archive->backend->DeleteFile(path))
        return RESULT_SUCCESS;
    return ResultCode(ErrorDescription::NoData, ErrorModule::FS, // TODO: verify description
                      ErrorSummary::Canceled, ErrorLevel::Status);
}

ResultCode RenameFileBetweenArchives(Handle src_archive_handle, const FileSys::Path& src_path,
                                     Handle dest_archive_handle, const FileSys::Path& dest_path) {
    Archive* src_archive = Kernel::g_object_pool.GetFast<Archive>(src_archive_handle);
    Archive* dest_archive = Kernel::g_object_pool.GetFast<Archive>(dest_archive_handle);
    if (src_archive == nullptr || dest_archive == nullptr)
        return InvalidHandle(ErrorModule::FS);
    if (src_archive == dest_archive) {
        if (src_archive->backend->RenameFile(src_path, dest_path))
            return RESULT_SUCCESS;
    } else {
        // TODO: Implement renaming across archives
        return UnimplementedFunction(ErrorModule::FS);
    }
    return ResultCode(ErrorDescription::NoData, ErrorModule::FS, // TODO: verify description
                      ErrorSummary::NothingHappened, ErrorLevel::Status);
}

ResultCode DeleteDirectoryFromArchive(Handle archive_handle, const FileSys::Path& path) {
    Archive* archive = Kernel::g_object_pool.GetFast<Archive>(archive_handle);
    if (archive == nullptr)
        return InvalidHandle(ErrorModule::FS);
    if (archive->backend->DeleteDirectory(path))
        return RESULT_SUCCESS;
    return ResultCode(ErrorDescription::NoData, ErrorModule::FS, // TODO: verify description
                      ErrorSummary::Canceled, ErrorLevel::Status);
}

ResultCode CreateDirectoryFromArchive(Handle archive_handle, const FileSys::Path& path) {
    Archive* archive = Kernel::g_object_pool.GetFast<Archive>(archive_handle);
    if (archive == nullptr)
        return InvalidHandle(ErrorModule::FS);
    if (archive->backend->CreateDirectory(path))
        return RESULT_SUCCESS;
    return ResultCode(ErrorDescription::NoData, ErrorModule::FS, // TODO: verify description
                      ErrorSummary::Canceled, ErrorLevel::Status);
}

ResultCode RenameDirectoryBetweenArchives(Handle src_archive_handle, const FileSys::Path& src_path,
                                          Handle dest_archive_handle, const FileSys::Path& dest_path) {
    Archive* src_archive = Kernel::g_object_pool.GetFast<Archive>(src_archive_handle);
    Archive* dest_archive = Kernel::g_object_pool.GetFast<Archive>(dest_archive_handle);
    if (src_archive == nullptr || dest_archive == nullptr)
        return InvalidHandle(ErrorModule::FS);
    if (src_archive == dest_archive) {
        if (src_archive->backend->RenameDirectory(src_path, dest_path))
            return RESULT_SUCCESS;
    } else {
        // TODO: Implement renaming across archives
        return UnimplementedFunction(ErrorModule::FS);
    }
    return ResultCode(ErrorDescription::NoData, ErrorModule::FS, // TODO: verify description
                      ErrorSummary::NothingHappened, ErrorLevel::Status);
}

/**
 * Open a Directory from an Archive
 * @param archive_handle Handle to an open Archive object
 * @param path Path to the Directory inside of the Archive
 * @return Opened Directory object
 */
ResultVal<Handle> OpenDirectoryFromArchive(Handle archive_handle, const FileSys::Path& path) {
    Directory* directory = new Directory;
    Handle handle = Kernel::g_object_pool.Create(directory);

    Archive* archive = Kernel::g_object_pool.Get<Archive>(archive_handle);
    if (archive == nullptr) {
        return InvalidHandle(ErrorModule::FS);
    }
    directory->path = path;
    directory->backend = archive->backend->OpenDirectory(path);

    if (!directory->backend) {
        return ResultCode(ErrorDescription::NotFound, ErrorModule::FS,
                          ErrorSummary::NotFound, ErrorLevel::Permanent);
    }

    return MakeResult<Handle>(handle);
}

/// Initialize archives
void ArchiveInit() {
    g_archive_map.clear();

    // TODO(Link Mauve): Add the other archive types (see here for the known types:
    // http://3dbrew.org/wiki/FS:OpenArchive#Archive_idcodes).  Currently the only half-finished
    // archive type is SDMC, so it is the only one getting exposed.

    std::string sdmc_directory = FileUtil::GetUserPath(D_SDMC_IDX);
    auto archive = new FileSys::Archive_SDMC(sdmc_directory);
    if (archive->Initialize())
        CreateArchive(archive, "SDMC");
    else
        ERROR_LOG(KERNEL, "Can't instantiate SDMC archive with path %s", sdmc_directory.c_str());
}

/// Shutdown archives
void ArchiveShutdown() {
    g_archive_map.clear();
}

} // namespace Kernel
