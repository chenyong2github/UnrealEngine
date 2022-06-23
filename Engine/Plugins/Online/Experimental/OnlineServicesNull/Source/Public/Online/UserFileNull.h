// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Optional.h"
#include "Online/UserFileCommon.h"

namespace UE::Online {

class FOnlineServicesNull;

class ONLINESERVICESNULL_API FUserFileNull : public FUserFileCommon
{
public:
	using Super = FUserFileCommon;

	FUserFileNull(FOnlineServicesNull& InOwningSubsystem);

	// IUserFile
	virtual TOnlineAsyncOpHandle<FUserFileEnumerateFiles> EnumerateFiles(FUserFileEnumerateFiles::Params&& Params) override;
	virtual TOnlineResult<FUserFileGetEnumeratedFiles> GetEnumeratedFiles(FUserFileGetEnumeratedFiles::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUserFileReadFile> ReadFile(FUserFileReadFile::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUserFileWriteFile> WriteFile(FUserFileWriteFile::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUserFileCopyFile> CopyFile(FUserFileCopyFile::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUserFileDeleteFile> DeleteFile(FUserFileDeleteFile::Params&& Params) override;

protected:
	using FUserFileMap = TMap<FString, FUserFileContentsRef>;

	TMap<FOnlineAccountIdHandle, FUserFileMap> UserToFilesMap;

	TOptional<FUserFileMap> InitialFileStateFromConfig;
	void LoadUserFilesFromConfig();
};

/* UE::Online */ }
