// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "HAL/Platform.h"

class UDMXEntityFixturePatch;
class UDMXLibrary;


/** Available auto assign modes enumerated */
enum class EDMXFixturePatchAutoAssignMode : uint8
{
	/** Auto assign to the first universe in selection */
	FirstUniverseInSelection,

	/** Auto assign to the first universe reachable for the DMX Library */
	FirstReachableUniverse,

	/** Auto assign after the address of the last fixture patch in the library */
	LastAddressInLibrary
};

/**
 * Utility to auto assign fixture patches.
 */
class FDMXFixturePatchAutoAssignUtility
{
public:
	/** 
	 * Auto assigns FixturePatches. If two ore more patches in selection reside on the same address, 
	 * they're auto assigned in order as they appear in the array.
	 * 
	 * Note, all Patches are expected to reside in the same DMX Library.
	 */
	static void AutoAssign(const TArray<UDMXEntityFixturePatch*>& FixturePatches, EDMXFixturePatchAutoAssignMode AutoAssignMode);

private:
	/** Private constructor. Use public FDMXFixturePatchAutoAssignUtility::AutoAssign instead. */
	FDMXFixturePatchAutoAssignUtility(const TArray<UDMXEntityFixturePatch*>& FixturePatches, EDMXFixturePatchAutoAssignMode AutoAssignMode);

	/** Auto assigns to the universe in the patches selected to be auto assigned */
	void AssignToFirstUniverseInSelection();

	/** Auto assigns to the first reachable universe in the library */
	void AssignToFirstReachableUniverse();

	/** Auto assigns after the last patch in the DMX Library */
	void AutoAssignAfterLastAddressInLibrary();

	/** Returns true if patches can be auto assigned to given range */
	bool CanAutoAssignToRange(const TRange<uint64>& Range) const;

	/** Performs the auto assign operation to AutoAssignPatchStacks */
	void PerformAutoAssign(uint64 AbsoluteStartingChannel);

	/** Computes the min size required to fit all patches. Note this may not be a sufficient size, since patches cannot span multiple channels. */
	uint64 ComputeMinRequiredSize() const;

	/** Returns an array of free ranges in this dmx library, using absolute addresses. */
	TArray<TRange<uint64>> ComputeFreeRanges(uint64 FirstAbsoluteAddress) const;
	
	/** Returns true if any universe in the DMX Library can be reached */
	bool CanReachAnyUniverse() const;

	/** Returns the first reachable universe in which this auto assign operation occurs */
	int32 GetFirstReachableUniverse() const;

	/** The DMX Library in which the auto assign operation occurs */
	UDMXLibrary* DMXLibrary = nullptr;

	/** Patches to auto assign, as arrays of valid stacks. A stack are patches of same fixture type, mode and absolute starting channel */
	TArray<TArray<UDMXEntityFixturePatch*>> AutoAssignPatchStacks;

	/** Other patches in library that do not need to be auto assigned */
	TArray<UDMXEntityFixturePatch*> OtherPatches;
};
