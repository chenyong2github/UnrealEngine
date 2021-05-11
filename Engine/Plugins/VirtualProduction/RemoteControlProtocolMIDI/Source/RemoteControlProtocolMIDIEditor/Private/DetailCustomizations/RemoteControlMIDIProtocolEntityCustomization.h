// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IDetailCustomization.h"
#include "IDetailCustomNodeBuilder.h"
#include "IPropertyTypeCustomization.h"
#include "MIDIDeviceController.h"

class ISinglePropertyView;
class ITableRow;
class SComboButton;
class SMIDIDeviceComboBox;
class STableViewBase;
template <typename ItemType> class SListView;
template <typename NumericType> class SNumericEntryBox;

/** Menu item for a MIDI Event Type */
struct FMIDIEventType
{
	FMIDIEventType() = default;

	/** Create a FMIDIEventType for the given EventTypeId */
	explicit FMIDIEventType(const uint8 InEventTypeId)
		: EventTypeId(InEventTypeId)
	{}

	/** Create a FMIDIEventType for the given PropertyHandle (int32) */
	explicit FMIDIEventType(const TSharedPtr<IPropertyHandle>& InPropertyHandle);

	/** Numbered Event Type */
	int32 EventTypeId = 9;

	/** Named Event Type */
	EMIDIEventType EventTypeName = EMIDIEventType::Unknown;

	/** Get display name, if applicable */
	FText GetDisplayName() const;

	/** Returns true if the current EventTypeId matches an entry in EMIDIEventType */
	bool HasName() const
	{
		return GetNamedEventIds().Contains(EventTypeId);		
	}

	/** Returns the corresponding named event type for the current EventTypeId, or EMIDIEventType::Unknown if there's no correspondence */
	EMIDIEventType GetEventTypeEnumForId()
	{
		if(HasName())
		{
			return static_cast<EMIDIEventType>(EventTypeId);
		}

		return EMIDIEventType::Unknown;
	}

	/** Updates from the given PropertyHandle (int32) */
	void UpdateFromHandle(const TSharedPtr<IPropertyHandle>& InPropertyHandle);

private:
	/** Cached EMIDIEventType id's */
	static TArray<uint8> NamedEventIds;

	/** Cached display names for recognized Event Types */
	static TMap<uint8, FText> NamedEventDisplayNames;

	/** Get NamedEventIds, initializing if necessary */
	TArray<uint8> GetNamedEventIds() const;
};

/** Struct customization for RemoteControlMIDIProtocolEntity */
class REMOTECONTROLPROTOCOLMIDIEDITOR_API FRemoteControlMIDIProtocolEntityCustomization : public IDetailCustomization
{
public:
	FRemoteControlMIDIProtocolEntityCustomization();
	~FRemoteControlMIDIProtocolEntityCustomization() = default;

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FRemoteControlMIDIProtocolEntityCustomization>();
	}

	//~ IDetailCustomization interface begin
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ IDetailCustomization interface end

private:
	/** Event Type property handle */
	TSharedPtr<IPropertyHandle> EventTypeHandle;

	/** Lightweight representation of the EventType */
	TSharedPtr<FMIDIEventType> CurrentEventType;

	/** A list of all event types to choose from */
	static TArray<TSharedPtr<FMIDIEventType>> EventTypeSource;

	/** Displays the event name or number */
	TSharedPtr<SWidget> EventNameWidget;

	/** Numeric widget for event id */
	TSharedPtr<SNumericEntryBox<int32>> EventValueWidget;
	
	/** Widgets for the event types */
	TSharedPtr<SComboButton> EventTypeComboButton;
	TSharedPtr<SListView<TSharedPtr<FMIDIEventType>>> EventTypeListView;
};
