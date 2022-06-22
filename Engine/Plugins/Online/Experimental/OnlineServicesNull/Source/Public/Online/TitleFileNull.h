// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Optional.h"
#include "Online/TitleFileCommon.h"

namespace UE::Online {

class FOnlineServicesNull;

class ONLINESERVICESNULL_API FTitleFileNull : public FTitleFileCommon
{
public:
	using Super = FTitleFileCommon;

	FTitleFileNull(FOnlineServicesNull& InOwningSubsystem);

	// ITitleFile
	virtual TOnlineAsyncOpHandle<FTitleFileEnumerateFiles> EnumerateFiles(FTitleFileEnumerateFiles::Params&& Params) override;
	virtual TOnlineResult<FTitleFileGetEnumeratedFiles> GetEnumeratedFiles(FTitleFileGetEnumeratedFiles::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FTitleFileReadFile> ReadFile(FTitleFileReadFile::Params&& Params) override;

protected:
	TOptional<TMap<FString, FTitleFileContentsRef>> TitleFiles;
};

/* UE::Online */ }
