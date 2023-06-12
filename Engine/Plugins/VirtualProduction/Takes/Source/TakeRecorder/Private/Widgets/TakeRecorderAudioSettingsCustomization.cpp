// Copyright Epic Games, Inc. All Rights Reserved.

#include "TakeRecorderAudioSettingsCustomization.h"
#include "IDetailChildrenBuilder.h"
#include "TakeRecorderSettings.h"
#include "TakeRecorderSourceProperty.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"

void FAudioInputDevicePropertyCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	UseSystemDefaultHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAudioInputDeviceProperty, bUseSystemDefaultAudioDevice));
	InputDeviceHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAudioInputDeviceProperty, DeviceId));
	BufferSizeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAudioInputDeviceProperty, AudioInputBufferSize));
	DeviceInfoArrayHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAudioInputDeviceProperty, DeviceInfoArray))->AsArray();

	// Create check box which indicates system default audio device should be used (disables device combobox)
	if (UseSystemDefaultHandle.IsValid())
	{
		IDetailPropertyRow& UseSystemDefaultRow = ChildBuilder.AddProperty(PropertyHandle);
		UseSystemDefaultRow.CustomWidget()
		.NameContent()
		[
			UseSystemDefaultHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([&]() -> ECheckBoxState
			{
				bool bUseSystemDefault;
				UseSystemDefaultHandle->GetValue(bUseSystemDefault);

				return bUseSystemDefault ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([&](ECheckBoxState NewState) -> void
			{
				bool bUseSystemDefault = (NewState == ECheckBoxState::Checked);
				UseSystemDefaultHandle->SetValue(bUseSystemDefault);
                SynchronizeWidgetStates();
			})
		];
	}

	// Create audio input device combobox
	if (InputDeviceHandle.IsValid() && DeviceInfoArrayHandle.IsValid())
	{
		IDetailPropertyRow& InputDevicePropertyRow = ChildBuilder.AddProperty(PropertyHandle);

		InputDevicePropertyRow.CustomWidget()
		.NameContent()
		[
			InputDeviceHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			MakeAudioInputSelectorWidget()
		];
	}

	// Create audio callback buffer size widget
	if (BufferSizeHandle.IsValid())
	{
		IDetailPropertyRow& BufferSizePropertyRow = ChildBuilder.AddProperty(PropertyHandle);
		BufferSizePropertyRow.CustomWidget()
		.NameContent()
		[
			BufferSizeHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			BufferSizeHandle->CreatePropertyValueWidget()
		];
	}

	// Register delegate so UI can update when the audio input device changes
	// This is needed because the device menu lives in both the project settings window and the take recorder panel
	if (UTakeRecorderAudioInputSettings* AudioInputSettings = TakeRecorderAudioSettingsUtils::GetTakeRecorderAudioInputSettings())
	{
		AudioInputSettings->GetOnAudioInputDeviceChanged().Add(FSimpleDelegate::CreateSP(this, &FAudioInputDevicePropertyCustomization::SynchronizeWidgetStates));
	}
}

FString FAudioInputDevicePropertyCustomization::GetDeviceNameFromInfoProperty(const TSharedPtr<IPropertyHandle>& InDeviceInfoHandle)
{
	FString DeviceNameStr;
	if (InDeviceInfoHandle && InDeviceInfoHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> DeviceNameHandle = InDeviceInfoHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAudioInputDeviceInfoProperty, DeviceName));
		if (DeviceNameHandle.IsValid())
		{
			DeviceNameHandle->GetValue(DeviceNameStr);
		}
	}

	return DeviceNameStr;
}

FString FAudioInputDevicePropertyCustomization::GetDeviceIdFromInfoProperty(const TSharedPtr<IPropertyHandle>& InDeviceInfoHandle)
{
	FString DeviceIdStr;
	if (InDeviceInfoHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> DeviceIdHandle = InDeviceInfoHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAudioInputDeviceInfoProperty, DeviceId));
		if (DeviceIdHandle.IsValid())
		{
			DeviceIdHandle->GetValue(DeviceIdStr);
		}
	}

	return DeviceIdStr;
}

int32 FAudioInputDevicePropertyCustomization::GetChannelCountFromInfoProperty(const TSharedPtr<IPropertyHandle>& InDeviceInfoHandle)
{
	int32 ChannelCount = 0;
	if (InDeviceInfoHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> ChannelCountHandle = InDeviceInfoHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAudioInputDeviceInfoProperty, InputChannels));
		if (ChannelCountHandle.IsValid())
		{
			ChannelCountHandle->GetValue(ChannelCount);
		}
	}

	return ChannelCount;
}

bool FAudioInputDevicePropertyCustomization::GetIsDefaultFromInfoProperty(const TSharedPtr<IPropertyHandle>& InDeviceInfoHandle)
{
	bool bIsDefaultDevice = false;
	if (InDeviceInfoHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> IsDefaultHandle = InDeviceInfoHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAudioInputDeviceInfoProperty, bIsDefaultDevice));
		if (IsDefaultHandle.IsValid())
		{
			IsDefaultHandle->GetValue(bIsDefaultDevice);
		}
	}

	return bIsDefaultDevice;
}

TSharedRef<SWidget> FAudioInputDevicePropertyCustomization::MakeAudioInputSelectorWidget()
{
	FString CurrentDeviceId;
	InputDeviceHandle->GetValue(CurrentDeviceId);

	bool bHaveCurrentDevice = CurrentDeviceId.Len() != 0;
	TSharedPtr<IPropertyHandle> SelectedAudioInputDevice;

	uint32 NumDevices;
	DeviceInfoArrayHandle->GetNumElements(NumDevices);

	for (uint32 DeviceIndex = 0; DeviceIndex < NumDevices; ++DeviceIndex)
	{
		TSharedRef<IPropertyHandle> DeviceInfoHandle = DeviceInfoArrayHandle->GetElement(DeviceIndex);
		if (DeviceInfoHandle->IsValidHandle())
		{
			bool bIsDefaultDevice = GetIsDefaultFromInfoProperty(DeviceInfoHandle);
			FString DeviceId = GetDeviceIdFromInfoProperty(DeviceInfoHandle);

            if (bIsDefaultDevice)
            {
                DefaultDeviceId = DeviceId;
            }
            
			if (bIsDefaultDevice && !bHaveCurrentDevice)
			{
				CurrentDeviceId = DeviceId;
				InputDeviceHandle->SetValue(CurrentDeviceId);
				SelectedAudioInputDevice = DeviceInfoHandle;
			}
			else if (bHaveCurrentDevice)
			{
				if (CurrentDeviceId == DeviceId)
				{
					SelectedAudioInputDevice = DeviceInfoHandle;
				}
			}

			AudioInputDevices.Add(DeviceInfoHandle);
		}
	}

	// Combo box component:
	TSharedRef<SComboBox<TSharedPtr<IPropertyHandle>>> NewWidget = SNew(SComboBox<TSharedPtr<IPropertyHandle>>)
	.OptionsSource(&AudioInputDevices)
	.InitiallySelectedItem(SelectedAudioInputDevice)
	.OnGenerateWidget_Lambda([](TSharedPtr<IPropertyHandle> InItem)
	{
		return SNew(STextBlock)
			.Text(FText::AsCultureInvariant(GetDeviceNameFromInfoProperty(InItem)))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"));
	})
	.OnSelectionChanged_Lambda([this](TSharedPtr<IPropertyHandle> InSelection, ESelectInfo::Type InSelectInfo)
	{
		if (InSelection.IsValid() && ComboBoxTitleBlock.IsValid())
		{
			InputDeviceHandle->SetValue(GetDeviceIdFromInfoProperty(InSelection));
			ComboBoxTitleBlock->SetText(FText::AsCultureInvariant(GetDeviceNameFromInfoProperty(InSelection)));
		}
	})
	[
		SAssignNew(ComboBoxTitleBlock, STextBlock)
		.Text_Lambda([SelectedAudioInputDevice]()
		{
			return FText::AsCultureInvariant(GetDeviceNameFromInfoProperty(SelectedAudioInputDevice));
		})
		.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
	];

	ComboBox = NewWidget;
	UpdateDeviceCombobxEnabled();

	return NewWidget;
}

void FAudioInputDevicePropertyCustomization::UpdateDeviceCombobxEnabled()
{
	// Get current data model value
	bool bUseSystemDefault;
	UseSystemDefaultHandle->GetValue(bUseSystemDefault);

	// We want the widget enabled if bUseSystemDefault is false
	bool bEnableWidget = !bUseSystemDefault;

	// Get the current widget state
	bool bIsWidgetEnabled = ComboBox->IsEnabled();

	// If the UI doesn't match the data model, refresh the UI
	if (bEnableWidget != bIsWidgetEnabled)
	{
		ComboBox->SetEnabled(bEnableWidget);
        
        // If disabling, set the current menu item to default
        if (!bEnableWidget)
        {
            InputDeviceHandle->SetValue(DefaultDeviceId);
        }
	}
}

void FAudioInputDevicePropertyCustomization::SynchronizeWidgetStates()
{
	UpdateDeviceCombobxEnabled();

	uint32 NumDevices;
	DeviceInfoArrayHandle->GetNumElements(NumDevices);

	// Get current data model value
	FString CurrentDeviceId;
	InputDeviceHandle->GetValue(CurrentDeviceId);

	// Get current menu selection
	TSharedPtr<IPropertyHandle> CurrentMenuSelection = ComboBox->GetSelectedItem();
	
	// If the UI doesn't match the data model, refresh the UI
	if (CurrentDeviceId != GetDeviceIdFromInfoProperty(CurrentMenuSelection))
	{
		for (uint32 DeviceIndex = 0; DeviceIndex < NumDevices; ++DeviceIndex)
		{
			TSharedRef<IPropertyHandle> DeviceInfoHandle = DeviceInfoArrayHandle->GetElement(DeviceIndex);
			if (DeviceInfoHandle->IsValidHandle())
			{
				FString DeviceId = GetDeviceIdFromInfoProperty(DeviceInfoHandle);

				if (DeviceId == CurrentDeviceId)
				{
					ComboBox->SetSelectedItem(DeviceInfoHandle);
					break;
				}
			}
		}
	}
}

UTakeRecorderAudioInputSettings* TakeRecorderAudioSettingsUtils::GetTakeRecorderAudioInputSettings()
{
	const UTakeRecorderProjectSettings* ProjectSettings = GetDefault<UTakeRecorderProjectSettings>();
	for (TWeakObjectPtr<UObject> WeakObject : ProjectSettings->AdditionalSettings)
	{
		UObject* Object = WeakObject.Get();
		if (Object)
		{
			if (Object->IsA<UTakeRecorderAudioInputSettings>())
			{
				return Cast<UTakeRecorderAudioInputSettings>(Object);
			}
		}
	}
	return nullptr;
}
