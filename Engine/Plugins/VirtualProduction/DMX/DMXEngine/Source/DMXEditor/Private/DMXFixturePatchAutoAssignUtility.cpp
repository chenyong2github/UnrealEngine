// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXFixturePatchAutoAssignUtility.h"

#include "DMXProtocolConstants.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"

#include "ScopedTransaction.h"
#include "Math/Range.h"

#define LOCTEXT_NAMESPACE "DMXFixturePatchAutoAssignUtility"


namespace UE::DMX::DMXFixturePatchAutoAssignUtility::Private
{
	/** Returns true if two patches form a stack (same absolute address, same fixture type, same mode) */
	bool DoPatchesFormStack(UDMXEntityFixturePatch* First, UDMXEntityFixturePatch* Second)
	{
		if (!First || !Second)
		{
			return false;
		}

		uint64 FirstAbsoluteStartingChannel = ((uint64)First->GetUniverseID() * DMX_UNIVERSE_SIZE) + First->GetStartingChannel();
		uint64 SecondAbsoluteStartingChannel = ((uint64)Second->GetUniverseID() * DMX_UNIVERSE_SIZE) + Second->GetStartingChannel();
		if (FirstAbsoluteStartingChannel != SecondAbsoluteStartingChannel)
		{
			return false;
		}

		if (First->GetFixtureType() != Second->GetFixtureType())
		{
			return false;
		}

		if (First->GetActiveModeIndex() != Second->GetActiveModeIndex())
		{
			return false;
		}

		return true;
	}

	/** Builds arrays of stacked patches. A stack are patches of same fixture type, mode and absolute starting channel. */
	TArray<TArray<UDMXEntityFixturePatch*>> BuildPatchStacks(TArray<UDMXEntityFixturePatch*> FixturePatches)
	{
		TArray<TArray<UDMXEntityFixturePatch*>> PatchStacks;

		Algo::SortBy(FixturePatches, [](const UDMXEntityFixturePatch* FixturePatch)
			{
				return ((uint64)FixturePatch->GetUniverseID() * DMX_UNIVERSE_SIZE) + FixturePatch->GetStartingChannel();
			});

		TArray<UDMXEntityFixturePatch*> Stack;
		for (UDMXEntityFixturePatch* Patch : FixturePatches)
		{
			if (!Stack.IsEmpty() && !DoPatchesFormStack(Stack[0], Patch))
			{
				PatchStacks.Add(Stack);
				Stack.Reset();
			}
			
			Stack.Add(Patch);
		}

		// Add the last stack of patches
		PatchStacks.Add(Stack);
		
		return PatchStacks;
	}

	/** Finds patches in the DMX Library that are not contained in the FixturePatches array. Returns an ordered array (by addresses). */
	TArray<UDMXEntityFixturePatch*> FindOtherPatchesInLibrary(UDMXLibrary* DMXLibrary, const TArray<UDMXEntityFixturePatch*>& FixturePatches)
	{
		check(DMXLibrary);
		TArray<UDMXEntityFixturePatch*> Result = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
		Result.RemoveAll([FixturePatches](const UDMXEntityFixturePatch* FixturePatch)
			{
				return
					!FixturePatch ||
					!FixturePatch->GetFixtureType() ||
					FixturePatches.Contains(FixturePatch);
			});


		Algo::SortBy(Result, [](UDMXEntityFixturePatch* FixturePatch)
			{
				return ((uint64)FixturePatch->GetUniverseID() * DMX_UNIVERSE_SIZE) + FixturePatch->GetStartingChannel();
			});

		return Result;
	}
}

void FDMXFixturePatchAutoAssignUtility::AutoAssign(const TArray<UDMXEntityFixturePatch*>& FixturePatches, EDMXFixturePatchAutoAssignMode AutoAssignMode)
{
	FDMXFixturePatchAutoAssignUtility(FixturePatches, AutoAssignMode);
}

FDMXFixturePatchAutoAssignUtility::FDMXFixturePatchAutoAssignUtility(const TArray<UDMXEntityFixturePatch*>& FixturePatches, EDMXFixturePatchAutoAssignMode AutoAssignMode)
{
	if (FixturePatches.IsEmpty())
	{
		return;
	}

	// Ensure all patches reside in the same library
	DMXLibrary = FixturePatches[0]->GetParentLibrary();
	if (!ensureMsgf(DMXLibrary, TEXT("Trying to auto assign fixture patches but the DMX Library of its patches is not valid.")))
	{
		return;
	}
	check(DMXLibrary); // Assume a valid library for this operation from hereon

	for (const UDMXEntityFixturePatch* FixturePatch : FixturePatches)
	{
		if (!ensureMsgf(FixturePatch->GetParentLibrary() == DMXLibrary, TEXT("Trying to auto assign fixture patches, but the patches don't share a common DMX Library. This is not supported")))
		{
			return;
		}
	}

	// Initialize
	using namespace UE::DMX::DMXFixturePatchAutoAssignUtility::Private;
	OtherPatches = FindOtherPatchesInLibrary(DMXLibrary, FixturePatches);
	AutoAssignPatchStacks = BuildPatchStacks(FixturePatches);

	// Auto assign as per mode
	switch (AutoAssignMode)
	{
	case EDMXFixturePatchAutoAssignMode::FirstUniverseInSelection:
		AssignToFirstUniverseInSelection();
		break;

	case EDMXFixturePatchAutoAssignMode::FirstReachableUniverse:
		AssignToFirstReachableUniverse();
		break;

	case EDMXFixturePatchAutoAssignMode::LastAddressInLibrary:
		AutoAssignAfterLastAddressInLibrary();
		break;

	default:
		checkf(0, TEXT("Trying to use unhandled auto assign mode in FDMXFixturePatchAutoAssignUtility"));
	}
}

void FDMXFixturePatchAutoAssignUtility::AssignToFirstUniverseInSelection()
{
	if (!ensureMsgf(!AutoAssignPatchStacks.IsEmpty(), TEXT("Cannot auto assign patches without patches. Such cases should have been handled previously.")))
	{
		return;
	}

	if (!ensureMsgf(!AutoAssignPatchStacks[0].IsEmpty() || !AutoAssignPatchStacks[0][0], TEXT("No patch or invalid patch in patch stack when trying to auto assign..")))
	{
		return;
	}

	const uint64 MinRequiredSize = ComputeMinRequiredSize();

	UDMXEntityFixturePatch* FirstPachToAutoAssign = AutoAssignPatchStacks[0][0];
	const uint64 FirstReachableAbsoluteAddress = FirstPachToAutoAssign->GetUniverseID() * DMX_UNIVERSE_SIZE + 1;
	const TArray<TRange<uint64>> FreeRanges = ComputeFreeRanges(FirstReachableAbsoluteAddress);
	for (const TRange<uint64>& FreeRange : FreeRanges)
	{
		const uint64 SizeOfRange = FreeRange.Size<uint64>();
		if (SizeOfRange >= MinRequiredSize)
		{
			if (CanAutoAssignToRange(FreeRange))
			{
				PerformAutoAssign(FreeRange.GetLowerBoundValue());
				return;
			}
		}
	}
}

void FDMXFixturePatchAutoAssignUtility::AssignToFirstReachableUniverse()
{
	if (!ensureMsgf(CanReachAnyUniverse(), TEXT("If a DMX Library has no reachable universe, options to auto-assign to first reachable universe should be disabled. Please use CanReachAnyUniverse() to disable such options.")))
	{
		return;
	}

	const uint64 MinRequiredSize = ComputeMinRequiredSize();

	const uint64 FirstReachableAbsoluteAddress = GetFirstReachableUniverse() * DMX_UNIVERSE_SIZE + 1;
	const TArray<TRange<uint64>> FreeRanges = ComputeFreeRanges(FirstReachableAbsoluteAddress);
	for (const TRange<uint64>& FreeRange : FreeRanges)
	{
		const uint64 SizeOfRange = FreeRange.Size<uint64>();
		if (SizeOfRange >= MinRequiredSize)
		{
			if (CanAutoAssignToRange(FreeRange))
			{
				PerformAutoAssign(FreeRange.GetLowerBoundValue());
				return;
			}
		}
	}
}

void FDMXFixturePatchAutoAssignUtility::AutoAssignAfterLastAddressInLibrary()
{
	if (!ensureMsgf(!AutoAssignPatchStacks.IsEmpty(), TEXT("Cannot auto assign patches without patches. Such cases should have been handled previously.")))
	{
		return;
	}

	if (!ensureMsgf(!AutoAssignPatchStacks[0].IsEmpty() || !AutoAssignPatchStacks[0][0], TEXT("Cannot auto assign. No patch or invalid patch in patch stack when trying to auto assign..")))
	{
		return;
	}

	if (!ensureMsgf(!OtherPatches.IsEmpty() && OtherPatches[0], TEXT("Cannot auto assign. Invalid 'other' patch when trying to auto assign to last address in DMX Library")))
	{
		return;
	}

	const uint64 MinRequiredSize = ComputeMinRequiredSize();

	const uint64 AbsoluteAddressAfterLastPatchInLibrary = [this]()
	{
		if (OtherPatches.IsEmpty())
		{
			UDMXEntityFixturePatch* FirstPachToAutoAssign = AutoAssignPatchStacks[0][0];
			return FirstPachToAutoAssign->GetUniverseID() * DMX_UNIVERSE_SIZE + 1;
		}
		else
		{
			UDMXEntityFixturePatch* LastPatchInLibrary = OtherPatches.Last();
			return LastPatchInLibrary->GetUniverseID() * DMX_UNIVERSE_SIZE + 1;
		}
	}();
	const TArray<TRange<uint64>> FreeRanges = ComputeFreeRanges(AbsoluteAddressAfterLastPatchInLibrary);
	for (const TRange<uint64>& FreeRange : FreeRanges)
	{
		const uint64 SizeOfRange = FreeRange.Size<uint64>();
		if (SizeOfRange >= MinRequiredSize)
		{
			if (CanAutoAssignToRange(FreeRange))
			{
				PerformAutoAssign(FreeRange.GetLowerBoundValue());
				return;
			}
		}
	}
}

bool FDMXFixturePatchAutoAssignUtility::CanAutoAssignToRange(const TRange<uint64>& Range) const
{
	const uint64 SizeOfRange = Range.Size<uint64>();

	// For simplicity work with a 0 - 511 range
	uint64 RelativeStartingChannel = (Range.GetLowerBoundValue() - 1) % DMX_UNIVERSE_SIZE;
	uint64 PatchedChannels = 0;
	uint64 VoidChannels = 0; // Channels that take space but cannot hold a patch
	for (const TArray<UDMXEntityFixturePatch*>& PatchStack : AutoAssignPatchStacks)
	{
		check(!PatchStack.IsEmpty() && PatchStack[0]);

		const int32 ChannelSpan = PatchStack[0]->GetChannelSpan();
		if (RelativeStartingChannel + ChannelSpan > DMX_UNIVERSE_SIZE)
		{
			VoidChannels += DMX_UNIVERSE_SIZE - RelativeStartingChannel;
			RelativeStartingChannel = 0;
		}

		if (PatchedChannels + VoidChannels + ChannelSpan > SizeOfRange)
		{
			return false;
		}

		PatchedChannels += ChannelSpan;
	}

	return true;
}

void FDMXFixturePatchAutoAssignUtility::PerformAutoAssign(uint64 AbsoluteStartingChannel)
{
	const FScopedTransaction AutoAssignTransaction(NSLOCTEXT("DMXFixturePatchAutoAssignUtility", "AutoAssignTransaction", "Auto assign fixture patches"));

	for (const TArray<UDMXEntityFixturePatch*>& PatchStack : AutoAssignPatchStacks)
	{
		check(!PatchStack.IsEmpty() && PatchStack[0]);

		int32 RelativeStartingChannel = AbsoluteStartingChannel % DMX_UNIVERSE_SIZE;
		int32 Universe = AbsoluteStartingChannel / DMX_UNIVERSE_SIZE;

		const int32 ChannelSpan = PatchStack[0]->GetChannelSpan();
		if (RelativeStartingChannel + ChannelSpan - 1 > DMX_UNIVERSE_SIZE)
		{
			Universe++;
			RelativeStartingChannel = 1;
		}

		for (UDMXEntityFixturePatch* Patch : PatchStack)
		{
			Patch->PreEditChange(UDMXEntityFixturePatch::StaticClass()->FindPropertyByName(UDMXEntityFixturePatch::GetStartingChannelPropertyNameChecked()));

			Patch->SetUniverseID(Universe);
			Patch->SetStartingChannel(RelativeStartingChannel);

			Patch->PostEditChange();
		}

		AbsoluteStartingChannel = PatchStack[0]->GetUniverseID() * DMX_MAX_ADDRESS + PatchStack[0]->GetStartingChannel() + ChannelSpan;
	}
}

uint64 FDMXFixturePatchAutoAssignUtility::ComputeMinRequiredSize() const
{
	uint64 MinRequiredSize = 0;
	for (const TArray<UDMXEntityFixturePatch*>& PatchStack : AutoAssignPatchStacks)
	{
		check(!PatchStack.IsEmpty() && PatchStack[0]);
		MinRequiredSize += PatchStack[0]->GetChannelSpan();
	}

	return MinRequiredSize;
}

TArray<TRange<uint64>> FDMXFixturePatchAutoAssignUtility::ComputeFreeRanges(uint64 FirstAbsoluteAddress) const
{
	TArray<TRange<uint64>> FreeRanges;
	if (OtherPatches.IsEmpty())
	{
		FreeRanges.Add(TRange<uint64>(0, TNumericLimits<uint64>::Max()));
		return FreeRanges;
	}

	// Add the range from the first channel to the first patch, only if the first patch is not on channel 1
	const uint64 FirstAbsoulteStartingChannel = ((uint64)OtherPatches[0]->GetUniverseID() * DMX_UNIVERSE_SIZE) + OtherPatches[0]->GetStartingChannel();
	if (FirstAbsoulteStartingChannel > FirstAbsoluteAddress)
	{
		FreeRanges.Add(TRange<uint64>(FirstAbsoluteAddress, FirstAbsoulteStartingChannel));
	}

	// Add ranges in between patches
	const UDMXEntityFixturePatch* PreviousPatch = nullptr;
	for (UDMXEntityFixturePatch* Patch : OtherPatches)
	{
		if (PreviousPatch)
		{
			const uint64 FreeRangeStart = ((uint64)PreviousPatch->GetUniverseID() * DMX_UNIVERSE_SIZE) + PreviousPatch->GetStartingChannel() + PreviousPatch->GetChannelSpan();
			const uint64 FreeRangeEnd = ((uint64)Patch->GetUniverseID() * DMX_UNIVERSE_SIZE) + Patch->GetStartingChannel();
			
			if (FreeRangeStart >= FirstAbsoluteAddress && FreeRangeStart < FreeRangeEnd)
			{
				FreeRanges.Add(TRange<uint64>(FreeRangeStart, FreeRangeEnd));
			}
		}
		PreviousPatch = Patch;
	}

	// Add the range from the last patch to uint64 max. This always can be added as patches cannot span up to uint64 max.
	const uint64 LastAbsoulteEndingChannel = ((uint64)OtherPatches.Last()->GetUniverseID() * DMX_UNIVERSE_SIZE) + OtherPatches.Last()->GetStartingChannel() + OtherPatches.Last()->GetChannelSpan();
	FreeRanges.Add(TRange<uint64>(LastAbsoulteEndingChannel, TNumericLimits<uint64>::Max()));

	return FreeRanges;
}

bool FDMXFixturePatchAutoAssignUtility::CanReachAnyUniverse() const
{
	check(DMXLibrary);

	return !(DMXLibrary->GetInputPorts().IsEmpty() && DMXLibrary->GetOutputPorts().IsEmpty());
}

int32 FDMXFixturePatchAutoAssignUtility::GetFirstReachableUniverse() const
{
	check(DMXLibrary);
	
	if (!ensureMsgf(CanReachAnyUniverse(), TEXT("If a DMX Library has no reachable universe, options to auto-assign to first reachable universe should be disabled. Please use CanReachAnyUniverse() to disable such options.")))
	{
		return 1;
	}

	int32 Result = TNumericLimits<int32>::Max();
	for (const FDMXInputPortSharedRef& InputPort : DMXLibrary->GetInputPorts())
	{
		Result = FMath::Min(Result, InputPort->GetLocalUniverseStart());
	}
	for (const FDMXOutputPortSharedRef& OutputPort : DMXLibrary->GetOutputPorts())
	{
		Result = FMath::Min(Result, OutputPort->GetLocalUniverseStart());
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
