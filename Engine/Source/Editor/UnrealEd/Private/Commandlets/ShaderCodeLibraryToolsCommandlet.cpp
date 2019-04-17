// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Commandlets/ShaderCodeLibraryToolsCommandlet.h"
#include "Misc/Paths.h"

#include "PipelineFileCache.h"
#include "ShaderCodeLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogShaderCodeLibraryTools, Log, All);

UShaderCodeLibraryToolsCommandlet::UShaderCodeLibraryToolsCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UShaderCodeLibraryToolsCommandlet::Main(const FString& Params)
{
	return StaticMain(Params);
}

int32 UShaderCodeLibraryToolsCommandlet::StaticMain(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	if (Tokens.Num() >= 3)
	{
		FString const& Left = Tokens[0];
		FString const& Right = Tokens[1];
		FString const& Output = Tokens[2];
		bool bNativeFormat = Switches.Contains(TEXT("PreferNativeArchives")) || Switches.Contains(TEXT("-PreferNativeArchives"));
		
		bool bArchive = false;
		if (GConfig->GetBool(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("bSharedMaterialNativeLibraries"), bArchive, GGameIni))
		{
			bNativeFormat |= bArchive;
		}
		
		TArray<FString> OldMetaDataDirs;
		OldMetaDataDirs.Add(Left);
		
		return FShaderCodeLibrary::CreatePatchLibrary(OldMetaDataDirs, Right, Output, bNativeFormat) ? 0 : 1;
	}
	
	UE_LOG(LogShaderCodeLibraryTools, Warning, TEXT("Usage: <Path-To-Old-MetaData> <Path-To-New-MetaData> <Output-Path> [-PreferNativeArchives]\n"));
	return 0;
}
