// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolDMXEditorCustomization.h"

#include "DetailWidgetRow.h"
#include "DMXProtocolTypes.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "RemoteControlProtocolDMX.h"
#include "Library/DMXEntityFixtureType.h"


TSharedRef<IPropertyTypeCustomization> FRemoteControlProtocolDMXEditorTypeCustomization::MakeInstance()
{
	return MakeShared<FRemoteControlProtocolDMXEditorTypeCustomization>();
}

void FRemoteControlProtocolDMXEditorTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructProperty,
                                                                       FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// No need to have header widget, only structure child widget should be present
}

void FRemoteControlProtocolDMXEditorTypeCustomization::CustomizeChildren(const TSharedRef<IPropertyHandle> InStructProperty,
                                                                         IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> ProtocolEntityExtraSettingHandle = InStructProperty;
	if (!ensure(ProtocolEntityExtraSettingHandle.IsValid()))
	{
		return;
	}

	const TSharedPtr<IPropertyHandle> ProtocolEntityHandle = InStructProperty->GetParentHandle();
	if (!ensure(ProtocolEntityHandle.IsValid()))
	{
		return;
	}

	FixtureSignalFormatHandle = ProtocolEntityHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FRemoteControlDMXProtocolEntity, DataType));
	if (!ensure(FixtureSignalFormatHandle.IsValid()))
	{
		return;
	}
	FixtureSignalFormatHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FRemoteControlProtocolDMXEditorTypeCustomization::OnFixtureSignalFormatChange));

	StartingChannelPropertyHandle = ProtocolEntityHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FRemoteControlDMXProtocolEntityExtraSetting, StartingChannel));
	if (!ensure(StartingChannelPropertyHandle.IsValid()))
	{
		return;
	}
	StartingChannelPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FRemoteControlProtocolDMXEditorTypeCustomization::OnStartingChannelChange));
	
	ChildBuilder.AddProperty(StartingChannelPropertyHandle.ToSharedRef());
}

void FRemoteControlProtocolDMXEditorTypeCustomization::OnFixtureSignalFormatChange()
{
	CheckAndApplyStartingChannelValue();
}

void FRemoteControlProtocolDMXEditorTypeCustomization::OnStartingChannelChange()
{
	CheckAndApplyStartingChannelValue();
}

void FRemoteControlProtocolDMXEditorTypeCustomization::CheckAndApplyStartingChannelValue()
{
	// Define Number channels to occupy
	uint8 DataTypeByte = 0;
	FPropertyAccess::Result Result = FixtureSignalFormatHandle->GetValue(DataTypeByte);
	if(!ensure(Result == FPropertyAccess::Success))
	{
		return;
	}
	const EDMXFixtureSignalFormat DataType = static_cast<EDMXFixtureSignalFormat>(DataTypeByte);
	const uint8 NumChannelsToOccupy = UDMXEntityFixtureType::NumChannelsToOccupy(DataType);

	// Get starting channel value
	int32 StartingChannel = 0;
	Result = StartingChannelPropertyHandle->GetValue(StartingChannel);
	if(!ensure(Result == FPropertyAccess::Success))
	{
		return;
	}

	// Calculate channel overhead and apply if StartingChannel plus num channels to occupy doesn't fit into the Universe
	const int32 ChannelOverhead = StartingChannel + NumChannelsToOccupy - DMX_UNIVERSE_SIZE;
	if (ChannelOverhead > 0)
	{
		const int32 MaxStartingChannel = StartingChannel - ChannelOverhead;
		Result = StartingChannelPropertyHandle->SetValue(MaxStartingChannel);
		if(!ensure(Result == FPropertyAccess::Success))
		{
			return;
		}
	}
}

