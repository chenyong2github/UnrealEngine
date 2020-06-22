// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlModulationPatchLayout.h"

#include "AudioModulationSettings.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "IAudioModulation.h"
#include "SoundControlBus.h"
#include "SoundModulationParameterSettingsLayout.h"
#include "SoundModulationPatch.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"


#define LOCTEXT_NAMESPACE "AudioModulation"
namespace AudioModulationEditorUtils
{
	void GetPropertyHandleMap(TSharedRef<IPropertyHandle> StructPropertyHandle, TMap<FName, TSharedPtr<IPropertyHandle>>& PropertyHandles)
	{
		uint32 NumChildren;
		StructPropertyHandle->GetNumChildren(NumChildren);

		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			TSharedRef<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
			const FName PropertyName = ChildHandle->GetProperty()->GetFName();
			PropertyHandles.Add(PropertyName, ChildHandle);
		}
	}

	TArray<USoundControlBus*> GetMismatchedBuses(TSharedRef<IPropertyHandle> InputsHandle, const USoundModulationParameter* InputParameter)
	{
		uint32 NumInputs;
		TSharedPtr<IPropertyHandleArray> InputArray = InputsHandle->AsArray();
		InputArray->GetNumElements(NumInputs);

		if (!InputParameter)
		{
			return TArray<USoundControlBus*>();
		}

		TArray<USoundControlBus*> MismatchBuses;
		for (uint32 i = 0; i < NumInputs; ++i)
		{
			TSharedRef<IPropertyHandle> Input = InputArray->GetElement(static_cast<int32>(i));
			TSharedRef<IPropertyHandle> BusInputHandle = Input->GetChildHandle("Bus", false /* bRecurse */).ToSharedRef();
			UObject* LinkedObj = nullptr;
			if (BusInputHandle->GetValue(LinkedObj) == FPropertyAccess::Success)
			{
				if (USoundControlBus* Bus = Cast<USoundControlBus>(LinkedObj))
				{
					if (Bus->Parameter)
					{
						if (Bus->Parameter != nullptr && Bus->Parameter != InputParameter)
						{
							MismatchBuses.Add(Bus);
						}
					}
				}
			}
		}

		return MismatchBuses;
	}
} // namespace AudioModulationEditorUtils

void FSoundModulationPatchLayoutCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

void FSoundModulationPatchLayoutCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TMap<FName, TSharedPtr<IPropertyHandle>> PropertyHandles;
	AudioModulationEditorUtils::GetPropertyHandleMap(StructPropertyHandle, PropertyHandles);
	CustomizeControl(PropertyHandles, ChildBuilder);
}

TAttribute<EVisibility> FSoundModulationPatchLayoutCustomization::CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder)
{
	TSharedRef<IPropertyHandle>BypassHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationPatchBase, bBypass)).ToSharedRef();
	ChildBuilder.AddProperty(BypassHandle);

	TAttribute<EVisibility> VisibilityAttribute = TAttribute<EVisibility>::Create([this, BypassHandle]()
	{
		bool bIsBypassed = false;
		BypassHandle->GetValue(bIsBypassed);
		return bIsBypassed ? EVisibility::Hidden : EVisibility::Visible;
	});

	return VisibilityAttribute;
}

TAttribute<EVisibility> FSoundVolumeModulationPatchLayoutCustomization::CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder)
{
	TAttribute<EVisibility> VisibilityAttribute = FSoundModulationPatchLayoutCustomization::CustomizeControl(PropertyHandles, ChildBuilder);

	TSharedRef<IPropertyHandle>ValueHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundVolumeModulationPatch, DefaultInputValue)).ToSharedRef();
	ChildBuilder.AddProperty(ValueHandle)
		.Visibility(VisibilityAttribute);

	AddPatchProperties<FSoundVolumeModulationPatch>(VisibilityAttribute, PropertyHandles, ChildBuilder);
	return VisibilityAttribute;
}

TAttribute<EVisibility> FSoundPitchModulationPatchLayoutCustomization::CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder)
{
	TAttribute<EVisibility> VisibilityAttribute = FSoundModulationPatchLayoutCustomization::CustomizeControl(PropertyHandles, ChildBuilder);
	AddPatchProperties<FSoundPitchModulationPatch>(VisibilityAttribute, PropertyHandles, ChildBuilder);
	return VisibilityAttribute;
}

TAttribute<EVisibility> FSoundHPFModulationPatchLayoutCustomization::CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder)
{
	TAttribute<EVisibility> VisibilityAttribute = FSoundModulationPatchLayoutCustomization::CustomizeControl(PropertyHandles, ChildBuilder);
	AddPatchProperties<FSoundHPFModulationPatch>(VisibilityAttribute, PropertyHandles, ChildBuilder);
	return VisibilityAttribute;
}

TAttribute<EVisibility> FSoundLPFModulationPatchLayoutCustomization::CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder)
{
	TAttribute<EVisibility> VisibilityAttribute = FSoundModulationPatchLayoutCustomization::CustomizeControl(PropertyHandles, ChildBuilder);
	AddPatchProperties<FSoundLPFModulationPatch>(VisibilityAttribute, PropertyHandles, ChildBuilder);
	return VisibilityAttribute;
}

TAttribute<EVisibility> FSoundControlModulationPatchLayoutCustomization::CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>>& PropertyHandles, IDetailChildrenBuilder& ChildBuilder)
{
	TSharedRef<IPropertyHandle>BypassHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationPatchBase, bBypass)).ToSharedRef();
	ChildBuilder.AddProperty(BypassHandle);

	TAttribute<EVisibility> BypassedVisibilityAttribute = TAttribute<EVisibility>::Create([this, BypassHandle]()
	{
		bool bIsBypassed = false;
		BypassHandle->GetValue(bIsBypassed);
		return bIsBypassed ? EVisibility::Hidden : EVisibility::Visible;
	});

	TSharedRef<IPropertyHandle> InputsHandle			= PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundControlModulationPatch, Inputs)).ToSharedRef();
	TSharedRef<IPropertyHandle> InputParameterHandle	= PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundControlModulationPatch, InputParameter)).ToSharedRef();
	TSharedRef<IPropertyHandle> OutputParameterHandle	= PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundControlModulationPatch, OutputParameter)).ToSharedRef();
	TSharedRef<IPropertyHandle> TransformHandle			= PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundControlModulationPatch, Transform)).ToSharedRef();

	ChildBuilder.AddProperty(InputParameterHandle)
		.Visibility(BypassedVisibilityAttribute);

	ChildBuilder.AddCustomRow(LOCTEXT("ModulationPatchLayout_UnitMismatchHeadingWarning", "Bus Unit Mismatch Warning"))
		.ValueContent()
		.MinDesiredWidth(150.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(1.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.Text(TAttribute<FText>::Create([InputsHandle, InputParameterHandle]()
					{
						UObject* Object = nullptr;
						InputParameterHandle->GetValue(Object);

						if (USoundModulationParameter* Parameter = Cast<USoundModulationParameter>(Object))
						{
							return FText::Format(
								LOCTEXT("ModulationPatchLayout_UnitMismatchHeading", "{0} bus(es) reference parameter with units that do not match patch input units."),
								FText::AsNumber(AudioModulationEditorUtils::GetMismatchedBuses(InputsHandle, Parameter).Num())
							);
						}

						return FText();
					}))
				]
		]
		.Visibility(TAttribute<EVisibility>::Create([BypassHandle, InputsHandle, InputParameterHandle]()
		{
			bool bIsBypassed = false;
			BypassHandle->GetValue(bIsBypassed);
			if (bIsBypassed)
			{
				return EVisibility::Hidden;
			}

			UObject* Object = nullptr;
			InputParameterHandle->GetValue(Object);

			if (USoundModulationParameter* Parameter = Cast<USoundModulationParameter>(Object))
			{
				const int32 NumMismatched = AudioModulationEditorUtils::GetMismatchedBuses(InputsHandle, Parameter).Num();
				return NumMismatched > 0 ? EVisibility::Visible : EVisibility::Hidden;
			}

			return EVisibility::Hidden;
		}));

	ChildBuilder.AddProperty(InputsHandle)
	.Visibility(BypassedVisibilityAttribute);

	ChildBuilder.AddProperty(TransformHandle)
	.Visibility(BypassedVisibilityAttribute);

	ChildBuilder.AddProperty(OutputParameterHandle);
	return BypassedVisibilityAttribute;
}
#undef LOCTEXT_NAMESPACE
