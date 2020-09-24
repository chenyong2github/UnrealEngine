// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomAttributeCustomization.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWidget.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/CustomAttributes.h"
#include "Misc/AssertionMacros.h"
#include "Misc/Variant.h"

#include "PropertyHandle.h"
#include "IPropertyTypeCustomization.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "CustomAttributeCustomization"

TSharedRef<IPropertyTypeCustomization> FCustomAttributePerBoneDataCustomization::MakeInstance()
{
	return MakeShareable(new FCustomAttributePerBoneDataCustomization);
}

void FCustomAttributePerBoneDataCustomization::CustomizeHeader(TSharedRef<class IPropertyHandle> InPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& PropertyTypeCustomizationUtils)
{
	// Provide summary of attributes / number of each
	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);

	UAnimSequence* Sequence = nullptr;

	// Try and find the outer sequence the attributea are embedded
	for (UObject* Object : OuterObjects)
	{
		if (UAnimSequence* TempSequence = Cast<UAnimSequence>(Object))
		{
			Sequence = TempSequence;
			break;
		}
	}

	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<IPropertyHandle> BoneIndexHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCustomAttributePerBoneData, BoneTreeIndex));

	// In case we have a valid sequence try and retrieve the actual bone name for the stored bone index
	if (Sequence && Sequence->GetSkeleton() && BoneIndexHandle.IsValid())
	{
		FName BoneName = NAME_None;
		int32 BoneIndexValue = 0;
		if (BoneIndexHandle->GetValue(BoneIndexValue) == FPropertyAccess::Success)
		{
			BoneName = Sequence->GetSkeleton()->GetReferenceSkeleton().GetBoneName(BoneIndexValue);
		}

		NameWidget = SNew(STextBlock)
		.Font(PropertyTypeCustomizationUtils.GetRegularFont())
		.Text(FText::FromName(BoneName));
	}
	else
	{
		NameWidget = InPropertyHandle->CreatePropertyNameWidget();
	}

	HeaderRow.NameContent()
	[
		NameWidget.ToSharedRef()
	];
}

void FCustomAttributePerBoneDataCustomization::CustomizeChildren(TSharedRef<class IPropertyHandle> InPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& PropertyTypeCustomizationUtils)
{
	// Sort through attributes array and group them on a per-type basis	
	TSharedPtr<IPropertyHandle> AttributesHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCustomAttributePerBoneData, Attributes));
	TSharedPtr<IPropertyHandleArray> AttributesArrayHandle = AttributesHandle.IsValid() ? AttributesHandle->AsArray() : nullptr;

	if (AttributesArrayHandle.IsValid())
	{
		uint32 NumElements = 0;
		AttributesArrayHandle->GetNumElements(NumElements);

		if (NumElements)
		{
			IDetailGroup* FloatGroup = nullptr;
			IDetailGroup* IntGroup = nullptr;
			IDetailGroup* StringGroup = nullptr;
			
			// Create the groups on-demand, as to not end up with an empty one
			auto GetOrCreateGroup = [&StructBuilder](FName GroupName, const FText& GroupLabel, IDetailGroup*& GroupPtr)
			{
				if (GroupPtr == nullptr)
				{
					IDetailGroup& NewGroup = StructBuilder.AddGroup(GroupName, GroupLabel);
					GroupPtr = &NewGroup;
				}

				return GroupPtr;
			};
			
			for (uint32 ChildElementIndex = 0; ChildElementIndex < NumElements; ++ChildElementIndex)
			{
				TSharedRef<IPropertyHandle> ChildAttributeHandle = AttributesArrayHandle->GetElement(ChildElementIndex);

				TSharedPtr<IPropertyHandle> VariantTypeHandle = ChildAttributeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCustomAttribute, VariantType));
				int32 VariantTypeInt = 0;
				if (VariantTypeHandle.IsValid() && (VariantTypeHandle->GetValue(VariantTypeInt) == FPropertyAccess::Success))
				{
					const EVariantTypes VariantType = static_cast<EVariantTypes>(VariantTypeInt);
					switch (VariantType)
					{
						case EVariantTypes::Float:
						{
							const FName FloatGroupName("FloatAttributes");
							GetOrCreateGroup(FloatGroupName, LOCTEXT("FloatAttributesLabel", "Float Attributes"), FloatGroup)->AddPropertyRow(ChildAttributeHandle);
							break;
						}

						case EVariantTypes::Int32:
						{
							const FName IntGroupName("IntegerAttributes");
							GetOrCreateGroup(IntGroupName, LOCTEXT("IntAttributesLabel", "Integer Attributes"), IntGroup)->AddPropertyRow(ChildAttributeHandle);
							break;
						}

						case EVariantTypes::String:
						{
							const FName StringGroupName("StringAttributes");
							GetOrCreateGroup(StringGroupName, LOCTEXT("StringAttributesLabel", "String Attributes"), StringGroup)->AddPropertyRow(ChildAttributeHandle);
							break;
						}
					}
				}
			}
		}
	}
}

TSharedRef<IPropertyTypeCustomization> FCustomAttributeCustomization::MakeInstance()
{
	return MakeShareable(new FCustomAttributeCustomization);
}

void FCustomAttributeCustomization::CustomizeHeader(TSharedRef<class IPropertyHandle> InPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& PropertyTypeCustomizationUtils)
{
	TSharedPtr<IPropertyHandle> AttributeNameHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCustomAttribute, Name));
	TSharedPtr<IPropertyHandle> AttributeTimesHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCustomAttribute, Times));

	FName AttributeName = NAME_None;
	AttributeNameHandle->GetValue(AttributeName);

	uint32 NumKeys = 0;
	AttributeTimesHandle->AsArray()->GetNumElements(NumKeys);

	HeaderRow
	.NameContent()
	[
		SNew(STextBlock)
		.Font(PropertyTypeCustomizationUtils.GetRegularFont())
		.Text(FText::FromName(AttributeName))
	]
	.ValueContent()
	[		
		SNew(STextBlock)
		.Font(PropertyTypeCustomizationUtils.GetRegularFont())
		.Text(FText::Format(LOCTEXT("NumKeysFormat", "Number of Keys: {0}"), NumKeys))
	];
}

void FCustomAttributeCustomization::CustomizeChildren(TSharedRef<class IPropertyHandle> InPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& PropertyTypeCustomizationUtils)
{
	// No need to show child properties
}

#undef LOCTEXT_NAMESPACE // "CustomAttributeCustomization"