// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectRuntime.h"

const FSmartObjectClaimHandle FSmartObjectClaimHandle::InvalidHandle = {};
const FSmartObjectSlotRuntimeData FSmartObjectSlotRuntimeData::InvalidSlot = {};

//----------------------------------------------------------------------//
// FSmartObjectRuntime
//----------------------------------------------------------------------//
FSmartObjectRuntime::FSmartObjectRuntime(const USmartObjectDefinition& InDefinition)
	: Definition(&InDefinition)
	, SharedOctreeID(MakeShareable(new FSmartObjectOctreeID()))
{
}

uint32 FSmartObjectRuntime::FindFreeSlots(TBitArray<>& OutFreeSlots) const
{
	const int32 NumSlotDefinitions = GetDefinition().GetSlots().Num();

	// slots are considered free unless they are marked as being used in runtime slots
	OutFreeSlots.Init(/*Value=*/true, NumSlotDefinitions);
	uint32 TakenSlots = 0;

	// We may have less runtime slots than we have in the definition so we need to fetch the actual index from them.
	for (const FSmartObjectSlotRuntimeData& SlotRuntimeData : SlotsRuntimeData)
	{
		if (OutFreeSlots.IsValidIndex(SlotRuntimeData.SlotIndex) && SlotRuntimeData.State != ESmartObjectSlotState::Free)
		{
			++TakenSlots;
			OutFreeSlots[SlotRuntimeData.SlotIndex] = false;
		}
	}

	return NumSlotDefinitions - TakenSlots;
}

bool FSmartObjectRuntime::ClaimSlot(const FSmartObjectClaimHandle& ClaimHandle)
{
	if (!ensureMsgf(ClaimHandle.IsValid(), TEXT("Attempting to claim using an invalid handle: %s"), *ClaimHandle.Describe()))
	{
		return false;
	}

	FSmartObjectSlotRuntimeData* ExistingEntry = SlotsRuntimeData.FindByPredicate(
		[ClaimHandle](const FSmartObjectSlotRuntimeData& Entry){ return Entry.SlotIndex == ClaimHandle.SlotIndex; });

	FSmartObjectSlotRuntimeData& SlotRuntimeData = ExistingEntry != nullptr ? *ExistingEntry : SlotsRuntimeData.Add_GetRef(FSmartObjectSlotRuntimeData(ClaimHandle.SlotIndex));
	const bool bClaimed = SlotRuntimeData.Claim(ClaimHandle.UserID);

	return bClaimed;
}

bool FSmartObjectRuntime::ReleaseSlot(const FSmartObjectClaimHandle& ClaimHandle, const bool bAborted)
{
	bool bRemoved = false;

	// The slot index in the handle refers to the index in the definition so we need to fetch it from the runtime data.
	for (int32 EntryIndex = 0; EntryIndex < SlotsRuntimeData.Num(); ++EntryIndex)
	{
		FSmartObjectSlotRuntimeData& SlotRuntimeData = SlotsRuntimeData[EntryIndex];
		const ESmartObjectSlotState State = SlotRuntimeData.State;
		if (SlotRuntimeData.SlotIndex != ClaimHandle.SlotIndex)
		{
			continue;
		}

		if (State != ESmartObjectSlotState::Claimed && State != ESmartObjectSlotState::Occupied)
		{
			UE_LOG(LogSmartObject, Error, TEXT("Expected slot state is 'Claimed' or 'Occupied' but current state is '%s'. Slot will not be released"),
				*UEnum::GetValueAsString(SlotRuntimeData.State));
			break;
		}

		if (ClaimHandle.UserID != SlotRuntimeData.User)
		{
			UE_LOG(LogSmartObject, Error, TEXT("User '%s' is trying to release slot claimed or used by other user '%s'. Slot will not be released"),
				*ClaimHandle.UserID.Describe(), *SlotRuntimeData.User.Describe());
			break;
		}

		if (bAborted)
		{
			const bool bFunctionWasExecuted = SlotRuntimeData.OnSlotInvalidatedDelegate.ExecuteIfBound(ClaimHandle, SlotRuntimeData.State);
			UE_LOG(LogSmartObject, Verbose, TEXT("Slot invalidated callback was%scalled for slot %s"), bFunctionWasExecuted ? TEXT(" ") : TEXT(" not "), *SlotRuntimeData.Describe());
		}

		SlotsRuntimeData.RemoveAtSwap(EntryIndex, 1, /*bAllowShrinking=*/false);
		bRemoved = true;
		break;
	}

	return bRemoved;
}

bool FSmartObjectRuntime::UseSlot(const FSmartObjectClaimHandle& ClaimHandle)
{
	if (!ensureMsgf(ClaimHandle.IsValid(), TEXT("A valid claim handle is required to use a slot: %s"), *ClaimHandle.Describe()))
	{
		return false;
	}

	// The slot index in the handle refers to the index in the definition so we need to fetch it from the runtime data.
	for (FSmartObjectSlotRuntimeData& SlotRuntimeData : SlotsRuntimeData)
	{
		if (SlotRuntimeData.SlotIndex == ClaimHandle.SlotIndex)
		{
			ensureMsgf(SlotRuntimeData.User == ClaimHandle.UserID, TEXT("Attempt to use slot %s from handle %s but already assigned to %s")
				, *SlotRuntimeData.Describe(), *ClaimHandle.Describe(), *SlotRuntimeData.User.Describe());

			SlotRuntimeData.State = ESmartObjectSlotState::Occupied;
			return true;
		}
	}

	ensureMsgf(false, TEXT("Should have been claimed first: %s"), *ClaimHandle.Describe());
	return false;
}

//----------------------------------------------------------------------//
// FSmartObjectSlotRuntimeData
//----------------------------------------------------------------------//
bool FSmartObjectSlotRuntimeData::Claim(const FSmartObjectUserID& InUser)
{
	if (State == ESmartObjectSlotState::Free)
	{
		State = ESmartObjectSlotState::Claimed;
		User = InUser;
		return true;
	}
	return false;
}
