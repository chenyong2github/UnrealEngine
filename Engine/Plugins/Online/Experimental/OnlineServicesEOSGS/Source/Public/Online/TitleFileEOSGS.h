// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/TitleFileCommon.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_titlestorage_types.h"

namespace UE::Online {

class FOnlineServicesEOSGS;

struct FTitleFileEOSGSConfig
{
	FString SearchTag;
	int32 ReadChunkLengthBytes = 4096;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FTitleFileEOSGSConfig)
	ONLINE_STRUCT_FIELD(FTitleFileEOSGSConfig, SearchTag),
	ONLINE_STRUCT_FIELD(FTitleFileEOSGSConfig, ReadChunkLengthBytes)
END_ONLINE_STRUCT_META()

/* Meta */ }

class ONLINESERVICESEOSGS_API FTitleFileEOSGS : public FTitleFileCommon
{
public:
	using Super = FTitleFileCommon;

	FTitleFileEOSGS(FOnlineServicesEOSGS& InOwningSubsystem);
	virtual ~FTitleFileEOSGS() = default;

	// IOnlineComponent
	virtual void Initialize() override;
	virtual void LoadConfig() override;

	// ITitleFile
	virtual TOnlineAsyncOpHandle<FTitleFileEnumerateFiles> EnumerateFiles(FTitleFileEnumerateFiles::Params&& Params) override;
	virtual TOnlineResult<FTitleFileGetEnumeratedFiles> GetEnumeratedFiles(FTitleFileGetEnumeratedFiles::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FTitleFileReadFile> ReadFile(FTitleFileReadFile::Params&& Params) override;

protected:
	EOS_HTitleStorage TitleStorageHandle = nullptr;

	FTitleFileEOSGSConfig Config;

	TOptional<TArray<FString>> EnumeratedFiles;

	static void EOS_CALL OnFileTransferProgressStatic(const EOS_TitleStorage_FileTransferProgressCallbackInfo* Data);
};

/* UE::Online */ }
