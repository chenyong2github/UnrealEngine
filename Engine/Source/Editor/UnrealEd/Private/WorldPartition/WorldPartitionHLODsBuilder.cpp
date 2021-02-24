// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHLODsBuilder.h"

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Logging/LogMacros.h"
#include "FileHelpers.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
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
		return PackageHelper.Checkout(Package);
	}

	virtual bool Add(UPackage* Package) const override
	{
		return PackageHelper.AddToSourceControl(Package);
	}

	virtual bool Delete(const FString& PackageName) const override
	{
		return PackageHelper.Delete(PackageName);
	}

	virtual bool Delete(UPackage* Package) const override
	{
		return PackageHelper.Delete(Package);
	}

	virtual bool Save(UPackage* Package) const override
	{
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

private:
	FPackageSourceControlHelper& PackageHelper;
};

UWorldPartitionHLODsBuilder::UWorldPartitionHLODsBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSetupHLODs = FParse::Param(FCommandLine::Get(), TEXT("SetupHLODs"));
	bBuildHLODs = FParse::Param(FCommandLine::Get(), TEXT("BuildHLODs"));
	bDeleteHLODs = FParse::Param(FCommandLine::Get(), TEXT("DeleteHLODs"));
	bForceGC = FParse::Param(FCommandLine::Get(), TEXT("ForceGC"));
}

bool UWorldPartitionHLODsBuilder::RequiresCommandletRendering() const
{
	return bBuildHLODs || !(bSetupHLODs || bDeleteHLODs);
}

bool UWorldPartitionHLODsBuilder::Run(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	WorldPartition = World->GetWorldPartition();
	check(WorldPartition);

	SourceControlHelper = new FSourceControlHelper(PackageHelper);

	bool bRet;
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
	else
	{
		bRet = SetupHLODActors(false);
	}

	WorldPartition = nullptr;
	delete SourceControlHelper;
	
	return bRet;
}

bool UWorldPartitionHLODsBuilder::SetupHLODActors(bool bCreateOnly)
{
	WorldPartition->GenerateHLOD(SourceControlHelper, bCreateOnly);

	if (bCreateOnly)
	{
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
	}

	return true;
}

bool UWorldPartitionHLODsBuilder::BuildHLODActors()
{
	int32 NumActors = 0;
	for (UActorDescContainer::TIterator<AWorldPartitionHLOD> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
	{
		NumActors++;
	}

	UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("#### Building %d HLOD actors ####"), NumActors);

	int32 CurrentActor = 0;
	for (UActorDescContainer::TIterator<AWorldPartitionHLOD> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
	{
		FWorldPartitionReference ActorRef(WorldPartition, HLODIterator->GetGuid());
		FWorldPartitionActorDesc* ActorDesc = ActorRef.Get();

		AWorldPartitionHLOD* HLODActor = CastChecked<AWorldPartitionHLOD>(ActorDesc->GetActor());

		CurrentActor++;
		UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("    [%d/%d] Building HLOD actor..."), CurrentActor, NumActors);

		HLODActor->BuildHLOD();

		if (HLODActor->GetPackage()->IsDirty())
		{
			SourceControlHelper->Save(HLODActor->GetPackage());
		}

		if (bForceGC || HasExceededMaxMemory())
		{
			DoCollectGarbage();
		}
	}

	UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("#### Built %d HLOD actors ####"), NumActors);

	return true;
}

bool UWorldPartitionHLODsBuilder::DeleteHLODActors()
{
	UE_LOG(LogWorldPartitionHLODsBuilder, Display, TEXT("#### Deleting HLOD actors ####"));

	int32 NumDeleted = 0;
	for (UActorDescContainer::TIterator<AWorldPartitionHLOD> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
	{
		FWorldPartitionActorDesc* HLODActorDesc = *HLODIterator;
		FString PackageName = HLODActorDesc->GetActorPackage().ToString();

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
