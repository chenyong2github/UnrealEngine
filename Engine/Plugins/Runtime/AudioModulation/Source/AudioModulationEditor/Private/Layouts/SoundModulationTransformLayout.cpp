// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "SoundModulationTransformLayout.h"

#include "Audio.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PropertyRestriction.h"
#include "SoundModulationTransform.h"
#include "Widgets/Text/STextBlock.h"


FSoundModulationOutputTransformLayoutCustomization::FSoundModulationOutputTransformLayoutCustomization()
{
}

bool FSoundModulationOutputTransformLayoutCustomization::IsCurveSet(TSharedPtr<IPropertyHandle> CurveHandle, const TArray<FString>& Filters) const
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

void FSoundModulationOutputTransformLayoutCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Property handle here is the base struct. We are going to hide it since we're showing it's properties directly.
// 	PropertyHandle->MarkHiddenByCustomization();
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

	TSharedRef<IPropertyHandle> FloatCurveHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, FloatCurve)).ToSharedRef();
	const TArray<FString> CustomFilters = TArray<FString>({ TEXT("Custom") });
	ChildBuilder.AddProperty(FloatCurveHandle)
		.EditCondition(TAttribute<bool>::Create([this, CurveHandle, CustomFilters]() { return IsCurveSet(CurveHandle, CustomFilters); }), nullptr)
		.Visibility(TAttribute<EVisibility>::Create([this, CurveHandle, CustomFilters]() { return IsCurveSet(CurveHandle, CustomFilters) ? EVisibility::Visible : EVisibility::Hidden; }));

	TSharedRef<IPropertyHandle> ExpScalarHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, Scalar)).ToSharedRef();
	const TArray<FString> ScalarFilters = TArray<FString>({ TEXT("Exp"), TEXT("Log") });
	ChildBuilder.AddProperty(ExpScalarHandle)
		.EditCondition(TAttribute<bool>::Create([this, CurveHandle, ScalarFilters]() { return IsCurveSet(CurveHandle, ScalarFilters); }), nullptr)
		.Visibility(TAttribute<EVisibility>::Create([this, CurveHandle, ScalarFilters]() { return IsCurveSet(CurveHandle, ScalarFilters) ? EVisibility::Visible : EVisibility::Hidden; }));

	TSharedRef<IPropertyHandle>OutputMinHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, OutputMin)).ToSharedRef();
	ChildBuilder.AddProperty(OutputMinHandle);

	TSharedRef<IPropertyHandle>OutputMaxHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, OutputMax)).ToSharedRef();
	ChildBuilder.AddProperty(OutputMaxHandle);
}
