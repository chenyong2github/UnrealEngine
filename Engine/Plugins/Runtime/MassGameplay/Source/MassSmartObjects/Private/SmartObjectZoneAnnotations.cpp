// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectZoneAnnotations.h"
#include "MassSmartObjectSettings.h"
#include "SmartObjectCollection.h"
#include "SmartObjectComponent.h"
#include "SmartObjectSubsystem.h"
#include "ZoneGraphAnnotationSubsystem.h"
#include "ZoneGraphAnnotationTypes.h"
#include "ZoneGraphDelegates.h"
#include "ZoneGraphQuery.h"
#include "ZoneGraphSubsystem.h"
#include "GameFramework/Character.h"
#include "VisualLogger/VisualLogger.h"

void USmartObjectZoneAnnotations::PostSubsystemsInitialized()
{
	SmartObjectSubsystem = UWorld::GetSubsystem<USmartObjectSubsystem>(GetWorld());

#if WITH_EDITOR
	// Monitor collection changes to rebuild our lookup data
	if (SmartObjectSubsystem != nullptr)
	{
		OnMainCollectionChangedHandle = SmartObjectSubsystem->OnMainCollectionChanged.AddLambda([this]()
		{
			const UWorld* World = GetWorld();
			if (World != nullptr && !World->IsGameWorld())
			{
				RebuildForAllGraphs();
			}
		});
	}

	const UMassSmartObjectSettings* MassSmartObjectSettings = GetDefault<UMassSmartObjectSettings>();
	BehaviorTag = MassSmartObjectSettings->SmartObjectTag;

	// Track density settings changes
	OnAnnotationSettingsChangedHandle = MassSmartObjectSettings->OnAnnotationSettingsChanged.AddLambda([this]()
	{
		BehaviorTag = GetDefault<UMassSmartObjectSettings>()->SmartObjectTag;
	});

	// Monitor zone graph changes to rebuild our lookup data
	OnGraphDataChangedHandle = UE::ZoneGraphDelegates::OnZoneGraphDataBuildDone.AddLambda([this](const FZoneGraphBuildData& BuildData)
	{
		RebuildForAllGraphs();
	});
#endif

	// Update our cached members before calling base class since it might call
	// PostZoneGraphDataAdded and we need to be all set.
	Super::PostSubsystemsInitialized();
}

void USmartObjectZoneAnnotations::PostZoneGraphDataAdded(const AZoneGraphData& ZoneGraphData)
{
	const FZoneGraphStorage& Storage = ZoneGraphData.GetStorage();
	const int32 Index = Storage.DataHandle.Index;

#if WITH_EDITOR
	if (Index >= SmartObjectAnnotationDataArray.Num())
	{
		SmartObjectAnnotationDataArray.SetNum(Index + 1);
	}
#endif

	checkf(SmartObjectAnnotationDataArray.IsValidIndex(Index), TEXT("In Editor should always resize when necessary and runtime should always have valid precomputed data."));
	FSmartObjectAnnotationData& Data = SmartObjectAnnotationDataArray[Index];
	if (!Data.IsValid())
	{
		Data.DataHandle = Storage.DataHandle;
	}

#if WITH_EDITOR
	// We don't rebuild for runtime world, we use precomputed data
	if (!ZoneGraphData.GetWorld()->IsGameWorld())
	{
		RebuildForSingleGraph(Data, Storage);
	}
#endif // WITH_EDITOR

	Data.bInitialTaggingCompleted = false;
}

void USmartObjectZoneAnnotations::PreZoneGraphDataRemoved(const AZoneGraphData& ZoneGraphData)
{
	const FZoneGraphStorage& Storage = ZoneGraphData.GetStorage();
	const int32 Index = Storage.DataHandle.Index;

	if (!SmartObjectAnnotationDataArray.IsValidIndex(Index))
	{
		return;
	}

	FSmartObjectAnnotationData& Data = SmartObjectAnnotationDataArray[Index];

	// We use precomputed data for runtime so we only mark it as not longer used
	if (ZoneGraphData.GetWorld()->IsGameWorld())
	{
		Data.DataHandle = {};
	}
	else
	{
		Data.Reset();
	}
}

FZoneGraphTagMask USmartObjectZoneAnnotations::GetAnnotationTags() const
{
	return FZoneGraphTagMask(BehaviorTag);
}

const FSmartObjectAnnotationData* USmartObjectZoneAnnotations::GetAnnotationData(const FZoneGraphDataHandle DataHandle) const
{
	const int32 Index = DataHandle.Index;
	if (!SmartObjectAnnotationDataArray.IsValidIndex(Index))
	{
		return nullptr;
	}

	return &SmartObjectAnnotationDataArray[Index];
}

void USmartObjectZoneAnnotations::TickAnnotation(const float DeltaTime, FZoneGraphAnnotationTagContainer& BehaviorTagContainer)
{
	if (!BehaviorTag.IsValid())
	{
		return;
	}

	for (FSmartObjectAnnotationData& Data : SmartObjectAnnotationDataArray)
	{
		if (Data.bInitialTaggingCompleted || !Data.IsValid() || Data.ObjectToEntryPointLookup.IsEmpty())
		{
			continue;
		}

		// Apply tags
		TArrayView<FZoneGraphTagMask> LaneTags = BehaviorTagContainer.GetMutableAnnotationTagsForData(Data.DataHandle);
		for (const int32 LaneIndex : Data.AffectedLanes)
		{
			LaneTags[LaneIndex].Add(BehaviorTag);
		}

		Data.bInitialTaggingCompleted = true;
	}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	MarkRenderStateDirty();
#endif
}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
void USmartObjectZoneAnnotations::DebugDraw(FZoneGraphAnnotationSceneProxy* DebugProxy)
{
	UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	if (ZoneGraph == nullptr)
	{
		return;
	}

	for (FSmartObjectAnnotationData& Data : SmartObjectAnnotationDataArray)
	{
		const FZoneGraphStorage* ZoneStorage = Data.DataHandle.IsValid() ? ZoneGraph->GetZoneGraphStorage(Data.DataHandle) : nullptr;
		if (ZoneStorage == nullptr)
		{
			continue;
		}

		const ASmartObjectCollection* Collection = SmartObjectSubsystem->GetMainCollection();
		if (Collection == nullptr)
		{
			return;
		}

		for (const FSmartObjectCollectionEntry& Entry : Collection->GetEntries())
		{
			FSmartObjectHandle Handle = Entry.GetHandle();
			const FSmartObjectLaneLocation* SOLaneLocation = Data.ObjectToEntryPointLookup.Find(Handle);
			if (SOLaneLocation == nullptr)
			{
				continue;
			}

			const FVector& ObjectLocation = Entry.GetComponent()->GetComponentLocation();
			FZoneGraphLaneLocation EntryPointLocation;
			UE::ZoneGraph::Query::CalculateLocationAlongLane(*ZoneStorage, SOLaneLocation->LaneIndex, SOLaneLocation->DistanceAlongLane, EntryPointLocation);
			const FColor Color = FColor::Silver;
			constexpr float SphereRadius = 25.f;
			DebugProxy->Spheres.Emplace(SphereRadius, EntryPointLocation.Position, Color);
			DebugProxy->Spheres.Emplace(SphereRadius, ObjectLocation, Color);
			DebugProxy->DashedLines.Emplace(ObjectLocation, EntryPointLocation.Position, Color, /*dash size*/10.f);
		}
	}
}
#endif // !UE_BUILD_SHIPPING && !UE_BUILD_TEST

#if WITH_EDITOR
void USmartObjectZoneAnnotations::OnUnregister()
{
	GetDefault<UMassSmartObjectSettings>()->OnAnnotationSettingsChanged.Remove(OnAnnotationSettingsChangedHandle);
	OnAnnotationSettingsChangedHandle.Reset();

	UE::ZoneGraphDelegates::OnZoneGraphDataBuildDone.Remove(OnGraphDataChangedHandle);
	OnGraphDataChangedHandle.Reset();

	Super::OnUnregister();
}

void USmartObjectZoneAnnotations::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	FProperty* MemberProperty = nullptr;
	if (PropertyChangedEvent.PropertyChain.GetActiveMemberNode())
	{
		MemberProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	}

	if (MemberProperty && Property)
	{
		if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(USmartObjectZoneAnnotations, AffectedLaneTags))
		{
			RebuildForAllGraphs();
		}
	}
}

void USmartObjectZoneAnnotations::RebuildForSingleGraph(FSmartObjectAnnotationData& Data, const FZoneGraphStorage& Storage)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("ZoneGraphSmartObjectBehavior RebuildData")

	if (SmartObjectSubsystem == nullptr)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("Attempting to rebuild data while SmartObjectSubsystem is not set. This indicates a problem in the initialization flow."));
		return;
	}

	if (!BehaviorTag.IsValid())
	{
		UE_VLOG_UELOG(this, LogSmartObject, Warning, TEXT("Attempting to rebuild data while BehaviorTag is invalid (e.g. not set in MassSmartObjectSettings)"));
		return;
	}

	const ASmartObjectCollection* Collection = SmartObjectSubsystem->GetMainCollection();
	if (Collection == nullptr)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Attempting to rebuild data while main SmartObject collection is not set."));
		return;
	}

	const FVector SearchExtent(GetDefault<UMassSmartObjectSettings>()->SearchExtents);
	int32 NumAdded = 0;
	int32 NumDiscarded = 0;

	const int32 NumSO = Collection->GetEntries().Num();
	const int32 NumLanes = Storage.Lanes.Num();
	Data.ObjectToEntryPointLookup.Empty(NumSO);
	Data.LaneToSmartObjectsLookup.Empty(NumLanes);
	Data.AffectedLanes.Empty(NumLanes);

	for (const FSmartObjectCollectionEntry& Entry : Collection->GetEntries())
	{
		FZoneGraphLaneLocation EntryPointLocation;
		FSmartObjectHandle Handle = Entry.GetHandle();
		const FVector& ObjectLocation = Entry.GetComponent()->GetComponentLocation();

		const FBox QueryBounds(ObjectLocation - SearchExtent, ObjectLocation + SearchExtent);

		float DistanceSqr = 0.f;
		const bool bFound = UE::ZoneGraph::Query::FindNearestLane(Storage, QueryBounds, AffectedLaneTags, EntryPointLocation, DistanceSqr);
		if (bFound)
		{
			NumAdded++;

			const int32 LaneIndex = EntryPointLocation.LaneHandle.Index;

			Data.ObjectToEntryPointLookup.Add(Handle, { LaneIndex, EntryPointLocation.DistanceAlongLane });
			UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Adding ZG annotation for SmartObject '%s' on lane '%s'"), *LexToString(Handle), *EntryPointLocation.LaneHandle.ToString());

			auto AddLookupEntries = [this, &Data, Handle, &ObjectLocation](const int32 InLaneIndex, const FVector& LocationOnLane)
			{
				Data.AffectedLanes.AddUnique(InLaneIndex);
				Data.LaneToSmartObjectsLookup.FindOrAdd(InLaneIndex).SmartObjects.Add(Handle);

				UE_VLOG_SEGMENT(this, LogZoneGraphAnnotations, Display, ObjectLocation, LocationOnLane, FColor::Green, TEXT(""));
			};

			AddLookupEntries(LaneIndex, EntryPointLocation.Position);
			UE_VLOG_LOCATION(this, LogZoneGraphAnnotations, Display, ObjectLocation, 50.f /*radius*/, FColor::Green, TEXT("%s"), *LexToString(Handle));
		}
		else
		{
			NumDiscarded++;
			UE_VLOG_LOCATION(this, LogZoneGraphAnnotations, Display, ObjectLocation, 75.f /*radius*/, FColor::Red, TEXT("%s"), *LexToString(Handle));
		}
	}

	Data.ObjectToEntryPointLookup.Shrink();
	Data.AffectedLanes.Shrink();
	Data.LaneToSmartObjectsLookup.Shrink();
	Data.bInitialTaggingCompleted = false;

	UE_VLOG_UELOG(this, LogZoneGraphAnnotations, Log, TEXT("Summary: %d entry points added, %d discarded%s."), NumAdded, NumDiscarded, NumDiscarded == 0 ? TEXT("") : TEXT(" (too far from any lane)"));
}

void USmartObjectZoneAnnotations::RebuildForAllGraphs()
{
	UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	if (!ZoneGraphSubsystem)
	{
		return;
	}

	for (const FRegisteredZoneGraphData& RegisteredZoneGraphData : ZoneGraphSubsystem->GetRegisteredZoneGraphData())
	{
		if (!RegisteredZoneGraphData.bInUse || RegisteredZoneGraphData.ZoneGraphData == nullptr)
		{
			continue;
		}

		const FZoneGraphStorage& Storage = RegisteredZoneGraphData.ZoneGraphData->GetStorage();
		const int32 Index = Storage.DataHandle.Index;

		if (SmartObjectAnnotationDataArray.IsValidIndex(Index))
		{
			FSmartObjectAnnotationData& AnnotationData = SmartObjectAnnotationDataArray[Index];
			AnnotationData.Reset();
			RebuildForSingleGraph(AnnotationData, Storage);
		}
	}
}
#endif // WITH_EDITOR
