// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXInputInfoUniverseMonitor.h"
#include "Widgets/SDMXInputInfoUniverseCounts.h"
#include "Widgets/SDMXInputInfoUniverseChannelView.h"
#include "Widgets/SDMXInputInfoSelecter.h"

#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXEditorStyle.h"

#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Async/Async.h"

#define LOCTEXT_NAMESPACE "SDMXInputInfoUniverseMonitor"

void FUniverseCount::CopyChannelValues(const TArray<uint8>& InValues)
{
	FScopeLock BufferLock(&UniverseCountCritSec);
	for (int Ix = 0; Ix < InValues.Num(); Ix++)
	{
		if (InValues[Ix] != 0u)
		{
			ChannelValues.Add(Ix, InValues[Ix]);
		}
	}
}

void SDMXInputInfoUniverseMonitor::Construct(const FArguments& InArgs)
{
	WeakInfoSelector = InArgs._InfoSelector;
	
	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UniverseLabel", "Universe"))
					.Font(FDMXEditorStyle::Get().GetFontStyle("DMXEditor.Font.InputUniverseHeader"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(12.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Top)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ChannelVaalueLabel", "Chnl : Value"))
					.Font(FDMXEditorStyle::Get().GetFontStyle("DMXEditor.Font.InputUniverseHeader"))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				SNew(SSeparator)
				.Orientation(Orient_Horizontal)
			]
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SAssignNew(UniverseCountsList, SListView<TSharedPtr<FUniverseCount>>)
				.ItemHeight(20)
				.ListItemsSource(&UniverseCounts)
				.Visibility(EVisibility::Visible)
				.OnGenerateRow(this, &SDMXInputInfoUniverseMonitor::OnGenerateUniverseRow)
			]
		];
}


void SDMXInputInfoUniverseMonitor::SetupPacketReceiver()
{
	if (!UniverseCounterHandle.IsValid())
	{
		if (TSharedPtr<SDMXInputInfoSelecter> InfoSelecterPtr = WeakInfoSelector.Pin())
		{
			if (IDMXProtocolPtr DMXProtocolPtr = IDMXProtocol::Get(InfoSelecterPtr->GetCurrentProtocolName()))
			{
				UniverseCounterHandle = DMXProtocolPtr->GetOnUniverseInputUpdate().AddRaw(this, &SDMXInputInfoUniverseMonitor::PacketReceiver);
			}
		}
	}
}

void SDMXInputInfoUniverseMonitor::PacketReceiver(FName InProtocol, uint16 InUniverseID, const TArray<uint8>& InValues) 
{
	// Call on Game Thread as we need to interact with the UI.
	AsyncTask(ENamedThreads::GameThread, [this, InProtocol, InUniverseID]()
		{
			// If this gets called after FEngineLoop::Exit(), GetEngineSubsystem() can crash
			if (!IsEngineExitRequested())
			{
				UpdateUniverseCounter(InProtocol, InUniverseID);
			}
		});
}

void SDMXInputInfoUniverseMonitor::CollectDMXData(
		int InCounter, 
		uint16 InUniverseID, 
		TFunction<void(TSharedPtr<FUniverseCount>&, TArray<uint8>&)> Collector
	)
{
	if (WeakInfoSelector.IsValid())
	{
		if (TSharedPtr<SDMXInputInfoSelecter> InfoSelecterPtr = WeakInfoSelector.Pin())
		{
			if (IDMXProtocolPtr Protocol = IDMXProtocol::Get(InfoSelecterPtr->GetCurrentProtocolName()))
			{
				TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> ProtocolUniverse = Protocol->GetUniverseById(InUniverseID);
				if (ProtocolUniverse.IsValid())
				{
					TSharedPtr<FDMXBuffer> Buffer = ProtocolUniverse.Get()->GetInputDMXBuffer();
					if (Buffer.IsValid())
					{
						TSharedPtr<FUniverseCount> Counts = UniverseCounts[InCounter];
						Buffer->AccessDMXData([&Collector, &Counts](TArray<uint8>& InData)
							{
								Collector(Counts, InData);
							});
					}
				}
			}
		}
	}
}

void SDMXInputInfoUniverseMonitor::CopyChannelValues(TSharedPtr<FUniverseCount>& InCounter, TArray<uint8>& InDMXData)
{
	InCounter->CopyChannelValues(InDMXData);
}


void SDMXInputInfoUniverseMonitor::AddNewUniverse(uint16 InUniverseID)
{
	int EmplaceAt = UniverseCounts.Num();
	for (int Ix = 0; Ix < UniverseCounts.Num(); Ix++)
	{
		if (UniverseCounts[Ix]->GetUniverseID() >= InUniverseID)
		{
			EmplaceAt = Ix;
			break;
		}
	}
	UniverseCounts.EmplaceAt(EmplaceAt, MakeShared<FUniverseCount>(InUniverseID));

	for (auto kv : UniverseIDToUIDetails)
	{
		if (kv.Value >= EmplaceAt)
			UniverseIDToUIDetails[kv.Key]++;
	}

	UniverseIDToUIDetails.Add(InUniverseID, EmplaceAt);

	CollectDMXData(
		EmplaceAt,
		InUniverseID,
		&SDMXInputInfoUniverseMonitor::CopyChannelValues
	);
}


void SDMXInputInfoUniverseMonitor::UpdateUniverseCounter(FName InProtocol, uint16 InUniverseID)
{
	if (WeakInfoSelector.IsValid())
	{
		if (TSharedPtr<SDMXInputInfoSelecter> InfoSelecterPtr = WeakInfoSelector.Pin())
		{
			FName currentProtocolName = InfoSelecterPtr->GetCurrentProtocolName();
			if (currentProtocolName == InProtocol)
			{
				if (!UniverseIDToUIDetails.Contains(InUniverseID))
				{
					AddNewUniverse(InUniverseID);
				}
				else
				{
					int CollectFor = UniverseIDToUIDetails[InUniverseID];
					CollectDMXData(CollectFor, InUniverseID, [](TSharedPtr<FUniverseCount>& InCounter, TArray<uint8>& InDMXData)
						{
							InCounter->CopyChannelValues(InDMXData);
						}
					);
				}
				UniverseCountsList->RequestListRefresh();

				int DisplayIndex = UniverseIDToUIDetails[InUniverseID];

				TSharedPtr<FUniverseCount>& UniverseCount = UniverseCounts[DisplayIndex];
				if (UniverseCount.IsValid())
				{
					TSharedPtr<SDMXInputInfoUniverseCounts>& Display = UniverseCount->GetDisplay();
					if (Display.IsValid())
					{
						Display->SetValue(UniverseCount);
					}
				}
			}
			else
			{
				UniverseIDToUIDetails.Reset();
				UniverseCounts.Reset();

				UpdateUniverseCounter(InProtocol, InUniverseID);
			}
		}
	}
}

TSharedRef<ITableRow> SDMXInputInfoUniverseMonitor::OnGenerateUniverseRow(TSharedPtr<FUniverseCount> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	typedef STableRow<TSharedPtr<FUniverseCount>> RowType;

	TSharedRef<RowType> NewRow = SNew(RowType, OwnerTable);
	uint32 UniverseId = Item->GetUniverseID();
	TSharedPtr<SDMXInputInfoUniverseCounts> NewWidget =
		SNew(SDMXInputInfoUniverseCounts)
		.ID(UniverseId)
		.Value(TSharedPtr<FUniverseCount>());
	NewRow->SetContent(NewWidget.ToSharedRef());
	int Index = UniverseIDToUIDetails[UniverseId];
	TSharedPtr<FUniverseCount>& Counter = UniverseCounts[Index];
	Counter->SetDisplay(NewWidget);
	NewWidget->SetValue(Counter);
	return NewRow;
}

void SDMXInputInfoUniverseMonitor::Clear()
{
	UniverseIDToUIDetails.Reset();
	UniverseCounts.Reset();

	UniverseCountsList->RequestListRefresh();

}


#undef LOCTEXT_NAMESPACE