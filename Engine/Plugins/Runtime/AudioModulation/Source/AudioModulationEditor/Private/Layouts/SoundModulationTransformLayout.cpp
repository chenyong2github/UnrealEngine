// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationTransformLayout.h"

#include "Audio.h"
#include "AudioModulationStyle.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "InputCoreTypes.h"
#include "PropertyRestriction.h"
#include "SCurveEditor.h"
#include "ScopedTransaction.h"
#include "SoundModulationPatch.h"
#include "SoundModulationTransform.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"


#define LOCTEXT_NAMESPACE "SoundModulationOutputTransform"
void FSoundModulationOutputTransformLayoutCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

void FSoundModulationOutputTransformLayoutCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
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

	TSharedRef<IPropertyHandle>InputMinHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, InputMin)).ToSharedRef();
	ChildBuilder.AddProperty(InputMinHandle);

	TSharedRef<IPropertyHandle>InputMaxHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, InputMax)).ToSharedRef();
	ChildBuilder.AddProperty(InputMaxHandle);

	TSharedRef<IPropertyHandle>CurveHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, Curve)).ToSharedRef();
	ChildBuilder.AddProperty(CurveHandle);

	TSharedRef<IPropertyHandle> ScalarHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, Scalar)).ToSharedRef();
	const TArray<FString> ScalarFilters = TArray<FString>({ TEXT("Exp"), TEXT("Log") });

	ChildBuilder.AddProperty(ScalarHandle)
		.EditCondition(TAttribute<bool>::Create([this, CurveHandle, ScalarFilters]() { return IsScaleableCurve(CurveHandle, ScalarFilters); }), nullptr)
		.Visibility(TAttribute<EVisibility>::Create([this, CurveHandle, ScalarFilters]() { return IsScaleableCurve(CurveHandle, ScalarFilters) ? EVisibility::Visible : EVisibility::Hidden; }));

	TSharedRef<IPropertyHandle> SharedCurveHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, CurveShared)).ToSharedRef();
	ChildBuilder.AddProperty(SharedCurveHandle)
		.EditCondition(TAttribute<bool>::Create([this, CurveHandle, ScalarFilters]() { return IsSharedCurve(CurveHandle); }), nullptr)
		.Visibility(TAttribute<EVisibility>::Create([this, CurveHandle, ScalarFilters]() { return IsSharedCurve(CurveHandle) ? EVisibility::Visible : EVisibility::Hidden; }));

	TSharedRef<IPropertyHandle>OutputMinHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, OutputMin)).ToSharedRef();
	ChildBuilder.AddProperty(OutputMinHandle);

	TSharedRef<IPropertyHandle>OutputMaxHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, OutputMax)).ToSharedRef();
	ChildBuilder.AddProperty(OutputMaxHandle);
}

bool FSoundModulationOutputTransformLayoutCustomization::IsScaleableCurve(TSharedPtr<IPropertyHandle> CurveHandle, const TArray<FString>& Filters) const
{
	if (!CurveHandle.IsValid())
	{
		return false;
	}

	FString CurveString;
	CurveHandle->GetValueAsFormattedString(CurveString);
	for (const FString& Filter : Filters)
	{
		if (CurveString.Contains(*Filter))
		{
			return true;
		}
	}

	return false;
}

bool FSoundModulationOutputTransformLayoutCustomization::IsSharedCurve(TSharedPtr<IPropertyHandle> CurveHandle) const
{
	if (!CurveHandle.IsValid())
	{
		return false;
	}

	FString CurveString;
	CurveHandle->GetValueAsFormattedString(CurveString);
	return CurveString == TEXT("Shared");
}
#undef LOCTEXT_NAMESPACE
