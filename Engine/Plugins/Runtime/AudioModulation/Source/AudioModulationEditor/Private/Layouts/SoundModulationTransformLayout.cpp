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
	TSharedRef<IPropertyHandle> ExpScalarHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, Scalar)).ToSharedRef();
	ChildBuilder.AddProperty(ExpScalarHandle);

	TSharedRef<IPropertyHandle>OutputMinHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, OutputMin)).ToSharedRef();
	ChildBuilder.AddProperty(OutputMinHandle);

	TSharedRef<IPropertyHandle>OutputMaxHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationOutputTransform, OutputMax)).ToSharedRef();
	ChildBuilder.AddProperty(OutputMaxHandle);
}
