// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorDesc.h"

#if WITH_EDITOR
#include "Misc/HashBuilder.h"
#include "UObject/LinkerInstancingContext.h"
#include "UObject/UObjectHash.h"
#include "UObject/MetaData.h"
#include "Algo/Transform.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Misc/PackageName.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"
#include "UObject/FortniteNCBranchObjectVersion.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerUtils.h"
#include "Engine/Public/ActorReferencesUtils.h"
#endif

#if WITH_EDITOR
uint32 FWorldPartitionActorDesc::GlobalTag = 0;

FWorldPartitionActorDesc::FWorldPartitionActorDesc()
	: bIsUsingDataLayerAsset(false)
	, SoftRefCount(0)
	, HardRefCount(0)
	, Container(nullptr)
	, bIsForcedNonSpatiallyLoaded(false)
	, Tag(0)
{}

void FWorldPartitionActorDesc::Init(const AActor* InActor)
{	
	check(InActor->IsPackageExternal());

	Guid = InActor->GetActorGuid();
	check(Guid.IsValid());

	UClass* ActorClass = InActor->GetClass();

	// Get the first native class in the hierarchy
	ActorNativeClass = GetParentNativeClass(ActorClass);
	NativeClass = ActorNativeClass->GetFName();
	
	// For native class, don't set this
	if (!ActorClass->IsNative())
	{
		BaseClass = *InActor->GetClass()->GetPathName();
	}

	const FBox StreamingBounds = InActor->GetStreamingBounds();
	StreamingBounds.GetCenterAndExtents(BoundsLocation, BoundsExtent);

	RuntimeGrid = InActor->GetRuntimeGrid();
	bIsSpatiallyLoaded = InActor->GetIsSpatiallyLoaded();
	bActorIsEditorOnly = InActor->IsEditorOnly();
	bLevelBoundsRelevant = InActor->IsLevelBoundsRelevant();
	bActorIsHLODRelevant = InActor->IsHLODRelevant();
	HLODLayer = InActor->GetHLODLayer() ? FName(InActor->GetHLODLayer()->GetPathName()) : FName();
	
	// DataLayers
	{
		TArray<FName> LocalDataLayerAssetPaths;
		TArray<FName> LocalDataLayerInstanceNames;
		UDataLayerSubsystem* DataLayerSubsystem = UWorld::GetSubsystem<UDataLayerSubsystem>(InActor->GetWorld());
		
		LocalDataLayerAssetPaths.Reserve(InActor->GetDataLayerAssets().Num());
		for (const TObjectPtr<const UDataLayerAsset>& DataLayerAsset : InActor->GetDataLayerAssets())
		{
			if (DataLayerAsset && DataLayerSubsystem->GetDataLayerInstance(DataLayerAsset))
			{
				LocalDataLayerAssetPaths.Add(*DataLayerAsset->GetPathName());
			}
		}

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		LocalDataLayerInstanceNames = DataLayerSubsystem->GetDataLayerInstanceNames(InActor->GetActorDataLayers());
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		
		// Validation
		const bool bHasDataLayerAssets = LocalDataLayerAssetPaths.Num() > 0;
		const bool bHasDeprecatedDataLayers = LocalDataLayerInstanceNames.Num() > 0;
		check((!bHasDataLayerAssets && !bHasDeprecatedDataLayers) || (bHasDataLayerAssets != bHasDeprecatedDataLayers));

		// Init DataLayers persistent info
		bIsUsingDataLayerAsset = bHasDataLayerAssets;
		DataLayers = bIsUsingDataLayerAsset ? MoveTemp(LocalDataLayerAssetPaths) : MoveTemp(LocalDataLayerInstanceNames);

		// Init DataLayers transient info
		DataLayerInstanceNames = FDataLayerUtils::ResolvedDataLayerInstanceNames(this);
	}

	Tags = InActor->Tags;

	ActorPackage = InActor->GetPackage()->GetFName();
	ActorPath = *InActor->GetPathName();
	FolderPath = InActor->GetFolderPath();
	FolderGuid = InActor->GetFolderGuid();

	const AActor* AttachParentActor = InActor->GetAttachParentActor();
	if (AttachParentActor)
	{
		ParentActor = AttachParentActor->GetActorGuid();
	}
	
	TArray<AActor*> ActorReferences = ActorsReferencesUtils::GetExternalActorReferences(const_cast<AActor*>(InActor));

	if (ActorReferences.Num())
	{
		References.Empty(ActorReferences.Num());
		for(AActor* ActorReference: ActorReferences)
		{
			References.Add(ActorReference->GetActorGuid());
		}
	}

	ActorLabel = *InActor->GetActorLabel(false);

	Container = nullptr;
	ActorPtr = const_cast<AActor*>(InActor);
}

void FWorldPartitionActorDesc::Init(const FWorldPartitionActorDescInitData& DescData)
{
	ActorPackage = DescData.PackageName;
	ActorPath = DescData.ActorPath;
	ActorNativeClass = DescData.NativeClass;
	NativeClass = DescData.NativeClass->GetFName();

	// Serialize actor metadata
	FMemoryReader MetadataAr(DescData.SerializedData, true);

	// Serialize metadata custom versions
	FCustomVersionContainer CustomVersions;
	CustomVersions.Serialize(MetadataAr);
	MetadataAr.SetCustomVersions(CustomVersions);
	
	// Serialize metadata payload
	Serialize(MetadataAr);

	Container = nullptr;
}

bool FWorldPartitionActorDesc::Equals(const FWorldPartitionActorDesc* Other) const
{
	if (Guid == Other->Guid && 
		BaseClass == Other->BaseClass && 
		NativeClass == Other->NativeClass && 
		ActorPackage == Other->ActorPackage && 
		ActorPath == Other->ActorPath && 
		ActorLabel == Other->ActorLabel && 
		BoundsLocation.Equals(Other->BoundsLocation, 0.1f) &&
		BoundsExtent.Equals(Other->BoundsExtent, 0.1f) && 
		RuntimeGrid == Other->RuntimeGrid && 
		bActorIsEditorOnly == Other->bActorIsEditorOnly && 
		bLevelBoundsRelevant == Other->bLevelBoundsRelevant && 
		bActorIsHLODRelevant == Other->bActorIsHLODRelevant && 
		bIsUsingDataLayerAsset == Other->bIsUsingDataLayerAsset &&
		HLODLayer == Other->HLODLayer && 
		FolderPath == Other->FolderPath &&
		FolderGuid == Other->FolderGuid &&
		ParentActor == Other->ParentActor &&
		DataLayers.Num() == Other->DataLayers.Num() &&
		References.Num() == Other->References.Num())
	{
		TArray<FName> SortedDataLayers(DataLayers);
		TArray<FName> SortedDataLayersOther(Other->DataLayers);
		SortedDataLayers.Sort([](const FName& LHS, const FName& RHS) { return LHS.LexicalLess(RHS); });
		SortedDataLayersOther.Sort([](const FName& LHS, const FName& RHS) { return LHS.LexicalLess(RHS); });

		if (SortedDataLayers == SortedDataLayersOther)
		{
			TArray<FGuid> SortedReferences(References);
			TArray<FGuid> SortedReferencesOther(Other->References);
			SortedReferences.Sort();
			SortedReferencesOther.Sort();

			if (SortedReferences == SortedReferencesOther)
			{
				TArray<FName> SortedTags(Tags);
				TArray<FName> SortedTagsOther(Other->Tags);
				SortedTags.Sort([](const FName& LHS, const FName& RHS) { return LHS.LexicalLess(RHS); });
				SortedTagsOther.Sort([](const FName& LHS, const FName& RHS) { return LHS.LexicalLess(RHS); });

				return SortedTags == SortedTagsOther;
			}
		}
	}

	return false;
}

void FWorldPartitionActorDesc::SerializeTo(TArray<uint8>& OutData)
{
	// Serialize to archive and gather custom versions
	TArray<uint8> PayloadData;
	FMemoryWriter PayloadAr(PayloadData, true);
	Serialize(PayloadAr);

	// Serialize custom versions
	TArray<uint8> HeaderData;
	FMemoryWriter HeaderAr(HeaderData);
	FCustomVersionContainer CustomVersions = PayloadAr.GetCustomVersions();
	CustomVersions.Serialize(HeaderAr);

	// Append data
	OutData = MoveTemp(HeaderData);
	OutData.Append(PayloadData);
}

void FWorldPartitionActorDesc::TransformInstance(const FString& From, const FString& To, const FTransform& InstanceTransform)
{
	check(!HardRefCount);

	ActorPath = *ActorPath.ToString().Replace(*From, *To);

	// Transform BoundsLocation and BoundsExtent if necessary
	if (!InstanceTransform.Equals(FTransform::Identity))
	{
		//@todo_ow: This will result in a new BoundsExtent that is larger than it should. To fix this, we would need the Object Oriented BoundingBox of the actor (the BV of the actor without rotation)
		const FVector BoundsMin = BoundsLocation - BoundsExtent;
		const FVector BoundsMax = BoundsLocation + BoundsExtent;
		const FBox NewBounds = FBox(BoundsMin, BoundsMax).TransformBy(InstanceTransform);
		NewBounds.GetCenterAndExtents(BoundsLocation, BoundsExtent);
	}
}

FString FWorldPartitionActorDesc::ToString() const
{
	return FString::Printf(
		TEXT("Guid:%s BaseClass:%s NativeClass:%s Name:%s SpatiallyLoaded:%s Bounds:%s RuntimeGrid:%s EditorOnly:%s LevelBoundsRelevant:%s HLODRelevant:%s FolderPath:%s FolderGuid:%s Parent:%s"), 
		*Guid.ToString(), 
		*BaseClass.ToString(), 
		*NativeClass.ToString(), 
		*GetActorName().ToString(),
		bIsSpatiallyLoaded ? TEXT("true") : TEXT("false"),
		*GetBounds().ToString(),
		*RuntimeGrid.ToString(),
		bActorIsEditorOnly ? TEXT("true") : TEXT("false"),
		bLevelBoundsRelevant ? TEXT("true") : TEXT("false"),
		bActorIsHLODRelevant ? TEXT("true") : TEXT("false"),
		*FolderPath.ToString(),
		*FolderGuid.ToString(),
		*ParentActor.ToString()
	);
}

void FWorldPartitionActorDesc::Serialize(FArchive& Ar)
{
	check(Ar.IsPersistent());

	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteNCBranchObjectVersion::GUID);

	if (Ar.CustomVer(FFortniteNCBranchObjectVersion::GUID) >= FFortniteNCBranchObjectVersion::WorldPartitionActorDescNativeBaseClassSerialization)
	{
		Ar << BaseClass;
	}

	Ar << NativeClass << Guid;

	if(Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::LargeWorldCoordinates)
	{
		FVector3f BoundsLocationFlt, BoundsExtentFlt;
		Ar << BoundsLocationFlt << BoundsExtentFlt;
		BoundsLocation = FVector(BoundsLocationFlt);
		BoundsExtent = FVector(BoundsExtentFlt);
	}
	else
	{
		Ar << BoundsLocation << BoundsExtent;
	}
	
	if (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::ConvertedActorGridPlacementToSpatiallyLoadedFlag)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		EActorGridPlacement GridPlacement;
		Ar << (__underlying_type(EActorGridPlacement)&)GridPlacement;
		bIsSpatiallyLoaded = GridPlacement != EActorGridPlacement::AlwaysLoaded;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	else
	{
		Ar << bIsSpatiallyLoaded;
	}
		
	Ar << RuntimeGrid << bActorIsEditorOnly << bLevelBoundsRelevant;
	
	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeDataLayers)
	{
		TArray<FName> Deprecated_Layers;
		Ar << Deprecated_Layers;
	}

	Ar << References;

	if (Ar.CustomVer(FFortniteNCBranchObjectVersion::GUID) >= FFortniteNCBranchObjectVersion::WorldPartitionActorDescTagsSerialization)
	{
		Ar << Tags;
	}

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeArchivePersistent)
	{
		Ar << ActorPackage << ActorPath;
	}

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeDataLayers)
	{
		Ar << DataLayers;
	}

	if (Ar.CustomVer(FFortniteNCBranchObjectVersion::GUID) >= FFortniteNCBranchObjectVersion::WorldPartitionActorDescSerializeDataLayerAssets)
	{
		Ar << bIsUsingDataLayerAsset;
	}

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeActorLabel)
	{
		Ar << ActorLabel;
	}

	if ((Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeHLODInfo) ||
		(Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::WorldPartitionActorDescSerializeHLODInfo))
	{
		Ar << bActorIsHLODRelevant;
		Ar << HLODLayer;
	}
	else
	{
		bActorIsHLODRelevant = true;
		HLODLayer = FName();
	}

	if (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::WorldPartitionActorDescSerializeActorFolderPath)
	{
		Ar << FolderPath;
	}

	if (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::WorldPartitionActorDescSerializeAttachParent)
	{
		Ar << ParentActor;
	}

	if (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::AddLevelActorFolders)
	{
		Ar << FolderGuid;
	}
}

FBox FWorldPartitionActorDesc::GetBounds() const
{
	return FBox(BoundsLocation - BoundsExtent, BoundsLocation + BoundsExtent);
}

FName FWorldPartitionActorDesc::GetActorName() const
{
	return *FPaths::GetExtension(ActorPath.ToString());
}

FName FWorldPartitionActorDesc::GetActorLabelOrName() const
{
	return GetActorLabel().IsNone() ? GetActorName() : GetActorLabel();
}

FName FWorldPartitionActorDesc::GetDisplayClassName() const
{
	if (BaseClass.IsNone())
	{
		return NativeClass;
	}

	int32 Index;
	const FString BaseClassStr(BaseClass.ToString());
	if (BaseClassStr.FindLastChar(TCHAR('.'), Index))
	{
		FString CleanClassName = BaseClassStr.Mid(Index + 1);
		CleanClassName.RemoveFromEnd(TEXT("_C"));
		return *CleanClassName;
	}

	return BaseClass;
}

bool FWorldPartitionActorDesc::IsLoaded(bool bEvenIfPendingKill) const
{
	if (ActorPtr.IsExplicitlyNull())
	{
		ActorPtr = FindObject<AActor>(nullptr, *ActorPath.ToString());
	}

	return ActorPtr.IsValid(bEvenIfPendingKill);
}

AActor* FWorldPartitionActorDesc::GetActor(bool bEvenIfPendingKill, bool bEvenIfUnreachable) const
{
	if (ActorPtr.IsExplicitlyNull())
	{
		ActorPtr = FindObject<AActor>(nullptr, *ActorPath.ToString());
	}

	return bEvenIfUnreachable ? ActorPtr.GetEvenIfUnreachable() : ActorPtr.Get(bEvenIfPendingKill);
}

AActor* FWorldPartitionActorDesc::Load() const
{
	if (ActorPtr.IsExplicitlyNull())
	{
		// First, try to find the existing actor which could have been loaded by another actor (through standard serialization)
		ActorPtr = FindObject<AActor>(nullptr, *ActorPath.ToString());
	}

	// Then, if the actor isn't loaded, load it
	if (ActorPtr.IsExplicitlyNull())
	{
		const FLinkerInstancingContext* InstancingContext = nullptr;
		FSoftObjectPathFixupArchive* SoftObjectPathFixupArchive = nullptr;

		if (Container)
		{
			Container->GetInstancingContext(InstancingContext, SoftObjectPathFixupArchive);
		}

		UPackage* Package = nullptr;

		if (InstancingContext)
		{
			FName RemappedPackageName = InstancingContext->Remap(ActorPackage);
			check(RemappedPackageName != ActorPath);

			Package = CreatePackage(*RemappedPackageName.ToString());
		}

		Package = LoadPackage(Package, *ActorPackage.ToString(), LOAD_None, nullptr, InstancingContext);

		if (Package)
		{
			ActorPtr = FindObject<AActor>(nullptr, *ActorPath.ToString());
			if (AActor* Actor = ActorPtr.Get())
			{
				if (SoftObjectPathFixupArchive)
				{
					SoftObjectPathFixupArchive->Fixup(Actor);
				}
			}
			else
			{
				UE_LOG(LogWorldPartition, Warning, TEXT("Can't load actor guid `%s` ('%s') from package '%s'"), *Guid.ToString(), *GetActorName().ToString(), *ActorPackage.ToString());
			}
		}
	}

	return ActorPtr.Get();
}

void FWorldPartitionActorDesc::Unload()
{
	if (AActor* Actor = GetActor())
	{
		// At this point, it can happen that an actor isn't in an external package:
		//
		// PIE travel: 
		//		in this case, actors referenced by the world package (an example is the level script) will be duplicated as part of the PIE world duplication and will end up
		//		not being using an external package, which is fine because in that case they are considered as always loaded.
		//
		// FWorldPartitionCookPackageSplitter:
		//		should mark each FWorldPartitionActorDesc as moved, and the splitter should take responsbility for calling ClearFlags on every object in 
		//		the package when it does the move

		if (Actor->IsPackageExternal())
		{
			ForEachObjectWithPackage(Actor->GetPackage(), [](UObject* Object)
			{
				if (Object->HasAnyFlags(RF_Public | RF_Standalone))
				{
					CastChecked<UMetaData>(Object)->ClearFlags(RF_Public | RF_Standalone);
				}
				return true;
			}, false);
		}

		ActorPtr = nullptr;
	}
}

void FWorldPartitionActorDesc::RegisterActor()
{
	if (AActor* Actor = GetActor())
	{
		check(Container);
		Container->OnActorDescRegistered(*this);
	}
}

void FWorldPartitionActorDesc::UnregisterActor()
{
	if (AActor* Actor = GetActor())
	{
		check(Container);
		Container->OnActorDescUnregistered(*this);
	}
}
#endif