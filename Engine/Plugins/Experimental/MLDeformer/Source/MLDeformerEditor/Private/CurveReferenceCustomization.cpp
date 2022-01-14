// Copyright Epic Games, Inc. All Rights Reserved.

#include "CurveReferenceCustomization.h"
#include "CurveReference.h"
#include "SCurvePickerWidget.h"
#include "MLDeformerAsset.h"

TSharedRef<IPropertyTypeCustomization> FCurveReferenceCustomization::MakeInstance()
{
	return MakeShareable(new FCurveReferenceCustomization());
}

void FCurveReferenceCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	SetPropertyHandle(StructPropertyHandle);
	SetSkeleton(StructPropertyHandle);

	if (CurveNameProperty->IsValidHandle())
	{
		HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(0.0f)
		[
			SNew(SCurveSelectionWidget)
			.OnCurveSelectionChanged(this, &FCurveReferenceCustomization::OnCurveSelectionChanged)
			.OnGetSelectedCurve(this, &FCurveReferenceCustomization::OnGetSelectedCurve)
			.OnGetSkeleton(this, &FCurveReferenceCustomization::OnGetSkeleton)
		];
	}
}

void FCurveReferenceCustomization::SetSkeleton(TSharedRef<IPropertyHandle> StructPropertyHandle) 
{
	Skeleton = nullptr;

	TArray<UObject*> Objects;
	StructPropertyHandle->GetOuterObjects(Objects);
	check(Objects.Num() == 1);

	UMLDeformerAsset* DeformerAsset = Cast<UMLDeformerAsset>(Objects[0]);
	check(DeformerAsset);

	// Get the skeleton.
	bool bInvalidSkeletonIsError = false;
	Skeleton = DeformerAsset->GetSkeleton(bInvalidSkeletonIsError, nullptr);
}

TSharedPtr<IPropertyHandle> FCurveReferenceCustomization::FindStructMemberProperty(TSharedRef<IPropertyHandle> PropertyHandle, const FName& PropertyName)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 ChildIdx = 0; ChildIdx < NumChildren; ++ChildIdx)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(ChildIdx);
		if (ChildHandle->GetProperty()->GetFName() == PropertyName)
		{
			return ChildHandle;
		}
	}

	return TSharedPtr<IPropertyHandle>();
}

void FCurveReferenceCustomization::SetPropertyHandle(TSharedRef<IPropertyHandle> StructPropertyHandle)
{
	CurveNameProperty = FindStructMemberProperty(StructPropertyHandle, GET_MEMBER_NAME_CHECKED(FCurveReference, CurveName));
	check(CurveNameProperty->IsValidHandle());
}

void FCurveReferenceCustomization::OnCurveSelectionChanged(const FString& Name)
{
	CurveNameProperty->SetValue(Name);
}

FString FCurveReferenceCustomization::OnGetSelectedCurve() const
{
	FString CurveName;
	CurveNameProperty->GetValue(CurveName);
	return CurveName;
}

USkeleton* FCurveReferenceCustomization::OnGetSkeleton() const
{
	return Skeleton;
}
