// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataValidationChangelist.h"

#include "Algo/AnyOf.h"
#include "Algo/Transform.h"
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "SourceControlOperations.h"

#define LOCTEXT_NAMESPACE "DataValidationChangelist"

void GatherDependencies(const FName& InPackageName, TSet<FName>& OutDependencies, bool bGetFullDependencies)
{
	if (OutDependencies.Contains(InPackageName))
	{
		return;
	}

	OutDependencies.Add(InPackageName);

	TArray<FAssetData> Assets;
	TArray<FName> Dependencies;
	USourceControlHelpers::GetAssetDataFromPackage(InPackageName.ToString(), Assets, &Dependencies);

	for (const FName& PackageDependency : Dependencies)
	{
		if (bGetFullDependencies)
		{
			GatherDependencies(PackageDependency, OutDependencies, bGetFullDependencies);
		}
		else if (!OutDependencies.Contains(PackageDependency))
		{
			OutDependencies.Add(PackageDependency);
		}
	}
}

FString GetPrettyPackageName(const FName& InPackageName)
{
	TArray<FAssetData> Assets;
	USourceControlHelpers::GetAssetDataFromPackage(InPackageName.ToString(), Assets);

	if (Assets.Num() > 0)
	{
		FString AssetPath = Assets[0].ObjectPath.ToString();

		int32 LastDot = -1;
		if (AssetPath.FindLastChar('.', LastDot))
		{
			AssetPath.LeftInline(LastDot);
		}

		FString AssetName;

		static FName NAME_ActorLabel(TEXT("ActorLabel"));
		if (Assets[0].FindTag(NAME_ActorLabel))
		{
			Assets[0].GetTagValue(NAME_ActorLabel, AssetName);
		}
		else
		{
			AssetName = Assets[0].AssetName.ToString();
		}

		return AssetPath + "." + AssetName;
	}
	else
	{
		return InPackageName.ToString();
	}
}

EDataValidationResult UDataValidationChangelist::IsDataValid(TArray<FText>& ValidationErrors)
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	
	FSourceControlChangelistStatePtr ChangelistState = SourceControlProvider.GetState(Changelist->AsShared(), EStateCacheUsage::Use);

	// Gather dependencies of every file in the changelist
	TArray<FName> FilesInChangelist;
	TSet<FName> ExternalDependenciesSet;
	const bool bGetFullDependencyHierarchy = false;

	for (const FSourceControlStateRef& File : ChangelistState->GetFilesStates())
	{
		// We shouldn't consider dependencies of deleted files
		if (File->IsDeleted())
		{
			continue;
		}

		FString PackageName;
		if (FPackageName::TryConvertFilenameToLongPackageName(File->GetFilename(), PackageName))
		{
			FilesInChangelist.Add(*PackageName);
			GatherDependencies(*PackageName, ExternalDependenciesSet, bGetFullDependencyHierarchy);
		}
	}

	// For every dependency in the external dependencies that is not in the changelist
	// Make sure that the source control state is "not currently modified"
	TArray<FName> ExternalDependencies = ExternalDependenciesSet.Array();

	ExternalDependencies.RemoveAll([&FilesInChangelist](FName& File) -> bool {
		return Algo::AnyOf(FilesInChangelist, [&File](const auto& FileInChangelist) {
			return File == FileInChangelist;
			});
		});

	bool bHasChangelistErrors = false;

	TArray<FString> ExternalDependenciesFilenames;

	Algo::Transform(ExternalDependencies, ExternalDependenciesFilenames, [](const FName& InFilename) -> FString
	{
		return USourceControlHelpers::PackageFilename(InFilename.ToString());
	});

	// Update External dependencies state in case it changed from what is in cache
	SourceControlProvider.Execute(ISourceControlOperation::Create<FUpdateStatus>(), ExternalDependenciesFilenames);

	check(ExternalDependenciesFilenames.Num() == ExternalDependencies.Num());

	for (int32 i = 0; i < ExternalDependenciesFilenames.Num(); ++i)
	{
		const FString& ExternalPackageFilename = ExternalDependenciesFilenames[i];
		const FName& ExternalDependency = ExternalDependencies[i];

		FSourceControlStatePtr ExternalDependencyFileState = SourceControlProvider.GetState(ExternalPackageFilename, EStateCacheUsage::Use);

		// Check if file is in cache; if it's not in the cache, then it's not currently changed.
		if (!ExternalDependencyFileState)
		{
			continue;
		}

		// If file is checked out, added, it's not in this changelist, which is a problem
		if (ExternalDependencyFileState->IsCheckedOut() || ExternalDependencyFileState->IsAdded())
		{
			bHasChangelistErrors = true;
			FText CurrentError = FText::Format(LOCTEXT("DataValidation.Changelist.Error", "{0} is missing from this changelist."), FText::FromString(GetPrettyPackageName(ExternalDependency)));
			ValidationErrors.Add(CurrentError);
		}
	}

	return bHasChangelistErrors ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}

void UDataValidationChangelist::Initialize(FSourceControlChangelistPtr InChangelist)
{
	Changelist = InChangelist;
}

#undef LOCTEXT_NAMESPACE