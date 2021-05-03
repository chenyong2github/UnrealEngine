// Copyright Epic Games, Inc. All Rights Reserved.

#include "Virtualization/IVirtualizationBackend.h"

#include "Containers/StringView.h"
#include "HAL/FileManager.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"
#include "Virtualization/VirtualizationManager.h"
#include "VirtualizationUtilities.h"

namespace UE::Virtualization
{

/**
 * A basic backend based on the file system. This can be used to access/store virtualization
 * data either on a local disk or a network share. It is intended to be used as a caching system
 * to speed up operations (running a local cache or a shared cache for a site) rather than as the
 * proper backend solution.
 * 
 * Ini file setup:
 * 'Name'=(Type=FileSystem, Path="XXX")
 * Where 'Name' is the backend name in the hierarchy and 'XXX' if the path to the directory where
 * you want to files to be stored.
 */
class FFileSystemBackend : public IVirtualizationBackend
{
public:
	FFileSystemBackend(FStringView ConfigName)
		: IVirtualizationBackend(EOperations::Both)
	{
		Name = WriteToString<256>(TEXT("FFileSystemBackend - "), ConfigName).ToString();
	}

	virtual ~FFileSystemBackend() = default;

private:

	virtual bool Initialize(const FString& ConfigEntry) override
	{
		if (!FParse::Value(*ConfigEntry, TEXT("Path="), RootDirectory))
		{
			UE_LOG(LogVirtualization, Error, TEXT("[%s] 'Path=' not found in the config file"), *GetDebugString());
			return false;
		}

		FPaths::NormalizeDirectoryName(RootDirectory);

		if (RootDirectory.IsEmpty())
		{
			UE_LOG(LogVirtualization, Error, TEXT("[%s] Config file entry 'Path=' was empty"), *GetDebugString());
			return false;
		}

		// TODO: Validate that the given path is usable?

		UE_LOG(LogVirtualization, Log, TEXT("[%s] Using path: '%s'"), *GetDebugString(), *RootDirectory);

		return true;
	}

	virtual bool PushData(const FPayloadId& Id, const FCompressedBuffer& Payload) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FFileSystemBackend::PushData);

		if (DoesExist(Id))
		{
			UE_LOG(LogVirtualization, Verbose, TEXT("[%s] Already has a copy of the payload '%s'."), *GetDebugString(), *Id.ToString());
			return true;
		}

		TStringBuilder<512> FilePath;
		CreateFilePath(Id, FilePath);

		{
			// TODO: Should we write to a temp file and then move it once it has written?
			TUniquePtr<FArchive> FileAr(IFileManager::Get().CreateFileWriter(FilePath.ToString()));
			if (FileAr == nullptr)
			{
				UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to push payload '%s' to '%s'"), *GetDebugString(), *Id.ToString(), FilePath.ToString());
				return false;
			}

			for (const FSharedBuffer& Buffer : Payload.GetCompressed().GetSegments())
			{
				// Const cast because FArchive requires a non-const pointer!
				FileAr->Serialize(const_cast<void*>(Buffer.GetData()), static_cast<int64>(Buffer.GetSize()));
			}
		}

		return true;
	}

	virtual FCompressedBuffer PullData(const FPayloadId& Id) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FFileSystemBackend::PullData);

		TStringBuilder<512> FilePath;
		CreateFilePath(Id, FilePath);

		// TODO: Should we allow the error severity to be configured via ini or just not report this case at all?
		if (!IFileManager::Get().FileExists(FilePath.ToString()))
		{
			UE_LOG(LogVirtualization, Verbose, TEXT("[%s] Does not contain the payload '%s'"), *GetDebugString(), *Id.ToString());
			return FCompressedBuffer();
		}

		TUniquePtr<FArchive> FileAr(IFileManager::Get().CreateFileReader(FilePath.ToString()));
		if (FileAr == nullptr)
		{
			const uint32 SystemError = FPlatformMisc::GetLastError();
			// If we have a system error we can give a more informative error message but don't output it if the error is zero as 
			// this can lead to very confusing error messages.
			if (SystemError != 0)
			{
				TCHAR SystemErrorMsg[2048] = { 0 };
				FPlatformMisc::GetSystemErrorMessage(SystemErrorMsg, sizeof(SystemErrorMsg), SystemError);
				UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to load payload '%s' file '%s' due to system error: '%s' (%d))"), *GetDebugString(), *Id.ToString(), FilePath.ToString(), SystemErrorMsg, SystemError);
			}
			else
			{
				UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to load payload '%s' from '%s' (reason unknown)"), *GetDebugString(), *Id.ToString(), FilePath.ToString());
			}
		
			return FCompressedBuffer();
		}

		return FCompressedBuffer::FromCompressed(*FileAr);
	}

	bool DoesExist(const FPayloadId& Id)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FFileSystemBackend::DoesExist);

		TStringBuilder<512> FilePath;
		CreateFilePath(Id, FilePath);

		return IFileManager::Get().FileExists(FilePath.ToString());
	}

	virtual FString GetDebugString() const override
	{
		return Name;
	}

	void CreateFilePath(const FPayloadId& PayloadId, FStringBuilderBase& OutPath)
	{
		TStringBuilder<52> PayloadPath;
		Utils::PayloadIdToPath(PayloadId, PayloadPath);

		OutPath << RootDirectory << TEXT("/") << PayloadPath;
	}

private:

	FString Name;
	FString RootDirectory;
};

UE_REGISTER_VIRTUALIZATION_BACKEND_FACTORY(FFileSystemBackend, FileSystem);

} // namespace UE::Virtualization
