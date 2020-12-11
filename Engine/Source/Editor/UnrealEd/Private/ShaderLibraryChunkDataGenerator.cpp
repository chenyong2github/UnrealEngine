// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderLibraryChunkDataGenerator.h"
#include "IPlatformFileSandboxWrapper.h"
#include "Interfaces/ITargetPlatform.h"
#include "ShaderCodeLibrary.h"
#include "Misc/ConfigCacheIni.h"

FShaderLibraryChunkDataGenerator::FShaderLibraryChunkDataGenerator(const ITargetPlatform* TargetPlatform)
{
	// Find out if this platform requires stable shader keys, by reading the platform setting file.
	bOptedOut = false;
	PlatformNameUsedForIni = TargetPlatform->IniPlatformName();

	FConfigFile PlatformIniFile;
	FConfigCacheIni::LoadLocalIniFile(PlatformIniFile, TEXT("Engine"), true, *PlatformNameUsedForIni);
	PlatformIniFile.GetBool(TEXT("DevOptions.Shaders"), TEXT("bDoNotChunkShaderLib"), bOptedOut);
}


void FShaderLibraryChunkDataGenerator::GenerateChunkDataFiles(const int32 InChunkId, const TSet<FName>& InPackagesInChunk, const ITargetPlatform* TargetPlatform, FSandboxPlatformFile* InSandboxFile, TArray<FString>& OutChunkFilenames)
{
	if (!bOptedOut && InPackagesInChunk.Num() > 0)
	{
		checkf(PlatformNameUsedForIni == TargetPlatform->IniPlatformName(), TEXT("Mismatch between platform names in shaderlib chunk generator. Ini settings might have been applied incorrectly."));

		// get the sandbox content directory here, to relieve shaderlib from including the wrapper
		const FString InPlatformName = TargetPlatform->PlatformName();
		const FString ShaderlibContentSandboxRoot = (InSandboxFile->GetSandboxDirectory() / InSandboxFile->GetGameSandboxDirectoryName() / TEXT("Content")).Replace(TEXT("[Platform]"), *InPlatformName);
		const FString ShaderlibMetadataSandboxRoot = (InSandboxFile->GetSandboxDirectory() / InSandboxFile->GetGameSandboxDirectoryName() / TEXT("Metadata") / TEXT("PipelineCaches")).Replace(TEXT("[Platform]"), *InPlatformName);

		FShaderLibraryCooker::SaveShaderLibraryChunk(InChunkId, InPackagesInChunk, TargetPlatform, ShaderlibContentSandboxRoot, ShaderlibMetadataSandboxRoot, OutChunkFilenames);
	}
}
	