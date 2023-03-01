// Copyright Epic Games, Inc. All Rights Reserved.

#include "SInputDeviceSelector.h"

#include "InputDeviceDetectionProcessor.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "SInputDeviceSelector"

namespace UE::VCamCoreEditor::Private
{
	SInputDeviceSelector::~SInputDeviceSelector()
	{
		StopListeningForInput();
	}

	void SInputDeviceSelector::Construct(const FArguments& InArgs)
	{
		OnInputDeviceIDChangedDelegate = InArgs._OnInputDeviceIDChanged;
		
		ChildSlot
		[
			SNew(SHorizontalBox)
			
			+SHorizontalBox::Slot()
			[
				SNew(SButton)
				.PressMethod(EButtonPressMethod::DownAndUp)
				.ToolTipText(LOCTEXT("KeySelector", "Press a button on an input device to select device ID"))
				.OnClicked(this, &SInputDeviceSelector::ListenForInput)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(EKeys::GetMenuCategoryPaletteIcon(EKeys::Gamepad_FaceButton_Bottom.GetMenuCategory())))
						.ColorAndOpacity(this, &SInputDeviceSelector::GetKeyIconColor)
					]
				]
			]
			
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			[
				SAssignNew(ManualEnterBox, SNumericEntryBox<int32>)
				.OnValueCommitted(this, &SInputDeviceSelector::OnDeviceIDManuallyCommited)
				.Value(InArgs._CurrentInputDeviceID)
			]
		];
	}

	FReply SInputDeviceSelector::ListenForInput()
	{
		if (InputDeviceDetector)
		{
			StopListeningForInput();
			return FReply::Handled();
		}
		
		InputDeviceDetector = FInputDeviceDetectionProcessor::MakeAndRegister(FOnInputDeviceDetected::CreateLambda([this](int32 DeviceID)
		{
			OnInputDeviceIDChangedDelegate.Execute(DeviceID);
			StopListeningForInput();
		}));
		return FReply::Handled();
	}

	void SInputDeviceSelector::StopListeningForInput()
	{
		if (InputDeviceDetector)
		{
			InputDeviceDetector->Unregister();
			InputDeviceDetector.Reset();
		}
	}

	FSlateColor SInputDeviceSelector::GetKeyIconColor() const
	{
		return InputDeviceDetector.IsValid() ? FLinearColor(0.953f, 0.612f, 0.071f) : FLinearColor::White;
	}

	void SInputDeviceSelector::OnDeviceIDManuallyCommited(int32 Value, ETextCommit::Type CommitType)
	{
		OnInputDeviceIDChangedDelegate.Execute(Value);
	}
}

#undef LOCTEXT_NAMESPACE