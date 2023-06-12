// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DetailWidgetRow.h"
#include "IPropertyTypeCustomization.h"
#include "TakeRecorderSourceProperty.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

class FAudioInputDevicePropertyCustomization : public IPropertyTypeCustomization
{
public:

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:

	static FString GetDeviceNameFromInfoProperty(const TSharedPtr<IPropertyHandle>& InDeviceInfoHandle);

	FString GetDeviceIdFromInfoProperty(const TSharedPtr<IPropertyHandle>& InDeviceInfoHandle);
	int32 GetChannelCountFromInfoProperty(const TSharedPtr<IPropertyHandle>& InDeviceInfoHandle);

	bool GetIsDefaultFromInfoProperty(const TSharedPtr<IPropertyHandle>& InDeviceInfoHandle);
	TSharedRef<SWidget> MakeAudioInputSelectorWidget();

	void UpdateDeviceCombobxEnabled();
	void SynchronizeWidgetStates();

	TSharedPtr<IPropertyHandle> UseSystemDefaultHandle;
	TSharedPtr<IPropertyHandle> InputDeviceHandle;
	TSharedPtr<IPropertyHandle> DeviceChannelCountHandle;
	TSharedPtr<IPropertyHandle> BufferSizeHandle;
	TSharedPtr<IPropertyHandleArray> DeviceInfoArrayHandle;

	TArray<TSharedPtr<IPropertyHandle>> AudioInputDevices;
	TSharedPtr<SComboBox<TSharedPtr<IPropertyHandle>>> ComboBox;
	TSharedPtr<STextBlock> ComboBoxTitleBlock;
	FString DefaultDeviceId;
};

class TakeRecorderAudioSettingsUtils
{
public:

	static UTakeRecorderAudioInputSettings* GetTakeRecorderAudioInputSettings();
};