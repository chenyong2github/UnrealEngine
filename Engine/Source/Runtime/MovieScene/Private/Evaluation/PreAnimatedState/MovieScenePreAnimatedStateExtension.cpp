// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateExtension.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedEntityCaptureSource.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedCaptureSources.h"
#include "Evaluation/PreAnimatedState/MovieSceneRestoreStateParams.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneComponentPtr.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "Algo/Find.h"
#include "Algo/IndexOf.h"
#include "Algo/BinarySearch.h"

namespace UE
{
namespace MovieScene
{

TEntitySystemLinkerExtensionID<FPreAnimatedStateExtension> FPreAnimatedStateExtension::GetExtensionID()
{
	static TEntitySystemLinkerExtensionID<FPreAnimatedStateExtension> ID = UMovieSceneEntitySystemLinker::RegisterExtension<FPreAnimatedStateExtension>();
	return ID;
}


FPreAnimatedStateExtension::FPreAnimatedStateExtension(UMovieSceneEntitySystemLinker* InLinker)
	: TSharedEntitySystemLinkerExtension<FPreAnimatedStateExtension>(InLinker)
	, NumRequestsForGlobalState(0)
	, bEntriesInvalidated(false)
{}

FPreAnimatedStateExtension::~FPreAnimatedStateExtension()
{}

FPreAnimatedStorageID FPreAnimatedStateExtension::RegisterStorageInternal()
{
	static uint32 NextID = 0;
	return FPreAnimatedStorageID(++NextID);
}

FPreAnimatedStorageGroupHandle FPreAnimatedStateExtension::AllocateGroup(TSharedPtr<IPreAnimatedStateGroupManager> GroupManager)
{
	FPreAnimatedGroupMetaData NewEntry;
	NewEntry.GroupManagerPtr = GroupManager;

	const int32 NewIndex = GroupMetaData.Add(MoveTemp(NewEntry));
	return FPreAnimatedStorageGroupHandle{NewIndex};
}

void FPreAnimatedStateExtension::FreeGroup(FPreAnimatedStorageGroupHandle Handle)
{
	check(Handle);
	ensure(GroupMetaData[Handle.Value].AggregateMetaData.Num() == 0);

	FreeGroupInternal(Handle);
}

void FPreAnimatedStateExtension::FreeGroupInternal(FPreAnimatedStorageGroupHandle Handle)
{
	FPreAnimatedGroupMetaData& Group = GroupMetaData[Handle.Value];
	Group.GroupManagerPtr->OnGroupDestroyed(Handle);
	GroupMetaData.RemoveAt(Handle.Value);
}

void FPreAnimatedStateExtension::ReplaceObjectForGroup(FPreAnimatedStorageGroupHandle GroupHandle, const FObjectKey& OldObject, const FObjectKey& NewObject)
{
	FPreAnimatedGroupMetaData& Group = GroupMetaData[GroupHandle.Value];

	for (FAggregatePreAnimatedStateMetaData& MetaData : Group.AggregateMetaData)
	{
		TSharedPtr<IPreAnimatedStorage> Storage = GetStorageChecked(MetaData.ValueHandle.TypeID);
		Storage->OnObjectReplaced(MetaData.ValueHandle.StorageIndex, OldObject, NewObject);
	}
}

EPreAnimatedStorageRequirement FPreAnimatedStateExtension::GetStorageRequirement(const FPreAnimatedStateEntry& Entry) const
{
	const FPreAnimatedGroupMetaData&          Group     = GroupMetaData[Entry.GroupHandle.Value];
	const FAggregatePreAnimatedStateMetaData* Aggregate = Algo::FindBy(Group.AggregateMetaData, Entry.ValueHandle, &FAggregatePreAnimatedStateMetaData::ValueHandle);

	if (ensure(Aggregate))
	{
		if (Aggregate->NumRestoreContributors != 0)
		{
			return EPreAnimatedStorageRequirement::Transient;
		}
		return EPreAnimatedStorageRequirement::Persistent;
	}
	return EPreAnimatedStorageRequirement::None;
}

void FPreAnimatedStateExtension::EnsureMetaData(const FPreAnimatedStateEntry& Entry)
{
	FPreAnimatedGroupMetaData&          Group     = GroupMetaData[Entry.GroupHandle.Value];
	FAggregatePreAnimatedStateMetaData* Aggregate = Algo::FindBy(Group.AggregateMetaData, Entry.ValueHandle, &FAggregatePreAnimatedStateMetaData::ValueHandle);
	if (!Aggregate)
	{
		Aggregate = &Group.AggregateMetaData.Emplace_GetRef(Entry.ValueHandle);
	}
}

void FPreAnimatedStateExtension::AddMetaData(const FPreAnimatedStateMetaData& MetaData)
{
	FPreAnimatedGroupMetaData&          Group     = GroupMetaData[MetaData.Entry.GroupHandle.Value];
	FAggregatePreAnimatedStateMetaData* Aggregate = Algo::FindBy(Group.AggregateMetaData, MetaData.Entry.ValueHandle, &FAggregatePreAnimatedStateMetaData::ValueHandle);
	if (!Aggregate)
	{
		Aggregate = &Group.AggregateMetaData.Emplace_GetRef(MetaData.Entry.ValueHandle);
	}

	++Aggregate->NumContributors;
	if (MetaData.bWantsRestoreState)
	{
		++Aggregate->NumRestoreContributors;
		Aggregate->bWantedRestore = true;
	}
}

void FPreAnimatedStateExtension::RemoveMetaData(const FPreAnimatedStateMetaData& MetaData)
{
	FPreAnimatedGroupMetaData& Group          = GroupMetaData[MetaData.Entry.GroupHandle.Value];
	const int32                AggregateIndex = Algo::IndexOfBy(Group.AggregateMetaData, MetaData.Entry.ValueHandle, &FAggregatePreAnimatedStateMetaData::ValueHandle);

	if (!ensure(AggregateIndex != INDEX_NONE))
	{
		return;
	}

	FAggregatePreAnimatedStateMetaData& Aggregate = Group.AggregateMetaData[AggregateIndex];

	const int32 TotalNum = --Aggregate.NumContributors;
	if (MetaData.bWantsRestoreState)
	{
		if (--Aggregate.NumRestoreContributors == 0)
		{
			EPreAnimatedStorageRequirement NewRequirement = TotalNum != 0
				? EPreAnimatedStorageRequirement::Persistent
				: EPreAnimatedStorageRequirement::None;

			TSharedPtr<IPreAnimatedStorage> Storage = GetStorageChecked(MetaData.Entry.ValueHandle.TypeID);

			FRestoreStateParams Params = {WeakLinker.Get(), MetaData.RootInstanceHandle};
			NewRequirement = Storage->RestorePreAnimatedStateStorage(MetaData.Entry.ValueHandle.StorageIndex, EPreAnimatedStorageRequirement::Transient, NewRequirement, Params);

			if (NewRequirement == EPreAnimatedStorageRequirement::None)
			{
				if (Group.AggregateMetaData.Num() == 1)
				{
					// If the group is going to be empty - just remove it all
					FreeGroupInternal(MetaData.Entry.GroupHandle.Value);
				}
				else
				{
					// Otherwise remove just this aggregate
					Group.AggregateMetaData.RemoveAt(AggregateIndex, 1, false);
				}

				return;
			}
		}
	}

	if (TotalNum == 0)
	{
		Aggregate.bWantedRestore = false;
		Aggregate.TerminalInstanceHandle = MetaData.RootInstanceHandle;
	}
}

void FPreAnimatedStateExtension::UpdateMetaData(const FPreAnimatedStateMetaData& MetaData)
{
	FPreAnimatedGroupMetaData&          Group     = GroupMetaData[MetaData.Entry.GroupHandle.Value];
	FAggregatePreAnimatedStateMetaData* Aggregate = Algo::FindBy(Group.AggregateMetaData, MetaData.Entry.ValueHandle, &FAggregatePreAnimatedStateMetaData::ValueHandle);

	if (ensure(Aggregate))
	{
		if (MetaData.bWantsRestoreState)
		{
			++Aggregate->NumRestoreContributors;
			Aggregate->bWantedRestore = true;
		}
		else
		{
			--Aggregate->NumRestoreContributors;
		}
	}
}

FPreAnimatedEntityCaptureSource* FPreAnimatedStateExtension::GetEntityMetaData() const
{
	return EntityCaptureSource.Get();
}

FPreAnimatedEntityCaptureSource* FPreAnimatedStateExtension::GetOrCreateEntityMetaData()
{
	if (!EntityCaptureSource)
	{
		EntityCaptureSource = MakeUnique<FPreAnimatedEntityCaptureSource>(this);
	}
	return EntityCaptureSource.Get();
}

FPreAnimatedTrackInstanceCaptureSources* FPreAnimatedStateExtension::GetTrackInstanceMetaData() const
{
	return TrackInstanceCaptureSource.Get();
}

FPreAnimatedTrackInstanceCaptureSources* FPreAnimatedStateExtension::GetOrCreateTrackInstanceMetaData()
{
	if (!TrackInstanceCaptureSource)
	{
		TrackInstanceCaptureSource = MakeUnique<FPreAnimatedTrackInstanceCaptureSources>(this);
	}
	return TrackInstanceCaptureSource.Get();
}

void FPreAnimatedStateExtension::AddWeakCaptureSource(TWeakPtr<IPreAnimatedCaptureSource> InWeakCaptureSource)
{
	WeakExternalCaptureSources.Add(InWeakCaptureSource);
}

void FPreAnimatedStateExtension::RemoveWeakCaptureSource(TWeakPtr<IPreAnimatedCaptureSource> InWeakCaptureSource)
{
	WeakExternalCaptureSources.Remove(InWeakCaptureSource);
}

void FPreAnimatedStateExtension::RestoreStateForGroup(FPreAnimatedStorageGroupHandle GroupHandle, const FRestoreStateParams& Params)
{
	FPreAnimatedGroupMetaData& Group = GroupMetaData[GroupHandle.Value];

	// Ensure that the entries are restored in strictly the reverse order they were cached in
	for (int32 AggregateIndex = Group.AggregateMetaData.Num()-1; AggregateIndex >= 0; --AggregateIndex)
	{
		FAggregatePreAnimatedStateMetaData& Aggregate = Group.AggregateMetaData[AggregateIndex];

		TSharedPtr<IPreAnimatedStorage> Storage = GetStorageChecked(Aggregate.ValueHandle.TypeID);
		Storage->RestorePreAnimatedStateStorage(Aggregate.ValueHandle.StorageIndex, EPreAnimatedStorageRequirement::Persistent, EPreAnimatedStorageRequirement::NoChange, Params);
	}
}

void FPreAnimatedStateExtension::RestoreGlobalState(const FRestoreStateParams& Params)
{
	TArray<FPreAnimatedStateMetaData> ExpiredMetaData;

	if (FPreAnimatedEntityCaptureSource* EntityMetaData = GetEntityMetaData())
	{
		EntityMetaData->GatherAndRemoveExpiredMetaData(Params, ExpiredMetaData);
	}

	for (int32 Index = WeakExternalCaptureSources.Num()-1; Index >= 0; --Index)
	{
		TSharedPtr<IPreAnimatedCaptureSource> MetaData = WeakExternalCaptureSources[Index].Pin();
		if (MetaData)
		{
			MetaData->GatherAndRemoveExpiredMetaData(Params, ExpiredMetaData);
		}
		else
		{
			// Order is not important in this array, so we can use the more efficient RemoveAtSwap algorithm
			WeakExternalCaptureSources.RemoveAtSwap(Index, 1);
		}
	}

	// Remove all contributions
	for (const FPreAnimatedStateMetaData& MetaData : ExpiredMetaData)
	{
		FPreAnimatedGroupMetaData&          Group     = GroupMetaData[MetaData.Entry.GroupHandle.Value];
		FAggregatePreAnimatedStateMetaData* Aggregate = Algo::FindBy(Group.AggregateMetaData, MetaData.Entry.ValueHandle, &FAggregatePreAnimatedStateMetaData::ValueHandle);
		if (ensure(Aggregate))
		{
			const int32 TotalNum = --Aggregate->NumContributors;
			if (MetaData.bWantsRestoreState)
			{
				--Aggregate->NumRestoreContributors;
			}

			if (TotalNum == 0)
			{
				Aggregate->bWantedRestore = false;
				Aggregate->TerminalInstanceHandle = MetaData.RootInstanceHandle;
			}
		}
	}

	// Ensure that the entries are restored in strictly the reverse order they were cached in
	for (int32 Index = 0; Index < GroupMetaData.GetMaxIndex(); ++Index)
	{
		if (!GroupMetaData.IsAllocated(Index))
		{
			continue;
		}

		FPreAnimatedGroupMetaData& Group = GroupMetaData[Index];

		for (int32 AggregateIndex = Group.AggregateMetaData.Num()-1; AggregateIndex >= 0; --AggregateIndex)
		{
			FAggregatePreAnimatedStateMetaData& Aggregate = Group.AggregateMetaData[AggregateIndex];
			if (Aggregate.NumContributors == 0 && (!Aggregate.TerminalInstanceHandle.IsValid() || Aggregate.TerminalInstanceHandle == Params.TerminalInstanceHandle))
			{
				TSharedPtr<IPreAnimatedStorage> Storage = GetStorageChecked(Aggregate.ValueHandle.TypeID);
				Storage->RestorePreAnimatedStateStorage(Aggregate.ValueHandle.StorageIndex, EPreAnimatedStorageRequirement::Persistent, EPreAnimatedStorageRequirement::None, Params);

				Group.AggregateMetaData.RemoveAt(AggregateIndex, 1, false);
			}

			if (Group.AggregateMetaData.Num() == 0)
			{
				// Remove at will not re-allocate the array or shuffle items within the sparse array, so this is safe
				Group.GroupManagerPtr->OnGroupDestroyed(Index);
				GroupMetaData.RemoveAt(Index);
			}
		}
	}

	GroupMetaData.Shrink();

	bEntriesInvalidated = true;
}

void FPreAnimatedStateExtension::DiscardTransientState()
{
	if (FPreAnimatedEntityCaptureSource* EntityMetaData = GetEntityMetaData())
	{
		EntityMetaData->Reset();
	}

	for (int32 Index = WeakExternalCaptureSources.Num()-1; Index >= 0; --Index)
	{
		if (TSharedPtr<IPreAnimatedCaptureSource> MetaData = WeakExternalCaptureSources[Index].Pin())
		{
			MetaData->Reset();
		}
	}

	// Remove all contributions, whilst keeping the ledger of their entries within the storage
	for (FPreAnimatedGroupMetaData& Group : GroupMetaData)
	{
		for (FAggregatePreAnimatedStateMetaData& Aggregate : Group.AggregateMetaData)
		{
			Aggregate = FAggregatePreAnimatedStateMetaData(Aggregate.ValueHandle);

			TSharedPtr<IPreAnimatedStorage> Storage = GetStorageChecked(Aggregate.ValueHandle.TypeID);
			Storage->DiscardPreAnimatedStateStorage(Aggregate.ValueHandle.StorageIndex, EPreAnimatedStorageRequirement::Transient);
		}
	}

	bEntriesInvalidated = true;
}

void FPreAnimatedStateExtension::DiscardStateForGroup(FPreAnimatedStorageGroupHandle GroupHandle)
{
	TArray<FPreAnimatedStateMetaData> MetaDataToRemove;

	if (FPreAnimatedEntityCaptureSource* EntityMetaData = GetEntityMetaData())
	{
		EntityMetaData->GatherAndRemoveMetaDataForGroup(GroupHandle, MetaDataToRemove);
	}

	for (int32 Index = WeakExternalCaptureSources.Num()-1; Index >= 0; --Index)
	{
		if (TSharedPtr<IPreAnimatedCaptureSource> MetaData = WeakExternalCaptureSources[Index].Pin())
		{
			MetaData->GatherAndRemoveMetaDataForGroup(GroupHandle, MetaDataToRemove);
		}
	}

	// Throw away the meta data and reset any aggregates
	FPreAnimatedGroupMetaData& Group = GroupMetaData[GroupHandle.Value];

	for (FAggregatePreAnimatedStateMetaData& Aggregate : Group.AggregateMetaData)
	{
		TSharedPtr<IPreAnimatedStorage> Storage = GetStorageChecked(Aggregate.ValueHandle.TypeID);
		Storage->DiscardPreAnimatedStateStorage(Aggregate.ValueHandle.StorageIndex, EPreAnimatedStorageRequirement::Persistent);
	}

	Group.GroupManagerPtr->OnGroupDestroyed(GroupHandle.Value);
	GroupMetaData.RemoveAt(GroupHandle.Value, 1);

	bEntriesInvalidated = true;
}

bool FPreAnimatedStateExtension::ContainsAnyStateForInstanceHandle(FInstanceHandle RootInstanceHandle) const
{
	if (FPreAnimatedEntityCaptureSource* EntityMetaData = GetEntityMetaData())
	{
		if (EntityMetaData->ContainsInstanceHandle(RootInstanceHandle))
		{
			return true;
		}
	}

	for (int32 Index = WeakExternalCaptureSources.Num()-1; Index >= 0; --Index)
	{
		TSharedPtr<IPreAnimatedCaptureSource> MetaData = WeakExternalCaptureSources[Index].Pin();
		if (MetaData && MetaData->ContainsInstanceHandle(RootInstanceHandle))
		{
			return true;
		}
	}

	return false;
}

} // namespace MovieScene
} // namespace UE
