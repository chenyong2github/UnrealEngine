// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHLODsBuilder.h"

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Logging/LogMacros.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"

#include "EngineUtils.h"
#include "SourceControlHelpers.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"

#include "DerivedDataCacheInterface.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionHLODsBuilder, All, All);

class FSourceControlHelper : public ISourceControlHelper
{
public:
	FSourceControlHelper(FPackageSourceControlHelper& InPackageHelper)
		: PackageHelper(InPackageHelper)
	{}

	virtual ~FSourceControlHelper()
	{}

	virtual FString GetFilename(const FString& PackageName) const override
	{
		return SourceControlHelpers::PackageFilename(PackageName);
	}

	virtual FString GetFilename(UPackage* Package) const override
	{
		return SourceControlHelpers::PackageFilename(Package);
	}

	virtual bool Checkout(UPackage* Package) const override
	{
		ModifiedFiles.Add(GetFilename(Package));
		return PackageHelper.Checkout(Package);
	}

	virtual bool Add(UPackage* Package) const override
	{
		ModifiedFiles.Add(GetFilename(Package));
		return PackageHelper.AddToSourceControl(Package);
	}

	virtual bool Delete(const FString& PackageName) const override
	{
		ModifiedFiles.Add(PackageName);
		return PackageHelper.Delete(PackageName);
	}

	virtual bool Delete(UPackage* Package) const override
	{
		ModifiedFiles.Add(GetFilename(Package));
		return PackageHelper.Delete(Package);
	}

	virtual bool Save(UPackage* Package) const override
	{
		ModifiedFiles.Add(GetFilename(Package));

		// Checkout package
		Package->MarkAsFullyLoaded();

		if (!Checkout(Package))
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Error checking out package %s."), *Package->GetName());
			return false;
		}

		// Save package
		FString PackageFileName = GetFilename(Package);
		if (!UPackage::SavePackage(Package, nullptr, RF_Standalone, *PackageFileName))
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Error saving package %s."), *Package->GetName());
			return false;
		}

		// Add new package to source control
		if (!Add(Package))
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Error adding package %s to source control."), *Package->GetName());
			return false;
		}

		return true;
	}

	const TSet<FString> GetModifiedFiles() const
	{
		return ModifiedFiles;
	}

private:
	FPackageSourceControlHelper& PackageHelper;
	mutable TSet<FString> ModifiedFiles;
};

static const FString DistributedBuildWorkingDirName = TEXT("HLODTemp");
static const FString DistributedBuildManifestName = TEXT("HLODBuildManifest.ini");
static const FString BuildProductsFileName = TEXT("BuildProducts.txt");

FString GetHLODBuilderFolderName(uint32 BuilderIndex) { return FString::Printf(TEXT("HLODBuilder%d"), BuilderIndex); }
FString GetToSubmitFolderName() { return TEXT("ToSubmit"); }

UWorldPartitionHLODsBuilder::UWorldPartitionHLODsBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, BuilderIdx(INDEX_NONE)
	, BuilderCount(INDEX_NONE)
	, DistributedBuildWorkingDir(FPaths::RootDir() / DistributedBuildWorkingDirName)
	, DistributedBuildManifest(DistributedBuildWorkingDir / DistributedBuildManifestName)
{
	bSetupHLODs = FParse::Param(FCommandLine::Get(), TEXT("SetupHLODs"));
	bBuildHLODs = FParse::Param(FCommandLine::Get(), TEXT("BuildHLODs"));
	bDeleteHLODs = FParse::Param(FCommandLine::Get(), TEXT("DeleteHLODs"));
	bSubmitHLODs = FParse::Param(FCommandLine::Get(), TEXT("SubmitHLODs"));

	bSingleBuildStep = !bSetupHLODs && !bBuildHLODs && !bSubmitHLODs && !bDeleteHLODs;

	bAutoSubmit = FParse::Param(FCommandLine::Get(), TEXT("AutoSubmit"));

	bDistributedBuild = FParse::Param(FCommandLine::Get(), TEXT("DistributedBuild"));

	FParse::Value(FCommandLine::Get(), TEXT("BuildManifest="), BuildManifest);
	FParse::Value(FCommandLine::Get(), TEXT("BuilderIdx="), BuilderIdx);
	FParse::Value(FCommandLine::Get(), TEXT("BuilderCount="), BuilderCount);

	if (bDistributedBuild)
	{
		if (!BuildManifest.IsEmpty())
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Warning, TEXT("Ignoring parameter -BuildManifest when a distributed build is performed"));
		}

		BuildManifest = DistributedBuildManifest;
	}
}

bool UWorldPartitionHLODsBuilder::RequiresCommandletRendering() const
{
	// Commandlet requires rendering only for building HLODs
	// Building will occur either if -BuildHLODs is provided or no explicit step arguments are provided
	return bBuildHLODs || !(bSetupHLODs || bDeleteHLODs || bSubmitHLODs);
}

bool UWorldPartitionHLODsBuilder::ValidateParams() const
{
	if (bSetupHLODs && IsUsingBuildManifest())
	{
		if (BuilderCount <= 0)
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Missing parameter -BuilderCount=N (where N > 0), exiting..."));
			return false;
		}
	}

	if (bBuildHLODs && IsUsingBuildManifest())
	{
		if (BuilderIdx < 0)
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Missing parameter -BuilderIdx=i, exiting..."));
			return false;
		}

		if (!FPaths::FileExists(BuildManifest))
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Build manifest file \"%s\" not found, exiting..."), *BuildManifest);
			return false;
		}

		FString CurrentEngineVersion = FEngineVersion::Current().ToString();
		FString ManifestEngineVersion = TEXT("unknown");

		FConfigFile ConfigFile;
		ConfigFile.Read(BuildManifest);
		const FConfigSection* ConfigSection = ConfigFile.Find(TEXT("General"));
		if (ConfigSection)
		{
			const FConfigValue* ConfigValue = ConfigSection->Find(TEXT("EngineVersion"));
			if (ConfigValue)
			{
				ManifestEngineVersion = ConfigValue->GetValue();
			}
		}
		if (ManifestEngineVersion != CurrentEngineVersion)
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Build manifest engine version doesn't match current engine version (%s vs %s), exiting..."), *ManifestEngineVersion, *CurrentEngineVersion);
			return false;
		}
	}

	if (bSubmitHLODs && !IsDistributedBuild())
	{
		UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("-SubmitHLODs argument only valid for distributed builds, exiting..."), *BuildManifest);
		return false;
	}

	if (bAutoSubmit && !bSingleBuildStep)
	{
		UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("-AutoSubmit argument only valid when building HLODs in a single step, exiting..."), *BuildManifest);
		return false;
	}

	if (IsDistributedBuild() && !ISourceControlModule::Get().GetProvider().IsEnabled())
	{
		UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Distributed builds requires that a valid source control provider is enabled, exiting..."), *BuildManifest);
		return false;
	}

	return true;
}

bool UWorldPartitionHLODsBuilder::PreWorldInitialization(FPackageSourceControlHelper& PackageHelper)
{
	if (!ValidateParams())
	{
		return false;
	}

	bool bRet = true;

	// When running a distributed build, retrieve relevant build products from the previous steps
	if (IsDistributedBuild() && (bBuildHLODs || bSubmitHLODs))
	{
		FString WorkingDirFolder = bBuildHLODs ? GetHLODBuilderFolderName(BuilderIdx) : GetToSubmitFolderName();
		bRet = CopyFilesFromWorkingDir(WorkingDirFolder);
	}

	return bRet;
}

bool UWorldPartitionHLODsBuilder::RunInternal(UWorld* World, const FBox& Bounds, FPackageSourceControlHelper& PackageHelper)
{
	WorldPartition = World->GetWorldPartition();
	check(WorldPartition);

	SourceControlHelper = new FSourceControlHelper(PackageHelper);

	bool bRet = true;

	if (bSingleBuildStep)
	{
		bRet = SetupHLODActors(false);
	}
	else
	{
		if (bSetupHLODs)
		{
			bRet = SetupHLODActors(true);
		}
		else if (bBuildHLODs)
		{
			bRet = BuildHLODActors();
		}
		else if (bDeleteHLODs)
		{
			bRet = DeleteHLODActors();
		}
	}

	if (bRet && (bSubmitHLODs || bAutoSubmit))
	{
		bRet = SubmitHLODActors();
	}

	WorldPartition = nullptr;
	delete SourceControlHelper;

	// TODO: DDC shutdown crash workaround - Wait for any DDC writes to complete
	GetDerivedDataCacheRef().WaitForQuiescence(true);
	
	return bRet;
}

bool UWorldPartitionHLODsBuilder::SetupHLODActors(bool bCreateOnly)
{
	WorldPartition->GenerateHLOD(SourceControlHelper, bCreateOnly);

	if (bCreateOnly)
	{
		// When performing a distributed build, ensure our work folder is empty
		if (IsDistributedBuild())
		{
			IFileManager::Get().DeleteDirectory(*DistributedBuildWorkingDir, false, true);
		}

		UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("#### World HLOD actors ####"));

		int32 NumActors = 0;
		for (UActorDescContainer::TIterator<AWorldPartitionHLOD> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
		{
			FWorldPartitionActorDesc* HLODActorDesc = *HLODIterator;
			FString PackageName = HLODActorDesc->GetActorPackage().ToString();

			UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("    [%d] %s"), NumActors, *PackageName);

			NumActors++;
		}

		UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("#### World contains %d HLOD actors ####"), NumActors);

		if (IsUsingBuildManifest())
		{
			TMap<FString, int32> FilesToBuilderMap;
			bool bGenerated = GenerateBuildManifest(FilesToBuilderMap);
			if (!bGenerated)
			{
				return false;
			}

			// When performing a distributed build, move modified files to the temporary working dir, to be submitted later in the last "submit" step
			if (IsDistributedBuild())
			{
				// Ensure we don't hold on to packages of always loaded actors
				// When running distributed builds, we wanna leave the machine clean, so added files are deleted, check'd out files are reverted
				// and deleted files are restored.
				WorldPartition->Uninitialize();
				DoCollectGarbage();

				ModifiedFiles.Append(SourceControlHelper->GetModifiedFiles());

				TArray<TArray<FString>> BuildersFiles;
				BuildersFiles.SetNum(BuilderCount);

				for (const FString& ModifiedFile : ModifiedFiles)
				{
					int32* Idx = FilesToBuilderMap.Find(ModifiedFile);
					if (Idx)
					{
						BuildersFiles[*Idx].Add(ModifiedFile);
					}
					else
					{
						// Add general files to the last builder
						BuildersFiles.Last().Add(ModifiedFile);
					}
				}

				// Gather build product to ensure intermediary files are copied between the different HLOD generation steps
				TArray<FString> BuildProducts;

				// Copy files that will be handled by the different builders
				for (int32 Idx = 0; Idx < BuilderCount; Idx++)
				{
					if (!CopyFilesToWorkingDir(GetHLODBuilderFolderName(Idx), BuildersFiles[Idx], BuildProducts))
					{
						return false;
					}
				}

				// The build manifest must also be included as a build product to be available in the next steps
				BuildProducts.Add(BuildManifest);

				// Write build products to a file
				FString BuildProductsFile = DistributedBuildWorkingDir / BuildProductsFileName;
				bool bRet = FFileHelper::SaveStringArrayToFile(BuildProducts, *BuildProductsFile);
				if (!bRet)
				{
					UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Error writing build product file %s"), *BuildProductsFile);
					return false;
				}

				ModifiedFiles.Empty();
			}
		}
	}

	return true;
}

bool UWorldPartitionHLODsBuilder::BuildHLODActors()
{
	TArray<FGuid> HLODActorsToBuild;
	if (!GetHLODActorsToBuild(HLODActorsToBuild))
	{
		return false;
	}

	if (!ValidateWorkload(HLODActorsToBuild))
	{
		return false;
	}

	UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("#### Building %d HLOD actors ####"), HLODActorsToBuild.Num());

	int32 CurrentActor = 0;
	for (const FGuid& HLODActorGuid : HLODActorsToBuild)
	{
		FWorldPartitionReference ActorRef(WorldPartition, HLODActorGuid);
		FWorldPartitionActorDesc* ActorDesc = ActorRef.Get();

		AWorldPartitionHLOD* HLODActor = CastChecked<AWorldPartitionHLOD>(ActorDesc->GetActor());

		CurrentActor++;
		UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("    [%d/%d] Building HLOD actor %s..."), CurrentActor, HLODActorsToBuild.Num(), *HLODActor->GetActorLabel());

		HLODActor->BuildHLOD();

		UPackage* ActorPackage = HLODActor->GetPackage();
		if (ActorPackage->IsDirty())
		{
			bool bSaved = SourceControlHelper->Save(ActorPackage);
			if (!bSaved)
			{
				UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Failed to save %s, exiting..."), *USourceControlHelpers::PackageFilename(ActorPackage));
				return false;
			}
		}

		if (FWorldPartitionHelpers::HasExceededMaxMemory())
		{
			DoCollectGarbage();
		}
	}

	UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("#### Built %d HLOD actors ####"), HLODActorsToBuild.Num());

	// Move modified files to the temporary working dir, to be submitted later in the final "submit" pass, from a single machine.
	if (IsDistributedBuild())
	{
		// Ensure we don't hold on to packages of always loaded actors
		// When running distributed builds, we wanna leave the machine clean, so added files are deleted, check'd out files are reverted
		// and deleted files are restored.
		WorldPartition->Uninitialize();
		DoCollectGarbage();

		ModifiedFiles.Append(SourceControlHelper->GetModifiedFiles());

		TArray<FString> BuildProducts;

		if (!CopyFilesToWorkingDir("ToSubmit", ModifiedFiles.Array(), BuildProducts))
		{
			return false;
		}

		// Write build products to a file
		FString BuildProductsFile = DistributedBuildWorkingDir / BuildProductsFileName;
		bool bRet = FFileHelper::SaveStringArrayToFile(BuildProducts, *BuildProductsFile);
		if (!bRet)
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Error writing build product file %s"), *BuildProductsFile);
			return false;
		}

		ModifiedFiles.Empty();
	}

	return true;
}

bool UWorldPartitionHLODsBuilder::DeleteHLODActors()
{
	UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("#### Deleting HLOD actors ####"));

	TArray<FString> PackagesToDelete;
	for (UActorDescContainer::TIterator<AWorldPartitionHLOD> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
	{
		FWorldPartitionActorDesc* HLODActorDesc = *HLODIterator;
		FString PackageName = HLODActorDesc->GetActorPackage().ToString();
		PackagesToDelete.Add(PackageName);
	}

	// Ensure we don't hold on to packages of always loaded actors
	// When running distributed builds, we wanna leave the machine clean, so added files are deleted, checked out files are reverted
	// and deleted files are restored.
	WorldPartition->Uninitialize();
	DoCollectGarbage();

	int32 NumDeleted = 0;
	for (const FString& PackageName : PackagesToDelete)
	{
		bool bDeleted = SourceControlHelper->Delete(PackageName);
		if (bDeleted)
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("    [%d] %s"), NumDeleted, *PackageName);
		}
		else
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Failed to delete %s, exiting..."), *PackageName);
			return false;
		}
		NumDeleted++;
	}

	UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("#### Deleted %d HLOD actors ####"), NumDeleted);

	return true;
}

bool UWorldPartitionHLODsBuilder::SubmitHLODActors()
{
	bool bRet = true;

	// Ensure all files modified by the source control helper are taken into account
	ModifiedFiles.Append(SourceControlHelper->GetModifiedFiles());

	// Check in all modified files
	if (ModifiedFiles.Num() > 0)
	{
		FText ChangelistDescription = FText::FromString(FString::Printf(TEXT("Rebuilt HLODs for \"%s\" at %s"), *WorldPartition->GetWorld()->GetName(), *FEngineVersion::Current().ToString()));

		TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckInOperation = ISourceControlOperation::Create<FCheckIn>();
		CheckInOperation->SetDescription(ChangelistDescription);
		bRet = ISourceControlModule::Get().GetProvider().Execute(CheckInOperation, ModifiedFiles.Array()) == ECommandResult::Succeeded;
		if (!bRet)
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Failed to submit %d files to source control."), ModifiedFiles.Num());
		}
		else
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("#### Submitted %d files to source control ####"), ModifiedFiles.Num());
		}
	}
	else
	{
		UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("#### No files to submit ####"));
	}

	return bRet;
}

bool UWorldPartitionHLODsBuilder::GetHLODActorsToBuild(TArray<FGuid>& HLODActorsToBuild) const
{
	bool bRet = true;

	if (!BuildManifest.IsEmpty())
	{
		// Get HLOD actors to build from the BuildManifest file
		FConfigFile ConfigFile;
		ConfigFile.Read(BuildManifest);

		FString SectionName = GetHLODBuilderFolderName(BuilderIdx);

		const FConfigSection* ConfigSection = ConfigFile.Find(SectionName);
		if (ConfigSection)
		{
			TArray<FString> HLODActorGuidStrings;
			ConfigSection->MultiFind(TEXT("+HLODActorGuid"), HLODActorGuidStrings, /*bMaintainOrder=*/true);

			for (const FString& HLODActorGuidString : HLODActorGuidStrings)
			{
				FGuid HLODActorGuid;
				bRet = FGuid::Parse(HLODActorGuidString, HLODActorGuid);
				if (bRet)
				{
					HLODActorsToBuild.Add(HLODActorGuid);
				}
				else
				{
					UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Error parsing section [%s] in config file \"%s\""), *SectionName, *BuildManifest);
					break;
				}
			}
		}
		else
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Missing section [%s] in config file \"%s\""), *SectionName, *BuildManifest);
			bRet = false;
		}
	}
	else
	{
		TArray<TArray<FGuid>> HLODWorkloads = GetHLODWorldloads(1);
		HLODActorsToBuild = MoveTemp(HLODWorkloads[0]);
	}

	return bRet;
}

TArray<TArray<FGuid>> UWorldPartitionHLODsBuilder::GetHLODWorldloads(int32 NumWorkloads) const
{
	// Build a mapping of 1 HLOD[Level] -> N HLOD[Level - 1]
	TMap<FGuid, TArray<FGuid>>	HLODParenting;
	for (UActorDescContainer::TIterator<AWorldPartitionHLOD> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
	{
		TArray<FGuid>& ChildHLODs = HLODParenting.Add(HLODIterator->GetGuid());

		for (const FGuid& SubActorGuid : HLODIterator->GetSubActors())
		{
			FWorldPartitionActorDesc* SubActorDesc = WorldPartition->GetActorDesc(SubActorGuid);
			if (SubActorDesc && SubActorDesc->GetActorClass()->IsChildOf<AWorldPartitionHLOD>())
			{
				ChildHLODs.Add(SubActorGuid);
			}
		}
	}

	// All child HLODs must be built before their parent HLOD
	// Create groups to ensure those will be processed in the correct order, on the same builder
	TMap<FGuid, TArray<FGuid>> HLODGroups;
	TSet<FGuid>				   TriagedHLODs;

	TFunction<void(TArray<FGuid>&, const FGuid&)> RecursiveAdd = [&TriagedHLODs, &HLODParenting, &HLODGroups, &RecursiveAdd](TArray<FGuid>& HLODGroup, const FGuid& HLODGuid)
	{
		if (!TriagedHLODs.Contains(HLODGuid))
		{
			TriagedHLODs.Add(HLODGuid);
			HLODGroup.Insert(HLODGuid, 0); // Child will come first in the list, as they need to be built first...
			TArray<FGuid>* ChildHLODs = HLODParenting.Find(HLODGuid);
			if (ChildHLODs)
			{
				for (const auto& ChildGuid : *ChildHLODs)
				{
					RecursiveAdd(HLODGroup, ChildGuid);
				}
			}
		}
		else
		{
			HLODGroup.Insert(MoveTemp(HLODGroups.FindChecked(HLODGuid)), 0);
			HLODGroups.Remove(HLODGuid);
		}
	};

	for (const auto& Pair : HLODParenting)
	{
		if (!TriagedHLODs.Contains(Pair.Key))
		{
			TArray<FGuid>& HLODGroup = HLODGroups.Add(Pair.Key);
			RecursiveAdd(HLODGroup, Pair.Key);
		}
	}

	// Sort groups by number of HLOD actors
	HLODGroups.ValueSort([](const TArray<FGuid>& GroupA, const TArray<FGuid>& GroupB) { return GroupA.Num() > GroupB.Num(); });

	// Dispatch them in multiple lists and try to balance the workloads as much as possible
	TArray<TArray<FGuid>> Workloads;
	Workloads.SetNum(NumWorkloads);

	int32 Idx = 0;
	for (const auto& Pair : HLODGroups)
	{
		Workloads[Idx % NumWorkloads].Append(Pair.Value);
		Idx++;
	}

	// Validate workloads to ensure our meshes are built in the correct order
	for (const TArray<FGuid>& Workload : Workloads)
	{
		check(ValidateWorkload(Workload));
	}

	return Workloads;
}

bool UWorldPartitionHLODsBuilder::ValidateWorkload(const TArray<FGuid>&Workload) const
{
	TSet<FGuid> ProcessedHLOD;
	ProcessedHLOD.Reserve(Workload.Num());

	// For each HLOD entry in the workload, validate that its children are found before itself
	for (const FGuid& HLODActorGuid : Workload)
	{
		const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(HLODActorGuid);
		if(!ActorDesc)
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Unknown actor guid found, your HLOD actors are probably out of date. Run with -SetupHLODs to fix this. Exiting..."));
			return false;
		}

		if (!ActorDesc->GetActorClass()->IsChildOf<AWorldPartitionHLOD>())
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Unexpected actor guid found in HLOD workload, exiting..."));
			return false;
		}

		const FHLODActorDesc* HLODActorDesc = static_cast<const FHLODActorDesc*>(ActorDesc);

		for (const FGuid& SubActorGuid : HLODActorDesc->GetSubActors())
		{
			FWorldPartitionActorDesc* SubActorDesc = WorldPartition->GetActorDesc(SubActorGuid);

			// Invalid sub actor guid found, this is unexpected when running distributed builds as the build step is always preceeded by the setup step.
			check(SubActorDesc || !bDistributedBuild);

			if (SubActorDesc && SubActorDesc->GetActorClass()->IsChildOf<AWorldPartitionHLOD>())
			{
				if(!ProcessedHLOD.Contains(SubActorGuid))
				{
					UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Child HLOD actor missing or out of order in HLOD workload, exiting..."));
					return false;
				}
			}
		}

		ProcessedHLOD.Add(HLODActorGuid);
	}

	return true;
}

bool UWorldPartitionHLODsBuilder::GenerateBuildManifest(TMap<FString, int32>& FilesToBuilderMap) const
{
	TArray<TArray<FGuid>> BuildersWorkload = GetHLODWorldloads(BuilderCount);

	FConfigFile ConfigFile;

	FConfigSection& GeneralSection = ConfigFile.Add("General");
	GeneralSection.Add(TEXT("BuilderCount"), FString::FromInt(BuilderCount));
	GeneralSection.Add(TEXT("EngineVersion"), FEngineVersion::Current().ToString());

	for(int32 Idx = 0; Idx < BuilderCount; Idx++)
	{
		FString SectionName = GetHLODBuilderFolderName(Idx);

		FConfigSection& Section = ConfigFile.Add(SectionName);
		for(const FGuid& ActorGuid : BuildersWorkload[Idx])
		{
			Section.Add(TEXT("+HLODActorGuid"), ActorGuid.ToString(EGuidFormats::Digits));

			// Track which builder is responsible to handle each actor
			const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(ActorGuid);
			if (!ActorDesc)
			{
				UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Invalid actor GUID found while generating the HLOD build manifest, exiting..."));
				return false;
			}
			FString ActorPackageFilename = USourceControlHelpers::PackageFilename(ActorDesc->GetActorPackage().ToString());
			FilesToBuilderMap.Emplace(ActorPackageFilename, Idx);
		}
	}

	ConfigFile.Dirty = true;

	bool bRet = ConfigFile.Write(BuildManifest);
	if (!bRet)
	{
		UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Failed to write HLOD build manifest \"%s\""), *BuildManifest);
	}

	return bRet;
}

/*
	Working Dir structure
		/HLODBuilder0
			/Add
				NewFileA
				NewFileB
			/Delete
				DeletedFileA
				DeletedFileB
			/Edit
				EditedFileA
				EditedFileB

		/HLODBuilder1
			...
		/ToSubmit
			...

	Distributed mode
		* Distributed mode is ran into 3 steps
			* Setup (1 job)		
			* Build (N jobs)	
			* Submit (1 job)	
		
		* The Setup step will place files under the "HLODBuilder[0-N]" folder. Those files could be new or modified HLOD actors that will be built in the Build step. The setup step will also place files into the "ToSubmit" folder (deleted HLOD actors for example).
		* Each parallel job in the Build step will retrieve files from the "HLODBuilder[0-N]" folder. They will then proceed to build the HLOD actors as specified in the build manifest file. All built HLOD actor files will then be placed in the /ToSubmit folder.
		* The Submit step will gather all files under /ToSubmit and submit them.
		

		|			Setup			|					Build					  |		   Submit			|
		/Content -----------> /HLODBuilder -----------> /Content -----------> /ToSubmit -----------> /Content
*/

const FName FileAction_Add(TEXT("Add"));
const FName FileAction_Edit(TEXT("Edit"));
const FName FileAction_Delete(TEXT("Delete"));

bool UWorldPartitionHLODsBuilder::CopyFilesToWorkingDir(const FString& TargetDir, const TArray<FString>& FilesToCopy, TArray<FString>& BuildProducts)
{
	FString AbsoluteTargetDir = DistributedBuildWorkingDir / TargetDir / TEXT("");

	TArray<FString> FilesToDelete;

	for (const FString& SourceFilename : FilesToCopy)
	{
		FSourceControlState FileState = USourceControlHelpers::QueryFileState(SourceFilename);
		if (FileState.bIsValid)
		{
			bool bShouldCopyToWorkingDir = false;
			FName FileAction;

			if (FileState.bIsAdded)
			{
				FileAction = FileAction_Add;
				FilesToDelete.Add(SourceFilename);
			}
			else if (FileState.bIsCheckedOut)
			{
				FileAction = FileAction_Edit;
			}
			else if (FileState.bIsDeleted)
			{
				FileAction = FileAction_Delete;
			}

			if (!FileAction.IsNone())
			{
				FString SourceFilenameRelativeToRoot = SourceFilename;
				FPaths::MakePathRelativeTo(SourceFilenameRelativeToRoot, *FPaths::RootDir());

				FString TargetFilename = AbsoluteTargetDir / FileAction.ToString() / SourceFilenameRelativeToRoot;

				BuildProducts.Add(TargetFilename);

				if (FileAction != FileAction_Delete)
				{
					bool bRet = IFileManager::Get().Copy(*TargetFilename, *SourceFilename, false) == COPY_OK;
					if (!bRet)
					{
						UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Failed to copy file from \"%s\" to \"%s\""), *SourceFilename, *TargetFilename);
						return false;
					}
				}
				else
				{
					bool bRet = FFileHelper::SaveStringToFile(TEXT(""), *TargetFilename);
					if (!bRet)
					{
						UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Failed to create empty file at \"%s\""), *TargetFilename);
						return false;
					}
				}
			}
		}
	}

	bool bRet = USourceControlHelpers::RevertFiles(FilesToCopy);
	if (!bRet)
	{
		UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Failed to revert modified files: %s"), *USourceControlHelpers::LastErrorMsg().ToString());
		return false;
	}

	// Delete files we added
	for (const FString& FileToDelete : FilesToDelete)
	{
		if (!IFileManager::Get().Delete(*FileToDelete, false, true))
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Error deleting file %s locally"), *FileToDelete);
			return false;
		}
	}

	return true;
}

bool UWorldPartitionHLODsBuilder::CopyFilesFromWorkingDir(const FString& SourceDir)
{
	FString AbsoluteSourceDir = DistributedBuildWorkingDir / SourceDir / TEXT("");

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *AbsoluteSourceDir, TEXT("*.*"), true, false);

	TArray<FString>			FilesToAdd;
	TMap<FString, FString>	FilesToCopy;
	TArray<FString>			FilesToDelete;

	bool bRet = true;

	for(const FString& File : Files)
	{
		FString PathRelativeToWorkingDir = File;
		FPaths::MakePathRelativeTo(PathRelativeToWorkingDir, *AbsoluteSourceDir);

		FString FileActionString;
		const int32 SlashIndex = PathRelativeToWorkingDir.Find(TEXT("/"));
		if (SlashIndex != INDEX_NONE)
		{
			FileActionString = PathRelativeToWorkingDir.Mid(0, SlashIndex);
		}

		FPaths::MakePathRelativeTo(PathRelativeToWorkingDir, *(FileActionString / TEXT("")));
		FString FullPathInRootDirectory =  FPaths::RootDir() / PathRelativeToWorkingDir;

		FName FileAction(FileActionString);
		if (FileAction == FileAction_Add)
		{
			bRet = IFileManager::Get().Copy(*FullPathInRootDirectory, *File, false) == COPY_OK;
			if (!bRet)
			{
				UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Failed to copy file from \"%s\" to \"%s\""), *File, *FullPathInRootDirectory);
				return false;
			}
			FilesToAdd.Add(*FullPathInRootDirectory);
		}
		else if (FileAction == FileAction_Edit)
		{
			FilesToCopy.Add(FullPathInRootDirectory, File);
		}
		else if (FileAction == FileAction_Delete)
		{
			FilesToDelete.Add(*FullPathInRootDirectory);
		}
		else
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Unsupported file action %s for file %s"), *FileActionString, *FullPathInRootDirectory);
		}
	}

	// Add
	if (!FilesToAdd.IsEmpty())
	{
		bRet = USourceControlHelpers::MarkFilesForAdd(FilesToAdd);
		if (!bRet)
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Adding files to source control failed: %s"), *USourceControlHelpers::LastErrorMsg().ToString());
			while (!FPlatformMisc::IsDebuggerPresent())
			{
				FPlatformProcess::Sleep(0.1f);
			}
			return false;
		}
		ModifiedFiles.Append(FilesToAdd);
	}

	// Delete
	if (!FilesToDelete.IsEmpty())
	{
		bRet = USourceControlHelpers::MarkFilesForDelete(FilesToDelete);
		if (!bRet)
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Deleting files from source control failed: %s"), *USourceControlHelpers::LastErrorMsg().ToString());
			return false;
		}
		ModifiedFiles.Append(FilesToDelete);
	}

	// Edit
	if (!FilesToCopy.IsEmpty())
	{
		TArray<FString> FilesToEdit;
		FilesToCopy.GetKeys(FilesToEdit);

		bRet = USourceControlHelpers::CheckOutFiles(FilesToEdit);
		if (!bRet)
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Checking out files from source control failed: %s"), *USourceControlHelpers::LastErrorMsg().ToString());
			return false;
		}
		ModifiedFiles.Append(FilesToEdit);

		for (const auto& Pair : FilesToCopy)
		{
			bRet = IFileManager::Get().Copy(*Pair.Key, *Pair.Value) == COPY_OK;
			if (!bRet)
			{
				UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Failed to copy file from \"%s\" to \"%s\""), *Pair.Value, *Pair.Key);
				return false;
			}
		}
	}

	return true;
}