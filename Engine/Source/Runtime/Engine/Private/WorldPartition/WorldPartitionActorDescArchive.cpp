// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionActorDescArchive.h"
#include "WorldPartition/WorldPartitionClassDescRegistry.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "UObject/FortniteNCBranchObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "AssetRegistry/AssetData.h"

FActorDescArchive::FActorDescArchive(FArchive& InArchive, FWorldPartitionActorDesc* InActorDesc)
	: FArchiveProxy(InArchive)
	, ActorDesc(InActorDesc)
{
	check(InArchive.IsPersistent());

	SetIsPersistent(true);
	SetIsLoading(InArchive.IsLoading());

	UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
	UsingCustomVersion(FFortniteNCBranchObjectVersion::GUID);
	UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

	if (CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::WorldPartitionActorClassDescSerialize)
	{
		InnerArchive << InActorDesc->bIsDefaultActorDesc;
	}

	if (CustomVer(FFortniteNCBranchObjectVersion::GUID) >= FFortniteNCBranchObjectVersion::WorldPartitionActorDescNativeBaseClassSerialization)
	{
		if (CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::WorldPartitionActorDescActorAndClassPaths)
		{
			FName BaseClassPathName;
			InnerArchive << BaseClassPathName;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			InActorDesc->BaseClass = FAssetData::TryConvertShortClassNameToPathName(BaseClassPathName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
		else
		{
			InnerArchive << InActorDesc->BaseClass;
		}
	}

	if (CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::WorldPartitionActorDescActorAndClassPaths)
	{
		FName NativeClassPathName;
		InnerArchive << NativeClassPathName;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		InActorDesc->NativeClass = FAssetData::TryConvertShortClassNameToPathName(NativeClassPathName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	else
	{
		InnerArchive << InActorDesc->NativeClass;
	}

	// Get the class descriptor to do delta serialization
	FWorldPartitionClassDescRegistry& ClassDescRegistry = FWorldPartitionClassDescRegistry::Get();
	const FTopLevelAssetPath ClassPath = InActorDesc->BaseClass.IsValid() ? InActorDesc->BaseClass : InActorDesc->NativeClass;
	ClassDesc = InActorDesc->bIsDefaultActorDesc ? ClassDescRegistry.GetClassDescDefaultForClass(ClassPath) : ClassDescRegistry.GetClassDescDefaultForActor(ClassPath);
	if (!ClassDesc)
	{
		ClassDesc = ClassDescRegistry.GetClassDescDefault(FTopLevelAssetPath(TEXT("/Script/Engine.Actor")));
		//UE_LOG(LogWorldPartition, Warning, TEXT("Can't find class descriptor '%s' for '%s', using '%s'"), *ClassPath.ToString(), *InActorDesc->GetActorSoftPath().ToString(), *ClassDesc->GetActorSoftPath().ToString());
	}
	check(ClassDesc);
}
#endif