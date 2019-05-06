// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.
// Storage interface

#ifndef RAIL_SDK_RAIL_STORAGE_H
#define RAIL_SDK_RAIL_STORAGE_H

#include "rail/sdk/base/rail_component.h"
#include "rail/sdk/base/rail_string.h"
#include "rail/sdk/rail_result.h"
#include "rail/sdk/rail_storage_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

class IRailFile;
class IRailStreamFile;
// storage helper singleton
class IRailStorageHelper {
  public:
    virtual ~IRailStorageHelper() {}

    // Open a file
    // the interface will get a new object
    // when not using the file, you need call Release()
    // file_name could include a relative path
    virtual IRailFile* OpenFile(const RailString& filename, RailResult* result = NULL) = 0;

    // Create a file
    // the interface will get a new object
    // when not using the file, you need call Release()
    // file_name could include a relative path
    virtual IRailFile* CreateFile(const RailString& filename, RailResult* result = NULL) = 0;

    // The file_name parameter is only a file name, do not include a path
    virtual bool IsFileExist(const RailString& filename) = 0;

    // List the non-streamfiles included in the Description File.
    // If the Description File does not exist, return false.
    // If failed in getting user data directory, return false,
    // or the user data directory does not exist return false.
    virtual bool ListFiles(RailArray<RailString>* filelist) = 0;

    // Delete File
    virtual RailResult RemoveFile(const RailString& filename) = 0;

    virtual bool IsFileSyncedToCloud(const RailString& filename) = 0;

    // Get file timestamp
    virtual RailResult GetFileTimestamp(const RailString& filename, uint64_t* time_stamp) = 0;

    virtual uint32_t GetFileCount() = 0;

    virtual RailResult GetFileNameAndSize(uint32_t file_index,
                        RailString* filename,
                        uint64_t* file_size) = 0;

    virtual RailResult AsyncQueryQuota() = 0;

    virtual RailResult SetSyncFileOption(const RailString& filename,
                        const RailSyncFileOption& option) = 0;

    virtual bool IsCloudStorageEnabledForApp() = 0;

    virtual bool IsCloudStorageEnabledForPlayer() = 0;

    virtual RailResult AsyncPublishFileToUserSpace(const RailPublishFileToUserSpaceOption& option,
                        const RailString& user_data) = 0;

    // Open a stream file to read or write
    // the interface will get a new object
    // when not using the file, you need call Release()
    // file_name could include a relative path
    // if file does not exist and option's open_type
    // is kRailStreamOpenFileTypeTruncateWrite or kRailStreamOpenFileTypeAppendWrite
    // and option's unavaliabe_when_new_file_writing is true, then
    // SDK will rename the filename to a tmp file, and it will be renamed back
    // to the original filename when close the stream file.
    virtual IRailStreamFile* OpenStreamFile(const RailString& filename,
                                const RailStreamFileOption& option,
                                RailResult* result = NULL) = 0;

    // contents: "*", "*.dat", "sav*";
    virtual RailResult AsyncListStreamFiles(const RailString& contents,
                        const RailListStreamFileOption& option,
                        const RailString& user_data) = 0;

    // Async Rename file
    virtual RailResult AsyncRenameStreamFile(const RailString& old_filename,
                        const RailString& new_filename,
                        const RailString& user_data) = 0;

    // Async Delete file
    virtual RailResult AsyncDeleteStreamFile(const RailString& filename,
                        const RailString& user_data) = 0;

    // Get operation systems the IRailFile could be synced
    // return enumerate value of EnumRailStorageFileEnabledOS
    virtual uint32_t GetRailFileEnabledOS(const RailString& filename) = 0;

    // Set this IRailFile could be synced in those operation systems
    virtual RailResult SetRailFileEnabledOS(const RailString& filename,
                        EnumRailStorageFileEnabledOS sync_os) = 0;
};

// file class
class IRailFile : public IRailComponent {
  public:
    virtual ~IRailFile() {}

    // Get the file name
    virtual const RailString& GetFilename() = 0;

    // Read the contents of the file,
    // the return value is the byte size really read
    virtual uint32_t Read(void* buff, uint32_t bytes_to_read, RailResult* result = NULL) = 0;

    // Write to the file,
    // the return value is the byte size actually written
    virtual uint32_t Write(const void* buff,
                        uint32_t bytes_to_write,
                        RailResult* result = NULL) = 0;

    virtual RailResult AsyncRead(uint32_t bytes_to_read, const RailString& user_data) = 0;

    virtual RailResult AsyncWrite(const void* buffer,
                        uint32_t bytes_to_write,
                        const RailString& user_data) = 0;

    // Get the file size
    virtual uint32_t GetSize() = 0;

    // Close the file
    virtual void Close() = 0;
};

class IRailStreamFile : public IRailComponent {
  public:
    virtual ~IRailStreamFile() {}
    // Get the file name
    virtual const RailString& GetFilename() = 0;

    // Read the contents of the file
    // param offset is to set the current file pointer of the file
    // the function will read the file starting from the specified offset position.
    virtual RailResult AsyncRead(int32_t offset,
                        uint32_t bytes_to_read,
                        const RailString& user_data) = 0;

    // Write to the file
    virtual RailResult AsyncWrite(const void* buff,
                        uint32_t bytes_to_write,
                        const RailString& user_data) = 0;

    // Get the file size
    virtual uint64_t GetSize() = 0;

    // Close file and Some data may be lost when writing
    virtual RailResult Close() = 0;

    // Just close file. Some data may be lost when writing
    virtual void Cancel() = 0;
};

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_STORAGE_H
