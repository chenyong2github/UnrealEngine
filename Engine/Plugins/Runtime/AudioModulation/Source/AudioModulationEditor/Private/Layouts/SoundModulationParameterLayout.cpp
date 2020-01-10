// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationParameterLayout.h"

#include "AudioModulationSettings.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "IAudioExtensionPlugin.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"


#define LOCTEXT_NAMESPACE "SoundModulationParameter"
void FSoundModulationParameterLayoutCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Property handle here is the base struct. We are going to hide it since we're showing it's properties directly.
// 	PropertyHandle->MarkHiddenByCustomization();
}

void FSoundModulationParameterLayoutCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
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

	FName SetControlName;

	TSharedPtr<IPropertyHandle> ControlProperty = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationParameter, Control));
	ControlProperty->GetValue(SetControlName);

	TSharedPtr<IPropertyHandle>ValueHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationParameter, Value));
	ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FSoundModulationParameterLayoutCustomization::OnValueChanged, StructPropertyHandle));
	ValueHandle->SetToolTipText(StructPropertyHandle->GetToolTipText());

	ChildBuilder.AddCustomRow(StructPropertyHandle->GetPropertyDisplayName())
	.NameContent()
	[
		SNew(STextBlock)
		.Text(StructPropertyHandle->GetPropertyDisplayName())
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(280)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.FillWidth(0.35f)
		.Padding(2.0f, 0.0f)
		[
			ValueHandle->CreatePropertyValueWidget()
		]
		+ SHorizontalBox::Slot()
		.FillWidth(0.65f)
		[
			SAssignNew(ControlComboBox, SSoundModulationControlComboBox)
			.InitControlName(SetControlName)
			.ControlNameProperty(ControlProperty)
		]
	];
}

void FSoundModulationParameterLayoutCustomization::OnValueChanged(TSharedRef<IPropertyHandle> InStructPropertyHandle)
{
	// Potentially accessing the value while garbage collecting or saving the package could trigger a crash.
	// so we fail to get the value when that is occurring.
	if (GIsSavingPackage || IsGarbageCollecting())
	{
		return;
	}

	// Way to enforce value was set by editor and remains in-bounds but allows each callsite to set and check
	// at runtime (as desired being that input bus values can be out-of-bounds by design)
	if (InStructPropertyHandle->IsValidHandle())
	{
		TArray<void*> RawData;
		InStructPropertyHandle->AccessRawData(RawData);
		for (void* RawPtr : RawData)
		{
			if (RawPtr)
			{
				FSoundModulationParameter& ModParam = *reinterpret_cast<FSoundModulationParameter*>(RawPtr);
				ModParam.SetBounds(ModParam.GetMinValue(), ModParam.GetMaxValue());
			}
		}
	}
}
#undef LOCTEXT_NAMESPACE
