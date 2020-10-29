// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Monitors/SDMXActivityMonitor.h"

#include "DMXEditorLog.h"
#include "DMXEditorSettings.h"
#include "DMXEditorStyle.h"
#include "DMXEditorUtils.h"
#include "DMXSubsystem.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityController.h"
#include "Widgets/SNameListPicker.h"
#include "Widgets/Monitors/SDMXActivityInUniverse.h"

#include "Async/Async.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformString.h"
#include "HAL/CriticalSection.h"
#include "Internationalization/Regex.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"


#define LOCTEXT_NAMESPACE "SDMXActivityMonitor"

const FName FDMXActivityMonitorConstants::MonitoredSourceInputName = FName("Input");
const FText FDMXActivityMonitorConstants::MonitoredSourceInputText = LOCTEXT("MonitorSourceInputText", "Input");
const FName FDMXActivityMonitorConstants::MonitoredSourceOutputName = FName("Output");
const FText FDMXActivityMonitorConstants::MonitoredSourceOutputText = LOCTEXT("MonitorSourceOutputText", "Output");

void SDMXActivityMonitor::Construct(const FArguments& InArgs)
{
	MonitoredSourceNamesSource.Add(MakeShared<FName>(FDMXActivityMonitorConstants::MonitoredSourceInputName));
	MonitoredSourceNamesSource.Add(MakeShared<FName>(FDMXActivityMonitorConstants::MonitoredSourceOutputName));

	LoadMonitorSettings();

	SetCanTick(true);

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			.AutoHeight()
			[
				SNew(SWrapBox)
				.InnerSlotPadding(FVector2D(35.0f, 10.0f))
				.UseAllottedWidth(true)

				// Protocol
				+ SWrapBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)

					//Label
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ProtocolLabel", "Protocol"))
					] 

					// Protocol combo box
					+ SHorizontalBox::Slot()
					.Padding(10.0f, 0.0f, 0.0f, 0.0f)
					.AutoWidth()
					[
						SNew(SBox)
						.MinDesiredWidth(80.0f)
						[
							SNew(SNameListPicker)
							.Value(this, &SDMXActivityMonitor::GetProtocolName)
							.OnValueChanged(this, &SDMXActivityMonitor::OnProtocolSelected)
							.OptionsSource(FDMXProtocolName::GetPossibleValues())
						]
					]
				]

				// Min Universe ID
				+ SWrapBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)

					//Label
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("UniversesLabel", "Universes"))
					] 

					// Min Universe ID SSpinBox
					+ SHorizontalBox::Slot()
					.Padding(10.0f, 0.0f, 0.0f, 0.0f)
					.AutoWidth()
					[
						SAssignNew(MinUniverseIDSpinBox, SSpinBox<uint32>)
						.SliderExponent(1000)
						.Value(MinUniverseID)
						.OnValueCommitted(this, &SDMXActivityMonitor::OnMinUniverseIDCommitted)
						.MinValue(0u)
						.MaxValue(UINT16_MAX)
						.MinSliderValue(0u)
						.MaxSliderValue(UINT16_MAX)
						.MinDesiredWidth(50.0f)
					]

					//Label
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(5.0f, 0.0f, 0.0f, 0.0f)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ToLabel", "to"))
					]

					// Max Universe ID SSpinBox
					+ SHorizontalBox::Slot()
					.Padding(5.0f, 0.0f, 0.0f, 0.0f)
					.AutoWidth()
					[
						SAssignNew(MaxUniverseIDSpinBox, SSpinBox<uint32>)
						.SliderExponent(100.0f)
						.Value(MaxUniverseID)
						.OnValueCommitted(this, &SDMXActivityMonitor::OnMaxUniverseIDCommitted)
						.MinValue(0)
						.MaxValue(DMX_MAX_UNIVERSE)
						.MinSliderValue(0)
						.MaxSliderValue(DMX_MAX_UNIVERSE)
						.MinDesiredWidth(50.0f)
					]
				]

				// Protocol selection
				+ SWrapBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)

					//Label
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("MonitorSourceLabel", "Source"))
					] 

					// Monitor origin combo box
					+ SHorizontalBox::Slot()						
					.Padding(10.0f, 0.0f, 0.0f, 0.0f)
					.AutoWidth()					
					[
						SNew(SBox)
						.MinDesiredWidth(80.0f)
						[
							SAssignNew(MonitoredSourceCombobBox, SComboBox<TSharedPtr<FName>>)						
							.OptionsSource(&MonitoredSourceNamesSource)
							.OnGenerateWidget(this, &SDMXActivityMonitor::GenerateMonitorSourceEntry)
							.OnSelectionChanged(this, &SDMXActivityMonitor::OnMonitoredSourceChanged)
							[
								SNew(STextBlock)
								.Text(this, &SDMXActivityMonitor::GetMonitoredSourceText)
							]
						]
					]
				]

				// Clear button
				+ SWrapBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearTextLabel", "Clear"))
					.OnClicked(this, &SDMXActivityMonitor::OnClearButtonClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(3.0f)
			[
				SNew(SSeparator)
				.Orientation(Orient_Horizontal)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
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
					.Text(LOCTEXT("ChannelValueLabel", "Addr / Value"))
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
				SNew(SScrollBox)
				.Orientation(EOrientation::Orient_Horizontal)

				+ SScrollBox::Slot()				
				[
					SAssignNew(UniverseList, SListView<TSharedPtr<SDMXActivityInUniverse>>)
					.ItemHeight(20.0f)
					.ListItemsSource(&UniverseListSource)
					.Visibility(EVisibility::Visible)
					.OnGenerateRow(this, &SDMXActivityMonitor::OnGenerateUniverseRow)
				]
			]
		];
}

void SDMXActivityMonitor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (!IsEngineExitRequested() && FSlateApplication::IsInitialized())
	{
		if (IDMXProtocolPtr DMXProtocolPtr = IDMXProtocol::Get(ProtocolName))
		{
			const IDMXUniverseSignalMap& InboundSignalMap = DMXProtocolPtr->GameThreadGetInboundSignals();

			for (const TPair<int32, TSharedPtr<FDMXSignal>>& UniverseSingalKvp : InboundSignalMap)
			{
				const TSharedRef<SDMXActivityInUniverse>& ActivityWidget = GetOrCreateActivityWidget(UniverseSingalKvp.Value->UniverseID);
				ActivityWidget->VisualizeInputBuffer(UniverseSingalKvp.Value->ChannelData);
			}
		}
	}
}

TSharedRef<ITableRow> SDMXActivityMonitor::OnGenerateUniverseRow(TSharedPtr<SDMXActivityInUniverse> ActivityWidget, const TSharedRef<STableViewBase>& OwnerTable)
{
	return
		SNew(STableRow<TSharedPtr<SDMXActivityInUniverse>>, OwnerTable)
		[
			ActivityWidget.ToSharedRef()
		];
}

void SDMXActivityMonitor::LoadMonitorSettings()
{
	// Restore and set the protocol
	UDMXEditorSettings* DMXEditorSettings = GetMutableDefault<UDMXEditorSettings>();
	ProtocolName = FDMXProtocolName(DMXEditorSettings->ActivityMonitorProtocol);
	if (!ProtocolName.IsValid())
	{
		ProtocolName = FDMXProtocolName(IDMXProtocol::GetFirstProtocolName());
	}
	check(ProtocolName.IsValid());

	// Restore and set the monitored source
	TSharedPtr<FName>* MonitorSourceNameFromSettings =
		MonitoredSourceNamesSource.FindByPredicate([&](TSharedPtr<FName> MonitorSource) {
			return *MonitorSource.Get() == DMXEditorSettings->ActivityMonitorSource;
		});

	if (MonitorSourceNameFromSettings)
	{
		MonitoredSourceName = *MonitorSourceNameFromSettings;
	}
	else
	{
		check(MonitoredSourceNamesSource.Num() > 0);
		MonitoredSourceName = MonitoredSourceNamesSource[0];
	}

	// Restore Min and Max Universe ID
	if (IDMXProtocolPtr Protocol = ProtocolName.GetProtocol())
	{
		MinUniverseID = DMXEditorSettings->ActivityMonitorMinUniverseID;
		if (!IsUniverseIDInRange(MinUniverseID))
		{
			MinUniverseID = Protocol->GetMinUniverseID();
		}

		MaxUniverseID = DMXEditorSettings->ActivityMonitorMaxUniverseID;
		if (!IsUniverseIDInRange(MaxUniverseID))
		{
			MaxUniverseID = Protocol->GetMaxUniverses();
		}
	}

	// Save, in case settings were not valid and changed
	SaveMonitorSettings();

	// Adopt the loaded settings
	SetProtocol(ProtocolName);
}

void SDMXActivityMonitor::SaveMonitorSettings() const
{
	check(MonitoredSourceName.IsValid());

	UDMXEditorSettings* DMXEditorSettings = GetMutableDefault<UDMXEditorSettings>();
	check(DMXEditorSettings);

	DMXEditorSettings->ActivityMonitorProtocol = ProtocolName.Name;
	DMXEditorSettings->ActivityMonitorMinUniverseID = MinUniverseID;
	DMXEditorSettings->ActivityMonitorMaxUniverseID = MaxUniverseID;
	DMXEditorSettings->ActivityMonitorSource = *MonitoredSourceName.Get();

	DMXEditorSettings->SaveConfig();
}

void SDMXActivityMonitor::SetProtocol(FName NewProtocolName)
{
	FDMXProtocolName NewDMXProtocolName(NewProtocolName);
	check(NewProtocolName.IsValid());

	IDMXProtocolPtr OldProtocol = ProtocolName.GetProtocol();
	IDMXProtocolPtr NewProtocol = NewDMXProtocolName.GetProtocol();
	check(NewProtocol.IsValid());
	ProtocolName = NewDMXProtocolName;

	if (IDMXProtocolPtr Protocol = ProtocolName.GetProtocol())
	{
		if (!IsUniverseIDInRange(MinUniverseID))
		{
			MinUniverseID = Protocol->GetMinUniverseID();
		}
		if (!IsUniverseIDInRange(MaxUniverseID))
		{
			MaxUniverseID = FMath::Clamp(static_cast<uint16>(MinUniverseID + 100), Protocol->GetMinUniverseID(), Protocol->GetMaxUniverses());
		}

		AddMonitoredUniversesToProtocol();
	}
}

TSharedRef<SDMXActivityInUniverse> SDMXActivityMonitor::GetOrCreateActivityWidget(uint16 UniverseID)
{
	TSharedPtr<SDMXActivityInUniverse>* BufferViewPtr = UniverseListSource.FindByPredicate([&](const TSharedPtr<SDMXActivityInUniverse>& BufferViewCandidate) {
			return BufferViewCandidate->GetUniverseID() == UniverseID;
		});

	TSharedPtr<SDMXActivityInUniverse> BufferView = BufferViewPtr ? *BufferViewPtr : nullptr;
	if (!BufferView.IsValid())
	{
		BufferView =
			SNew(SDMXActivityInUniverse)
			.UniverseID(UniverseID);

		UniverseListSource.Add(BufferView);

		// Sort the universe list source ascending
		UniverseListSource.Sort([](const TSharedPtr<SDMXActivityInUniverse>& ViewA, const TSharedPtr<SDMXActivityInUniverse>& ViewB) {
			return ViewA->GetUniverseID() < ViewB->GetUniverseID();
			});

		UniverseList->RequestListRefresh();
	}

	check(BufferView.IsValid());
	return BufferView.ToSharedRef();
}

void SDMXActivityMonitor::AddMonitoredUniversesToProtocol()
{
	if (IDMXProtocolPtr Protocol = ProtocolName.GetProtocol())
	{
		for (uint32 UniverseID = MinUniverseID; UniverseID <= MaxUniverseID; UniverseID++)
		{
			IDMXProtocolUniversePtr Universe = Protocol->GetUniverseById(UniverseID);

			// If the universe is not existing yet, add it to the protocol
			if (!Universe.IsValid())
			{
				FJsonObject UniverseSettings;
				Protocol->GetDefaultUniverseSettings(UniverseID, UniverseSettings);
				Protocol->AddUniverse(UniverseSettings);
			}
		}
	}
}

void SDMXActivityMonitor::OnProtocolSelected(FName NewProtocolName)
{
	FDMXEditorUtils::ZeroAllDMXBuffers();

	ClearDisplay();
	SetProtocol(NewProtocolName);

	SaveMonitorSettings();
}

void SDMXActivityMonitor::OnMinUniverseIDCommitted(uint32 NewValue, ETextCommit::Type CommitType)
{
	check(MinUniverseIDSpinBox.IsValid());
	check(IsUniverseIDInRange(NewValue));

	if (MaxUniverseID >= NewValue)
	{
		MinUniverseID = NewValue;
	}
	else
	{
		MinUniverseID = MaxUniverseID;
		MinUniverseIDSpinBox->SetValue(MinUniverseID);
	}

	ClearDisplay();
	AddMonitoredUniversesToProtocol();

	SaveMonitorSettings();
}

void SDMXActivityMonitor::OnMaxUniverseIDCommitted(uint32 NewValue, ETextCommit::Type CommitType)
{
	check(MaxUniverseIDSpinBox.IsValid());
	check(IsUniverseIDInRange(NewValue));

	if (MinUniverseID <= NewValue)
	{
		MaxUniverseID = NewValue;
	}
	else
	{
		MaxUniverseID = MinUniverseID;
		MaxUniverseIDSpinBox->SetValue(MaxUniverseID);
	}

	ClearDisplay();
	AddMonitoredUniversesToProtocol();

	SaveMonitorSettings();
}

FReply SDMXActivityMonitor::OnClearButtonClicked()
{
	FDMXEditorUtils::ZeroAllDMXBuffers();

	ClearDisplay();

	return FReply::Handled();
}

bool SDMXActivityMonitor::IsUniverseIDInRange(uint32 InUniverseID) const
{
	if (IDMXProtocolPtr Protocol = ProtocolName.GetProtocol())
	{
		return 
			Protocol->GetMinUniverseID() <= InUniverseID && 
			InUniverseID <= Protocol->GetMaxUniverses();
	}
	return false;
}

void SDMXActivityMonitor::ClearDisplay()
{
	check(UniverseList.IsValid());

	UniverseListSource.Reset();
	UniverseList->RequestListRefresh();
}

FText SDMXActivityMonitor::GetMonitoredSourceText() const
{
	check(MonitoredSourceName.IsValid());
		
	if (*MonitoredSourceName == FDMXActivityMonitorConstants::MonitoredSourceInputName)
	{
		return FDMXActivityMonitorConstants::MonitoredSourceInputText;
	}
	else if (*MonitoredSourceName == FDMXActivityMonitorConstants::MonitoredSourceOutputName)
	{
		return FDMXActivityMonitorConstants::MonitoredSourceOutputText;
	}

	UE_LOG_DMXEDITOR(Fatal, TEXT("Unhandled Mointored Source Name."));
	return FText::GetEmpty();
}

TSharedRef<SWidget> SDMXActivityMonitor::GenerateMonitorSourceEntry(TSharedPtr<FName> ProtocolNameToAdd)
{
	return
		SNew(STextBlock)
		.Text(FText::FromName(*ProtocolNameToAdd.Get()));
}

void SDMXActivityMonitor::OnMonitoredSourceChanged(TSharedPtr<FName> NewMonitoredSourceName, ESelectInfo::Type SelectInfo)
{
	check(NewMonitoredSourceName.IsValid());

	MonitoredSourceName = NewMonitoredSourceName;

	ClearDisplay();
	SaveMonitorSettings();
	AddMonitoredUniversesToProtocol();
}

#undef LOCTEXT_NAMESPACE
