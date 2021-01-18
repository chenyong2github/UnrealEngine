// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
* Defines the order mapping for files within a pak.
* When read from the files present in the pak, Indexes will be [0,NumFiles).  This is important for detecting gaps in the order between adjacent files in a patch .pak.
* For new files being added into the pak, the values can be arbitrary, and will be usable only for relative order in an output list.
* Due to the arbitrary values for new files, the FPakOrderMap can contain files with duplicate Order values.
*/
class FPakOrderMap
{
public:
	FPakOrderMap()
		: MaxPrimaryOrderIndex(MAX_uint64)
	{}

	void Empty()
	{
		OrderMap.Empty();
		MaxPrimaryOrderIndex = MAX_uint64;
	}

	int32 Num() const
	{
		return OrderMap.Num();
	}

	/** Add the given filename with the given Sorting Index */
	void Add(const FString& Filename, uint64 Index)
	{
		OrderMap.Add(Filename, Index);
	}

	/**
	* Add the given filename with the given Offset interpreted as Offset in bytes in the Pak File.  This version of Add is only useful when all Adds are done by offset, and are converted
	* into Sorting Indexes at the end by a call to ConvertOffsetsToOrder
	*/
	void AddOffset(const FString& Filename, uint64 Offset)
	{
		OrderMap.Add(Filename, Offset);
	}

	/** Remaps all the current values in the OrderMap onto [0, NumEntries).  Useful to convert from Offset in Pak file bytes into an Index sorted by Offset */
	void ConvertOffsetsToOrder()
	{
		TArray<TPair<FString, uint64>> FilenameAndOffsets;
		for (auto& FilenameAndOffset : OrderMap)
		{
			FilenameAndOffsets.Add(FilenameAndOffset);
		}
		FilenameAndOffsets.Sort([](const TPair<FString, uint64>& A, const TPair<FString, uint64>& B)
		{
			return A.Value < B.Value;
		});
		int64 Index = 0;
		for (auto& FilenameAndOffset : FilenameAndOffsets)
		{
			OrderMap[FilenameAndOffset.Key] = Index;
			++Index;
		}
	}

	bool PAKFILEUTILITIES_API ProcessOrderFile(const TCHAR* ResponseFile, bool bSecondaryOrderFile = false, bool bMergeOrder = false);

	uint64 PAKFILEUTILITIES_API GetFileOrder(const FString& Path, bool bAllowUexpUBulkFallback, bool* OutIsPrimary=nullptr) const;

	void PAKFILEUTILITIES_API WriteOpenOrder(FArchive* Ar);

private:
	FString RemapLocalizationPathIfNeeded(const FString& PathLower, FString& OutRegion) const;

	TMap<FString, uint64> OrderMap;
	uint64 MaxPrimaryOrderIndex;
};


PAKFILEUTILITIES_API bool ExecuteUnrealPak(const TCHAR* CmdLine);

