// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlMIDIProtocolEntityCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "ISinglePropertyView.h"
#include "MIDIDeviceManager.h"
#include "PropertyHandle.h"
#include "PropertyRestriction.h"
#include "RemoteControlProtocolMIDI.h"
#include "Algo/MaxElement.h"
#include "Algo/Transform.h"
#include "Async/Async.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "FRemoteControlMIDIProtocolEntityCustomization"

TArray<uint8> FMIDIEventType::NamedEventIds = {};
TMap<uint8, FText> FMIDIEventType::NamedEventDisplayNames = {};
TArray<TSharedPtr<FMIDIEventType>> FRemoteControlMIDIProtocolEntityCustomization::EventTypeSource = {};

FMIDIEventType::FMIDIEventType(const TSharedPtr<IPropertyHandle>& InPropertyHandle)
{
	UpdateFromHandle(InPropertyHandle);
}

FText FMIDIEventType::GetDisplayName() const
{
	if(HasName())
	{
		return NamedEventDisplayNames[EventTypeId];
	}
	else
	{
		return FText::AsNumber(EventTypeId);
	}
}

void FMIDIEventType::UpdateFromHandle(const TSharedPtr<IPropertyHandle>& InPropertyHandle)
{
	int32 Value;
	InPropertyHandle->GetValue(Value);

	EventTypeId = Value;
}

TArray<uint8> FMIDIEventType::GetNamedEventIds() const
{
	if(NamedEventIds.Num() > 0)
	{
		return NamedEventIds;
	}

	UEnum* MIDIEventTypeEnum = StaticEnum<EMIDIEventType>();
	for(auto EnumIdx = 1; EnumIdx < MIDIEventTypeEnum->NumEnums() - 1; ++EnumIdx)
	{
		//FString EnumName = MIDIEventTypeEnum->GetNameStringByIndex(EnumIdx);
		const int64 EnumValue = MIDIEventTypeEnum->GetValueByIndex(EnumIdx);
		NamedEventIds.Add(EnumValue);

		const FText EnumDisplayName = MIDIEventTypeEnum->GetDisplayNameTextByIndex(EnumIdx);
		NamedEventDisplayNames.Add(static_cast<uint8>(EnumValue), EnumDisplayName);
	}

	return NamedEventIds;
}

FRemoteControlMIDIProtocolEntityCustomization::FRemoteControlMIDIProtocolEntityCustomization()
{
	// initialize if not already
	if(EventTypeSource.Num() == 0)
	{
		UEnum* MIDIEventTypeEnum = StaticEnum<EMIDIEventType>();
		// Start at 1, so skips "unknown" entry
		for(auto EnumIdx = 1; EnumIdx < MIDIEventTypeEnum->NumEnums() - 1; ++EnumIdx)
		{
			const int64 EnumValue = MIDIEventTypeEnum->GetValueByIndex(EnumIdx);
			EventTypeSource.Emplace(MakeShared<FMIDIEventType>(EnumValue));
		}
	}
}

// Only property customized is EventType
void FRemoteControlMIDIProtocolEntityCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	EventTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(FRemoteControlMIDIProtocolEntity, EventType));
	CurrentEventType = MakeShared<FMIDIEventType>(EventTypeHandle);
	EventTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([this]()
	{
		if(CurrentEventType.IsValid())
		{
			CurrentEventType->UpdateFromHandle(EventTypeHandle);
		}
	}));

	static FLinearColor BackgroundColor = FEditorStyle::GetColor("DefaultForeground");
	BackgroundColor *= 0.75f;
	BackgroundColor.A = 1.0f;
	
	UEnum* MIDIEventTypeEnum = StaticEnum<EMIDIEventType>();

	// Remove EMIDIEventType::Unknown from the UI
	{
		static FText RestrictReason = LOCTEXT("InvalidMIDIEventType", "Unknown isn't a valid MIDIEventType for UI display");
		TSharedPtr<FPropertyRestriction> EnumRestriction = MakeShared<FPropertyRestriction>(RestrictReason);
		EnumRestriction->AddHiddenValue(MIDIEventTypeEnum->GetNameStringByIndex(0));
		EventTypeHandle->AddRestriction(EnumRestriction.ToSharedRef());
	}
	
	IDetailPropertyRow* EventTypeRow = DetailBuilder.EditDefaultProperty(EventTypeHandle);
	ensure(EventTypeRow);

	TSharedPtr<SWidget> DefaultNameWidget;
	TSharedPtr<SWidget> DefaultValueWidget;
	EventTypeRow->GetDefaultWidgets(DefaultNameWidget, DefaultValueWidget);

	auto NameVisibility = [&]
	{
		// if user selected the text input, or is still using the numeric input, keep the text input hidden!
		return EventNameWidget->HasAnyUserFocusOrFocusedDescendants()
            || EventValueWidget->HasAnyUserFocusOrFocusedDescendants() ? EVisibility::Hidden : EVisibility::HitTestInvisible;
	};

	EventValueWidget = StaticCastSharedPtr<SNumericEntryBox<int32>>(DefaultValueWidget);

	const TSharedRef<SWidget> InputWidget = SNew(SOverlay)
    	+ SOverlay::Slot()
    	[
    		EventValueWidget.ToSharedRef()
    	]
		+ SOverlay::Slot()
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SColorBlock)
				.Color(BackgroundColor)
				.Visibility_Lambda(NameVisibility)
			]
			+ SOverlay::Slot()
			[
				SAssignNew(EventNameWidget, SEditableTextBox)
	            .Visibility_Lambda(NameVisibility)
	            .Text_Lambda([&] { return CurrentEventType->GetDisplayName(); })
			]
		];

	using FMIDIEventTypePtr = TSharedPtr<FMIDIEventType>;
	using SEventTypeListType = SListView<FMIDIEventTypePtr>;

	EventTypeRow->CustomWidget()
	.NameContent()
	[
		DefaultNameWidget.ToSharedRef()
	]
    .ValueContent()
    [
        // EventType, with named items from EMIDIEventType, and integer input
        SAssignNew(EventTypeComboButton, SComboButton)
        .ContentPadding(FMargin(0,0,5,0))
        .CollapseMenuOnParentFocus(true)
        .ButtonContent()
        [
            SNew(SBorder)
            .BorderImage(FEditorStyle::GetBrush("NoBorder") )
            .Padding(FMargin(0, 0, 8, 0))
            [
            	InputWidget
            ]
        ]
        .MenuContent()
        [
	        SAssignNew(EventTypeListView, SEventTypeListType)
	        .ListItemsSource(&EventTypeSource)
	        .OnGenerateRow_Lambda([](FMIDIEventTypePtr InItem, const TSharedRef<STableViewBase>& InOwnerTable)
	        {
	            return SNew(STableRow<FMIDIEventTypePtr>, InOwnerTable)
	            [
	                SNew(STextBlock).Text(InItem->GetDisplayName())
	            ];
	        })
	        .OnSelectionChanged_Lambda([this](FMIDIEventTypePtr InProposedSelection, ESelectInfo::Type InSelectInfo)
	        {
	            if(InProposedSelection.IsValid())
	            {
	            	EventTypeHandle->SetValue(static_cast<int32>(InProposedSelection->EventTypeId));
	                
	                const TSharedRef<SWindow> ParentContextMenuWindow = FSlateApplication::Get().FindWidgetWindow(EventTypeListView.ToSharedRef()).ToSharedRef();
	                FSlateApplication::Get().RequestDestroyWindow( ParentContextMenuWindow );
	            }
	        })
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
