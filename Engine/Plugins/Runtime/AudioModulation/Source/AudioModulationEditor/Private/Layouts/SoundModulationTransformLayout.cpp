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

	TSharedRef<IPropertyHandle>CurveHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, Curve)).ToSharedRef();
	ChildBuilder.AddProperty(CurveHandle);

	TSharedRef<IPropertyHandle> ScalarHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, Scalar)).ToSharedRef();
	const TArray<ESoundModulatorOutputCurve> ScalarFilters = { ESoundModulatorOutputCurve::Exp, ESoundModulatorOutputCurve::Log };

	ChildBuilder.AddProperty(ScalarHandle)
		.EditCondition(TAttribute<bool>::Create([this, CurveHandle, ScalarFilters]() { return IsScaleableCurve(CurveHandle, ScalarFilters); }), nullptr)
		.Visibility(TAttribute<EVisibility>::Create([this, CurveHandle, ScalarFilters]() { return IsScaleableCurve(CurveHandle, ScalarFilters) ? EVisibility::Visible : EVisibility::Hidden; }));

	TSharedRef<IPropertyHandle> SharedCurveHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, CurveShared)).ToSharedRef();
	ChildBuilder.AddProperty(SharedCurveHandle)
		.EditCondition(TAttribute<bool>::Create([this, CurveHandle, ScalarFilters]() { return IsSharedCurve(CurveHandle); }), nullptr)
		.Visibility(TAttribute<EVisibility>::Create([this, CurveHandle, ScalarFilters]() { return IsSharedCurve(CurveHandle) ? EVisibility::Visible : EVisibility::Hidden; }));
}

bool FSoundModulationOutputTransformLayoutCustomization::IsScaleableCurve(TSharedPtr<IPropertyHandle> CurveHandle, const TArray<ESoundModulatorOutputCurve>& Filters) const
{
	if (!CurveHandle.IsValid())
	{
		return false;
	}

	uint8 CurveString;
	CurveHandle->GetValue(CurveString);
	for (ESoundModulatorOutputCurve Filter : Filters)
	{
		if (CurveString == static_cast<uint8>(Filter))
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

	uint8 CurveValue;
	CurveHandle->GetValue(CurveValue);
	return CurveValue == static_cast<uint8>(ESoundModulatorOutputCurve::Shared);
}
#undef LOCTEXT_NAMESPACE
