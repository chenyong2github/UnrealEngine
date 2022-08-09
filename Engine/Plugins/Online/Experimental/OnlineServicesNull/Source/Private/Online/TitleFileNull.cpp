// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/TitleFileNull.h"

#include "Containers/StringConv.h"
#include "Online/AuthNull.h"
#include "Online/OnlineErrorDefinitions.h"
#include "Online/OnlineServicesNull.h"
#include "Online/OnlineServicesNullTypes.h"

namespace {

using FTitleFileContents = UE::Online::FTitleFileContents;
using FTitleFileContentsRef = UE::Online::FTitleFileContentsRef;

TMap<FString, FTitleFileContentsRef> GetTitleFilesFromConfig()
{
	const TCHAR* ConfigSection = TEXT("OnlineServices.Null.TitleFile");

	TMap<FString, FTitleFileContentsRef> Result;

	for (int FileIdx = 0;; FileIdx++)
	{
		FString Filename;
		GConfig->GetString(ConfigSection, *FString::Printf(TEXT("File_%d_Name"), FileIdx), Filename, GEngineIni);
		if (Filename.IsEmpty())
		{
			break;
		}

		FString FileContentsStr;
		GConfig->GetString(ConfigSection, *FString::Printf(TEXT("File_%d_Contents"), FileIdx), FileContentsStr, GEngineIni);

		if (!FileContentsStr.IsEmpty())
		{
			const FTCHARToUTF8 FileContentsUtf8(*FileContentsStr);
			Result.Emplace(MoveTemp(Filename), MakeShared<FTitleFileContents>((uint8*)FileContentsUtf8.Get(), FileContentsUtf8.Length()));
		}
	}

	return Result;
}

}

namespace UE::Online {

FTitleFileNull::FTitleFileNull(FOnlineServicesNull& InOwningSubsystem)
	: Super(InOwningSubsystem)
{
}

TOnlineAsyncOpHandle<FTitleFileEnumerateFiles> FTitleFileNull::EnumerateFiles(FTitleFileEnumerateFiles::Params&& Params)
{
	TOnlineAsyncOpRef<FTitleFileEnumerateFiles> Op = GetOp<FTitleFileEnumerateFiles>(MoveTemp(Params));

	if (!Services.Get<FAuthNull>()->IsLoggedIn(Op->GetParams().LocalUserId))
	{
		Op->SetError(Errors::InvalidUser());
		return Op->GetHandle();
	}

	if(!TitleFiles.IsSet())
	{
		TitleFiles.Emplace(GetTitleFilesFromConfig());
	}

	Op->SetResult({});
	return Op->GetHandle();
}

TOnlineResult<FTitleFileGetEnumeratedFiles> FTitleFileNull::GetEnumeratedFiles(FTitleFileGetEnumeratedFiles::Params&& Params)
{
	if (!Services.Get<FAuthNull>()->IsLoggedIn(Params.LocalUserId))
	{
		return TOnlineResult<FTitleFileGetEnumeratedFiles>(Errors::InvalidUser());
	}

	if (!TitleFiles.IsSet())
	{
		// Need to call EnumerateFiles first.
		return TOnlineResult<FTitleFileGetEnumeratedFiles>(Errors::InvalidState());
	}

	FTitleFileGetEnumeratedFiles::Result Result;
	TitleFiles->GenerateKeyArray(Result.Filenames);
	return TOnlineResult<FTitleFileGetEnumeratedFiles>(MoveTemp(Result));
	
}

TOnlineAsyncOpHandle<FTitleFileReadFile> FTitleFileNull::ReadFile(FTitleFileReadFile::Params&& Params)
{
	TOnlineAsyncOpRef<FTitleFileReadFile> Op = GetOp<FTitleFileReadFile>(MoveTemp(Params));

	if (!Services.Get<FAuthNull>()->IsLoggedIn(Op->GetParams().LocalUserId))
	{
		Op->SetError(Errors::InvalidUser());
		return Op->GetHandle();
	}

	if (Op->GetParams().Filename.IsEmpty())
	{
		Op->SetError(Errors::InvalidParams());
		return Op->GetHandle();
	}

	if (!TitleFiles.IsSet())
	{
		// Need to call EnumerateFiles first.
		Op->SetError(Errors::InvalidState());
		return Op->GetHandle();
	}

	const FTitleFileContentsRef* Found = TitleFiles->Find(Op->GetParams().Filename);
	if (!Found)
	{
		Op->SetError(Errors::NotFound());
		return Op->GetHandle();
	}

	Op->SetResult({*Found});
	return Op->GetHandle();
}

/* UE::Online */ }
