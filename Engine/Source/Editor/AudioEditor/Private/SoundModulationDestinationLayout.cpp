// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationDestinationLayout.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioEditorModule.h"
#include "Delegates/Delegate.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "IAudioModulation.h"
#include "Logging/LogMacros.h"
#include "Misc/Attribute.h"
#include "Sound/SoundModulationDestination.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SoundModulationParameter"

namespace ModDestinationLayoutUtils
{
	IAudioModulation* GetEditorModulationInterface()
	{
		if (GEditor)
		{
			if (UWorld* World = GEditor->GetEditorWorldContext().World())
			{
				FAudioDeviceHandle AudioDeviceHandle = World->GetAudioDevice();
				if (AudioDeviceHandle.IsValid() && AudioDeviceHandle->IsModulationPluginEnabled())
				{
					return AudioDeviceHandle->ModulationInterface.Get();
				}
			}
		}

		return nullptr;
	}

	bool IsModulationEnabled()
	{
		return GetEditorModulationInterface() != nullptr;
	}

	FName GetParameterNameFromMetaData(const TSharedRef<IPropertyHandle>& InHandle)
	{
		static const FName AudioParamFieldName("AudioParam");

		if (InHandle->HasMetaData(AudioParamFieldName))
		{
			FString ParamString = InHandle->GetMetaData(AudioParamFieldName);
			return FName(ParamString);
		}

		return FName();
	}

	bool IsParamMismatched(TSharedRef<IPropertyHandle> ModulatorHandle, TSharedRef<IPropertyHandle> StructPropertyHandle, FName* OutModParamName = nullptr, FName* OutDestParamName = nullptr)
	{
		if (OutModParamName)
		{
			*OutModParamName = FName();
		}

		if (OutDestParamName)
		{
			*OutDestParamName = FName();
		}

		UObject* ModObject = nullptr;
		ModulatorHandle->GetValue(ModObject);

		USoundModulatorBase* ModBase = Cast<USoundModulatorBase>(ModObject);
		if (!ModBase)
		{
			return false;
		}

		if (IAudioModulation* ModulationInterface = ModDestinationLayoutUtils::GetEditorModulationInterface())
		{
			const FName ModParamName = ModBase->GetOutputParameterName();
			const FName DestParamName = ModDestinationLayoutUtils::GetParameterNameFromMetaData(StructPropertyHandle);
			if (ModParamName != FName() && DestParamName != FName() && ModParamName != DestParamName)
			{
				if (OutModParamName)
				{
					*OutModParamName = ModParamName;
				}

				if (OutDestParamName)
				{
					*OutDestParamName = DestParamName;
				}
				return true;
			}
		}

		return false;
	}

	FText SetMetaData(TSharedRef<IPropertyHandle> StructPropertyHandle, TSharedRef<IPropertyHandle> ValueHandle, FText& OutUnitDisplayText, FName& OutParamName)
	{
		bool bClampValuesSet = false;
		float ClampMinValue = 0.0f;
		float ClampMaxValue = 1.0f;
		float UIMinValue = 0.0f;
		float UIMaxValue = 1.0f;
		if (StructPropertyHandle->HasMetaData("ClampMin"))
		{
			bClampValuesSet = true;
			FString ParamString = StructPropertyHandle->GetMetaData("ClampMin");
			ClampMinValue = FCString::Atof(*ParamString);
		}

		if (StructPropertyHandle->HasMetaData("ClampMax"))
		{
			FString ParamString = StructPropertyHandle->GetMetaData("ClampMax");
			ClampMaxValue = FCString::Atof(*ParamString);
			bClampValuesSet = true;
		}

		OutParamName = ModDestinationLayoutUtils::GetParameterNameFromMetaData(StructPropertyHandle);
		if (OutParamName != FName())
		{
			// If parameter was provided, it overrides ClampMin/Max.  User data however overrides UIMin/Max if its
			// in clamp range.
			if (IAudioModulation* ModulationInterface = ModDestinationLayoutUtils::GetEditorModulationInterface())
			{
				Audio::FModulationParameter Parameter = ModulationInterface->GetParameter(OutParamName);
				UIMinValue = Parameter.MinValue;
				UIMaxValue = Parameter.MaxValue;
				ClampMinValue = UIMinValue;
				ClampMaxValue = UIMaxValue;
				OutUnitDisplayText = Parameter.UnitDisplayName;
				if (bClampValuesSet)
				{
					UE_LOG(LogAudioEditor, Warning, TEXT("ClampMin/Max overridden by AudioModulation plugin asset with ParamName '%s'."), *OutParamName.ToString());
				}
			}

			if (StructPropertyHandle->HasMetaData("UIMin"))
			{
				float NewMin = UIMinValue;
				FString ParamString = StructPropertyHandle->GetMetaData("UIMin");
				NewMin = FCString::Atof(*ParamString);
				UIMinValue = FMath::Clamp(NewMin, ClampMinValue, ClampMaxValue);
			}

			if (StructPropertyHandle->HasMetaData("UIMax"))
			{
				float NewMax = UIMaxValue;
				FString ParamString = StructPropertyHandle->GetMetaData("UIMax");
				NewMax = FCString::Atof(*ParamString);
				UIMaxValue = FMath::Clamp(NewMax, ClampMinValue, ClampMaxValue);
			}
		}

		ValueHandle->SetInstanceMetaData("ClampMin", FString::Printf(TEXT("%f"), ClampMinValue));
		ValueHandle->SetInstanceMetaData("ClampMax", FString::Printf(TEXT("%f"), ClampMaxValue));
		ValueHandle->SetInstanceMetaData("UIMin", FString::Printf(TEXT("%f"), UIMinValue));
		ValueHandle->SetInstanceMetaData("UIMax", FString::Printf(TEXT("%f"), UIMaxValue));
		
		return OutUnitDisplayText;
	}

	void CustomizeChildren_AddValueRow(
		IDetailChildrenBuilder& ChildBuilder,
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		TSharedRef<IPropertyHandle> ValueHandle,
		TSharedRef<IPropertyHandle> ModulatorHandle,
		TSharedRef<IPropertyHandle> EnablementHandle)
	{
		FText UnitDisplayText = FText::GetEmpty();
		FName ParamName;
		SetMetaData(StructPropertyHandle, ValueHandle, UnitDisplayText, ParamName);

		const FText DisplayName = StructPropertyHandle->GetPropertyDisplayName();
		ChildBuilder.AddCustomRow(DisplayName)
		.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(DisplayName)
				.ToolTipText(StructPropertyHandle->GetToolTipText())
			]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					ValueHandle->CreatePropertyValueWidget()
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(UnitDisplayText)
					.ToolTipText(ValueHandle->GetToolTipText())
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					EnablementHandle->CreatePropertyValueWidget()
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					EnablementHandle->CreatePropertyNameWidget()
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ToolTipText(LOCTEXT("ResetToParameterDefaultToolTip", "Reset to parameter's default"))
					.ButtonStyle(FEditorStyle::Get(), TEXT("NoBorder"))
					.ContentPadding(0.0f)
					.Visibility(TAttribute<EVisibility>::Create([ParamName, ValueHandle]
					{
						float CurrentValue = 0.0f;
						ValueHandle->GetValue(CurrentValue);

						if (IAudioModulation* ModulationInterface = ModDestinationLayoutUtils::GetEditorModulationInterface())
						{
							Audio::FModulationParameter Parameter = ModulationInterface->GetParameter(ParamName);
								
							return Parameter.DefaultValue == CurrentValue
								? EVisibility::Hidden
								: EVisibility::Visible;
						}

						return EVisibility::Hidden;
					}))
					.OnClicked(FOnClicked::CreateLambda([ParamName, ValueHandle]()
					{
						if (IAudioModulation* ModulationInterface = ModDestinationLayoutUtils::GetEditorModulationInterface())
						{
							Audio::FModulationParameter Parameter = ModulationInterface->GetParameter(ParamName);
							ValueHandle->SetValue(Parameter.DefaultValue);
						}

						return FReply::Handled();
					}))
					.Content()
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
					]
				]
		];

		EnablementHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([EnablementHandle, ValueHandle, StructPropertyHandle, ModulatorHandle]()
		{
			bool bEnabled = false;
			EnablementHandle->GetValue(bEnabled);
			if (bEnabled)
			{
				Audio::FModulationParameter Parameter;

				if (IAudioModulation* ModulationInterface = ModDestinationLayoutUtils::GetEditorModulationInterface())
				{
					const FName ParamName = ModDestinationLayoutUtils::GetParameterNameFromMetaData(StructPropertyHandle);
					Parameter = ModulationInterface->GetParameter(ParamName);
				}

				ValueHandle->SetValue(Parameter.DefaultValue);
			}
			else
			{
				UObject* NullObj = nullptr;
				ModulatorHandle->SetValue(NullObj);
			}
		}));
	}

	void CustomizeChildren_AddValueNoModRow(IDetailChildrenBuilder& ChildBuilder, TSharedRef<IPropertyHandle> StructPropertyHandle, TSharedRef<IPropertyHandle> ValueHandle)
	{
		FText UnitDisplayText = FText::GetEmpty();
		FName ParamName;
		SetMetaData(StructPropertyHandle, ValueHandle, UnitDisplayText, ParamName);

		const FText DisplayName = StructPropertyHandle->GetPropertyDisplayName();
		FDetailWidgetRow& ValueNoModRow = ChildBuilder.AddCustomRow(DisplayName);
		ValueNoModRow.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(DisplayName)
			.ToolTipText(StructPropertyHandle->GetToolTipText())
		]
		.ValueContent()
		.MinDesiredWidth(120.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					ValueHandle->CreatePropertyValueWidget()
				]
		];
	}

	void CustomizeChildren_AddModulatorRow(IDetailChildrenBuilder& ChildBuilder, TSharedRef<IPropertyHandle> StructPropertyHandle, TSharedRef<IPropertyHandle> ModulatorHandle, TSharedRef<IPropertyHandle> EnablementHandle)
	{
		const FText DisplayName = StructPropertyHandle->GetPropertyDisplayName();
		ChildBuilder.AddCustomRow(DisplayName)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(FText::Format(LOCTEXT("SoundModulationParameter_ModulatorFormat", "{0} Modulator"), DisplayName))
				.ToolTipText(ModulatorHandle->GetToolTipText())
			]
			.ValueContent()
			.MinDesiredWidth(200.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					ModulatorHandle->CreatePropertyValueWidget()
				]
			]
			.Visibility(TAttribute<EVisibility>::Create([EnablementHandle]()
			{
				bool bModulationEnabled = false;
				EnablementHandle->GetValue(bModulationEnabled);
				return bModulationEnabled 
					? EVisibility::Visible
					: EVisibility::Hidden;
			}));

		ChildBuilder.AddCustomRow(LOCTEXT("SoundModulationDestinationLayout_UnitMismatchHeadingWarning", "Unit Mismatch Warning"))
			.ValueContent()
			.MinDesiredWidth(150.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(10.0f, 0.0f, 0.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
						.Text(TAttribute<FText>::Create([ModulatorHandle, StructPropertyHandle]()
						{
							UObject* ModObject = nullptr;
							ModulatorHandle->GetValue(ModObject);

							UObject* DestObject = nullptr;
							
							FName ModName;
							FName DestName;
							if (ModDestinationLayoutUtils::IsParamMismatched(ModulatorHandle, StructPropertyHandle, &ModName, &DestName))
							{
								return FText::Format(LOCTEXT("ModulationDestinationLayout_UnitMismatchFormat", "Parameter Mismatch: Modulator Output = {0}, Destination Input = {1}"),
									FText::FromName(ModName),
									FText::FromName(DestName));
							}

							return FText::GetEmpty();

						}))
					]
			]
			.Visibility(TAttribute<EVisibility>::Create([ModulatorHandle, StructPropertyHandle]()
			{
				return ModDestinationLayoutUtils::IsParamMismatched(ModulatorHandle, StructPropertyHandle)
					? EVisibility::Visible
					: EVisibility::Hidden;
			}));
	}

	void SetBoundsMetaData(FName FieldName, float InDefault, const TSharedRef<IPropertyHandle>& InHandle, TSharedRef<IPropertyHandle>& OutHandle)
	{
		if (InHandle->HasMetaData(FieldName))
		{
			const FString& Value = InHandle->GetMetaData(FieldName);
			OutHandle->SetInstanceMetaData(FieldName, Value);
		}
		else
		{
			OutHandle->SetInstanceMetaData(FieldName, FString::Printf(TEXT("%f"), InDefault));
		}
	}
} // namespace ModParamLayoutUtils

void FSoundModulationDestinationLayoutCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

void FSoundModulationDestinationLayoutCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TMap<FName, TSharedPtr<IPropertyHandle>> PropertyHandles;
	uint32 NumChildren;
	StructPropertyHandle->GetNumChildren(NumChildren);

	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedRef<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
		const FName PropertyName = ChildHandle->GetProperty()->GetFName();
		PropertyHandles.Add(PropertyName, ChildHandle);
	}

	TSharedRef<IPropertyHandle>EnablementHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDestinationSettings, bEnableModulation)).ToSharedRef();
	TSharedRef<IPropertyHandle>ModulatorHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDestinationSettings, Modulator)).ToSharedRef();
	TSharedRef<IPropertyHandle>ValueHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDestinationSettings, Value)).ToSharedRef();

	if (ModDestinationLayoutUtils::IsModulationEnabled())
	{
		ModDestinationLayoutUtils::CustomizeChildren_AddValueRow(ChildBuilder, StructPropertyHandle, ValueHandle, ModulatorHandle, EnablementHandle);
		ModDestinationLayoutUtils::CustomizeChildren_AddModulatorRow(ChildBuilder, StructPropertyHandle, ModulatorHandle, EnablementHandle);
	}
	else
	{
		ModDestinationLayoutUtils::CustomizeChildren_AddValueNoModRow(ChildBuilder, StructPropertyHandle, ValueHandle);
	}
}

void FSoundModulationDefaultSettingsLayoutCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

void FSoundModulationDefaultSettingsLayoutCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	if (ModDestinationLayoutUtils::IsModulationEnabled())
	{
		TMap<FName, TSharedPtr<IPropertyHandle>> PropertyHandles;
		uint32 NumChildren;
		StructPropertyHandle->GetNumChildren(NumChildren);

		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			TSharedRef<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
			const FName PropertyName = ChildHandle->GetProperty()->GetFName();
			PropertyHandles.Add(PropertyName, ChildHandle);
		}

		TSharedRef<IPropertyHandle> VolumeHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultSettings, VolumeModulationDestination)).ToSharedRef();
		TSharedRef<IPropertyHandle> PitchHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultSettings, PitchModulationDestination)).ToSharedRef();
		TSharedRef<IPropertyHandle> HighpassHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultSettings, HighpassModulationDestination)).ToSharedRef();
		TSharedRef<IPropertyHandle> LowpassHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultSettings, LowpassModulationDestination)).ToSharedRef();

		ChildBuilder.AddProperty(VolumeHandle);
		ChildBuilder.AddProperty(PitchHandle);
		ChildBuilder.AddProperty(HighpassHandle);
		ChildBuilder.AddProperty(LowpassHandle);
	}
}

void FSoundModulationDefaultRoutingSettingsLayoutCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

void FSoundModulationDefaultRoutingSettingsLayoutCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	if (ModDestinationLayoutUtils::IsModulationEnabled())
	{
		TMap<FName, TSharedPtr<IPropertyHandle>> PropertyHandles;
		uint32 NumChildren;
		StructPropertyHandle->GetNumChildren(NumChildren);

		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			TSharedRef<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
			const FName PropertyName = ChildHandle->GetProperty()->GetFName();
			PropertyHandles.Add(PropertyName, ChildHandle);
		}

		TSharedRef<IPropertyHandle> VolumeRouting = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultRoutingSettings, VolumeRouting)).ToSharedRef();
		TSharedRef<IPropertyHandle> VolumeHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultRoutingSettings, VolumeModulationDestination)).ToSharedRef();

		TSharedRef<IPropertyHandle> PitchRouting = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultRoutingSettings, PitchRouting)).ToSharedRef();
		TSharedRef<IPropertyHandle> PitchHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultRoutingSettings, PitchModulationDestination)).ToSharedRef();

		TSharedRef<IPropertyHandle> HighpassRouting = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultRoutingSettings, HighpassRouting)).ToSharedRef();
		TSharedRef<IPropertyHandle> HighpassHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultRoutingSettings, HighpassModulationDestination)).ToSharedRef();

		TSharedRef<IPropertyHandle> LowpassRouting = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultRoutingSettings, LowpassRouting)).ToSharedRef();
		TSharedRef<IPropertyHandle> LowpassHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationDefaultRoutingSettings, LowpassModulationDestination)).ToSharedRef();

		auto ShowModSettings = [] (TSharedRef<IPropertyHandle> RoutingHandle)
		{
			return TAttribute<EVisibility>::Create([RoutingHandle]()
			{
				uint8 RoutingValue = 0;
				if (RoutingHandle->GetValue(RoutingValue) != FPropertyAccess::Success)
				{
					return EVisibility::Hidden;
				}

				if (static_cast<EModulationRouting>(RoutingValue) != EModulationRouting::Override)
				{
					return EVisibility::Hidden;
				}

				return EVisibility::Visible;
			});
		};

		ChildBuilder.AddProperty(VolumeRouting);
		ChildBuilder.AddProperty(VolumeHandle).Visibility(ShowModSettings(VolumeRouting));
		ChildBuilder.AddProperty(PitchRouting);
		ChildBuilder.AddProperty(PitchHandle).Visibility(ShowModSettings(PitchRouting));
		ChildBuilder.AddProperty(HighpassRouting);
		ChildBuilder.AddProperty(HighpassHandle).Visibility(ShowModSettings(HighpassRouting));
		ChildBuilder.AddProperty(LowpassRouting);
		ChildBuilder.AddProperty(LowpassHandle).Visibility(ShowModSettings(LowpassRouting));
	}
}
#undef LOCTEXT_NAMESPACE
