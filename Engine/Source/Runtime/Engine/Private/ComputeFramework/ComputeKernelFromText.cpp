// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelFromText.h"
#include "ComputeFramework/ComputeKernel.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"

#include "Internationalization/Regex.h"

UComputeKernelFromText::UComputeKernelFromText()
{
	UniqueId = FGuid::NewGuid();
}

#if WITH_EDITOR

void UComputeKernelFromText::PreEditChange(FEditPropertyChain& PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	PrevSourceFile = SourceFile;
}

void UComputeKernelFromText::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* ModifiedProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	if (!ModifiedProperty)
	{
		return;
	}

	FName ModifiedPropName = ModifiedProperty->GetFName();

	if (ModifiedPropName == GET_MEMBER_NAME_CHECKED(UComputeKernelFromText, SourceFile))
	{
		ReparseKernelSourceText();
	}
}

void UComputeKernelFromText::ReparseKernelSourceText()
{
	ON_SCOPE_EXIT
	{
		//FriendlyName = FPaths::GetCleanFilename(SourceFile.FilePath);
		//Name = (FriendlyName.IsEmpty()) ? TEXT("Unset"_SV) : FriendlyName;
	};

	if (SourceFile.FilePath.IsEmpty())
	{
		EntryPointName = FString();
		PermutationSet = FComputeKernelPermutationSet();
		DefinitionsSet = FComputeKernelDefinitionsSet();

		return;
	}

	FString FullKernelPath = FPaths::ConvertRelativePathToFull(SourceFile.FilePath);

	IPlatformFile& PlatformFileSystem = IPlatformFile::GetPlatformPhysical();
	if (!PlatformFileSystem.FileExists(*FullKernelPath))
	{
		UE_LOG(ComputeKernel, Error, TEXT("Unable to find kernel file \"%s\""), *FullKernelPath);

		SourceFile = PrevSourceFile;
		return;
	}

	FString KernelSourceText;
	if (!FFileHelper::LoadFileToString(KernelSourceText, &PlatformFileSystem, *FullKernelPath))
	{
		UE_LOG(ComputeKernel, Error, TEXT("Unable to read kernel file \"%s\""), *FullKernelPath);

		SourceFile = PrevSourceFile;
		return;
	}

	const FRegexPattern KernelEntryPointPattern(TEXT(R"(KERNEL_ENTRY_POINT\(\s*([a-zA-Z_][\w\d]*)\s*\))"));
	{
		FRegexMatcher KernelEntryPointMatcher(KernelEntryPointPattern, KernelSourceText);
		if (KernelEntryPointMatcher.FindNext())
		{
			EntryPointName = KernelEntryPointMatcher.GetCaptureGroup(1);
		}
	}

	const FRegexPattern KernelPermutationBoolPattern(TEXT(R"(KERNEL_PERMUTATION_BOOL\(\s*([a-zA-Z_][\w\d]*)\s*\))"));
	{
		FComputeKernelPermutationSet NewPermutationSet;

		FRegexMatcher BoolPermuationMatcher(KernelPermutationBoolPattern, KernelSourceText);
		while (BoolPermuationMatcher.FindNext())
		{
			NewPermutationSet.BooleanOptions.Emplace(BoolPermuationMatcher.GetCaptureGroup(1));
		}

		PermutationSet = NewPermutationSet;
	}

	const FRegexPattern KernelDefinePattern(TEXT(R"(KERNEL_DEFINE\(\s*([a-zA-Z_][\w\d]*)\s*\))"));
	{
		FComputeKernelDefinitionsSet NewDefinitionsSet;

		FRegexMatcher DefineMatcher(KernelDefinePattern, KernelSourceText);
		while (DefineMatcher.FindNext())
		{
			NewDefinitionsSet.Defines.Emplace(DefineMatcher.GetCaptureGroup(1));
		}

		DefinitionsSet = NewDefinitionsSet;
	}

	/*
	uint64 NewHash = 0;

	NewHash = Hash(NewHash, UniqueId);
	NewHash = Hash(NewHash, *SourceFile.FilePath);
	NewHash = Hash(NewHash, *KernelSource);

	if (SourceHash != NewHash)
	{
		SourceHash = NewHash;
		// Trigger recompile
	}
	*/
}

#endif
