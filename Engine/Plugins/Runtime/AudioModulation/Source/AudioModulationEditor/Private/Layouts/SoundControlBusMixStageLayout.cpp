// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBusMixStageLayout.h"

#include "Audio.h"
#include "AudioModulationStyle.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "IAudioModulation.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "InputCoreTypes.h"
#include "Misc/Attribute.h"
#include "PropertyRestriction.h"
#include "SCurveEditor.h"
#include "ScopedTransaction.h"
#include "Sound/SoundModulationDestination.h"
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "SoundModulationParameterSettingsLayout.h"
#include "UObject/ReflectedTypeAccessors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"


#define LOCTEXT_NAMESPACE "AudioModulation"


namespace AudioModulationEditorUtils
{
	USoundModulationParameter* GetParameterFromBus(TSharedRef<IPropertyHandle> BusHandle)
	{
		UObject* Object = nullptr;
		if (BusHandle->GetValue(Object) == FPropertyAccess::Success)
		{
			if (USoundControlBus* ControlBus = Cast<USoundControlBus>(Object))
			{
				return ControlBus->Parameter;
			}
		}

		return nullptr;
	}

	void HandleConvertLinearToUnit(TSharedRef<IPropertyHandle> BusHandle, TSharedRef<IPropertyHandle> LinearValueHandle, TSharedRef<IPropertyHandle> UnitValueHandle)
	{
		if (USoundModulationParameter* Parameter = AudioModulationEditorUtils::GetParameterFromBus(BusHandle))
		{
			if (Parameter->RequiresUnitConversion())
			{
				float LinearValue = 1.0f;
				if (LinearValueHandle->GetValue(LinearValue) == FPropertyAccess::Success)
				{
					const float UnitValue = Parameter->ConvertLinearToUnit(LinearValue);
					UnitValueHandle->SetValue(UnitValue, EPropertyValueSetFlags::NotTransactable);
				}
			}
		}
	}

	void HandleConvertUnitToLinear(TSharedRef<IPropertyHandle> BusHandle, TSharedRef<IPropertyHandle> UnitValueHandle, TSharedRef<IPropertyHandle> LinearValueHandle)
	{
		if (USoundModulationParameter* Parameter = AudioModulationEditorUtils::GetParameterFromBus(BusHandle))
		{
			if (Parameter->RequiresUnitConversion())
			{
				float UnitValue = 1.0f;
				if (UnitValueHandle->GetValue(UnitValue) == FPropertyAccess::Success)
				{
					const float LinearValue = Parameter->ConvertUnitToLinear(UnitValue);
					LinearValueHandle->SetValue(LinearValue);
				}
			}
		}
	}
}

void FSoundControlBusMixStageLayoutCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

void FSoundControlBusMixStageLayoutCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	uint32 NumChildren;
	StructPropertyHandle->GetNumChildren(NumChildren);

	TMap<FName, TSharedPtr<IPropertyHandle>> PropertyHandles;

	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedRef<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
		const FName PropertyName = ChildHandle->GetProperty()->GetFName();
		PropertyHandles.Add(PropertyName, ChildHandle);
	}

	TSharedRef<IPropertyHandle> BusHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundControlBusMixStage, Bus)).ToSharedRef();

	TAttribute<EVisibility> BusInfoVisibility = TAttribute<EVisibility>::Create([BusHandle]()
	{
		UObject* Bus = nullptr;
		BusHandle->GetValue(Bus);
		return Bus && Bus->IsA<USoundControlBus>() ? EVisibility::Visible : EVisibility::Hidden;
	});

	ChildBuilder.AddProperty(BusHandle);

	TSharedRef<IPropertyHandle> LinearValueHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSoundModulationMixValue, TargetValue)).ToSharedRef();
	TSharedRef<IPropertyHandle> UnitValueHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSoundModulationMixValue, TargetUnitValue)).ToSharedRef();

	// When editor opens, set unit value in case bus unit has changed while editor was closed.
	AudioModulationEditorUtils::HandleConvertLinearToUnit(BusHandle, LinearValueHandle, UnitValueHandle);

	LinearValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([BusHandle, LinearValueHandle, UnitValueHandle]()
	{
		AudioModulationEditorUtils::HandleConvertLinearToUnit(BusHandle, LinearValueHandle, UnitValueHandle);
	}));

	UnitValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([BusHandle, UnitValueHandle, LinearValueHandle]()
	{
		AudioModulationEditorUtils::HandleConvertUnitToLinear(BusHandle, UnitValueHandle, LinearValueHandle);
		AudioModulationEditorUtils::HandleConvertLinearToUnit(BusHandle, LinearValueHandle, UnitValueHandle);
	}));

	ChildBuilder.AddCustomRow(StructPropertyHandle->GetPropertyDisplayName())
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("BusModulationValue_MixValueName", "Value"))
		.ToolTipText(StructPropertyHandle->GetToolTipText())
	]
	.ValueContent()
		.MinDesiredWidth(300.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.FillWidth(0.4f)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					UnitValueHandle->CreatePropertyValueWidget()
				]
			+ SHorizontalBox::Slot()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(TAttribute<FText>::Create([BusHandle]()
					{
						USoundModulationParameter* Parameter = AudioModulationEditorUtils::GetParameterFromBus(BusHandle);
						if (Parameter && Parameter->RequiresUnitConversion())
						{
							return Parameter->Settings.UnitDisplayName;
						}

						return FText();
					}))
				]
			+ SHorizontalBox::Slot()
				.FillWidth(0.4f)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					LinearValueHandle->CreatePropertyValueWidget()
				]
			+ SHorizontalBox::Slot()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(LOCTEXT("SoundModulationControl_UnitValueLinear", "Linear"))
				]
	]
	.Visibility(TAttribute<EVisibility>::Create([BusHandle]()
	{
		if (USoundModulationParameter* Parameter = AudioModulationEditorUtils::GetParameterFromBus(BusHandle))
		{
			return Parameter->RequiresUnitConversion() ? EVisibility::Visible : EVisibility::Hidden;
		}

		return EVisibility::Hidden;
	}));

	ChildBuilder.AddProperty(LinearValueHandle)
	.Visibility(TAttribute<EVisibility>::Create([BusHandle]()
	{
		if (USoundModulationParameter* Parameter = AudioModulationEditorUtils::GetParameterFromBus(BusHandle))
		{
			return Parameter->RequiresUnitConversion() ? EVisibility::Hidden : EVisibility::Visible;
		}

		return EVisibility::Visible;
	}));

	TSharedRef<IPropertyHandle> AttackTimeHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSoundModulationMixValue, AttackTime)).ToSharedRef();
	ChildBuilder.AddProperty(AttackTimeHandle);

	TSharedRef<IPropertyHandle> ReleaseTimeHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSoundModulationMixValue, ReleaseTime)).ToSharedRef();
	ChildBuilder.AddProperty(ReleaseTimeHandle);
}
#undef LOCTEXT_NAMESPACE // SoundModulationFloat
