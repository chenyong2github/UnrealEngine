// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/UserFileCommon.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_playerdatastorage_types.h"

namespace UE::Online {

class FOnlineServicesEOSGS;

struct FUserFileEOSGSConfig
{
	int32 ChunkLengthBytes = 4096;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FUserFileEOSGSConfig)
	ONLINE_STRUCT_FIELD(FUserFileEOSGSConfig, ChunkLengthBytes)
END_ONLINE_STRUCT_META()

/* Meta */ }

class ONLINESERVICESEOSGS_API FUserFileEOSGS : public FUserFileCommon
{
public:
	using Super = FUserFileCommon;

	FUserFileEOSGS(FOnlineServicesEOSGS& InOwningSubsystem);
	virtual ~FUserFileEOSGS() = default;

	// IOnlineComponent
	virtual void Initialize() override;
	virtual void LoadConfig() override;

	// IUserFile
	virtual TOnlineAsyncOpHandle<FUserFileEnumerateFiles> EnumerateFiles(FUserFileEnumerateFiles::Params&& Params) override;
	virtual TOnlineResult<FUserFileGetEnumeratedFiles> GetEnumeratedFiles(FUserFileGetEnumeratedFiles::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUserFileReadFile> ReadFile(FUserFileReadFile::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUserFileWriteFile> WriteFile(FUserFileWriteFile::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUserFileCopyFile> CopyFile(FUserFileCopyFile::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUserFileDeleteFile> DeleteFile(FUserFileDeleteFile::Params&& Params) override;

protected:
	EOS_HPlayerDataStorage PlayerDataStorageHandle = nullptr;

	FUserFileEOSGSConfig Config;

	TMap<FOnlineAccountIdHandle, TArray<FString>> UserToFiles;

	static void EOS_CALL OnFileTransferProgressStatic(const EOS_PlayerDataStorage_FileTransferProgressCallbackInfo* Data);
};

/* UE::Online */ }
