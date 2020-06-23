// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

template <typename ItemType> class SListView;

/**
 * Tracks invidual universe channel values and the SDMXInputInfoUniverseCounts for displaying them
 */
struct FUniverseCount
{
	FUniverseCount(uint32 InUniverseID)
	{
		UniverseID = InUniverseID;
	}

	uint32 GetUniverseID()
	{
		return UniverseID;
	}

	void SetDisplay(TSharedPtr<class SDMXInputInfoUniverseCounts> InDisplay)
	{
		Display = InDisplay;
	}

	TSharedPtr<class SDMXInputInfoUniverseCounts>& GetDisplay()
	{
		return Display;
	}

	/** New universe channel values received, update the changed values and remove any 0 values */
	void CopyChannelValues(const TArray<uint8>& InValues);

	TMap<int, uint8>& GetChannelValues()
	{
		return ChannelValues;
	}

private:
	uint32 UniverseID;
	TMap<int, uint8> ChannelValues;
	TSharedPtr<class SDMXInputInfoUniverseCounts> Display;

	/** Mutex to make sure no two threads write to the buffer concurrently */
	mutable FCriticalSection UniverseCountCritSec;
};

/**
 * Monitor inputs of all universes and display non-zero channel values
 */
class SDMXInputInfoUniverseMonitor
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXInputInfoUniverseMonitor)
	{
	}
	SLATE_ARGUMENT(TWeakPtr<class SDMXInputInfoSelecter>, InfoSelector)
	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Remove all displayed  values on the universes monitor */
	void Clear();

	/** Enable routing of packets to this universe */
	void SetupPacketReceiver();

protected:
	TWeakPtr<class SDMXInputInfoSelecter> WeakInfoSelector;
	
	TSharedPtr<SListView<TSharedPtr<FUniverseCount>>> UniverseCountsList;

	TArray<TSharedPtr<FUniverseCount>> UniverseCounts;

	TMap<uint32, int> UniverseIDToUIDetails;

protected:

	/** Handler  for each new packet receieved */
	void PacketReceiver(FName InProtocol, uint16 InUniverseID, const TArray<uint8>& InValues);

	/** Update and display new universe channel values */
	void UpdateUniverseCounter(FName InProtocol, uint16 InUniverseID);

	/** Validate all pointers and call the Collector function */
	void CollectDMXData(int InCounter, uint16 InUniverseID, TFunction<void(TSharedPtr<FUniverseCount>&, TArray<uint8>&)> Collector);

	/** Called by SListView to generate custom list table row */
	TSharedRef<class ITableRow> OnGenerateUniverseRow(TSharedPtr<FUniverseCount> Item, const TSharedRef<class STableViewBase>& OwnerTable);

	static void CopyChannelValues(TSharedPtr<FUniverseCount>& InCounter, TArray<uint8>& InDMXData);

	/** Add new row and counter for new universe */
	void AddNewUniverse(uint16 InUniverseID);
private:
	FDelegateHandle UniverseCounterHandle;

};