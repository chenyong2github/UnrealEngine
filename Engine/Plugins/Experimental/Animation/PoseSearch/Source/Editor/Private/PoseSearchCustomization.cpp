// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchCustomization.h"
#include "PoseSearch/PoseSearch.h"
#include "Engine/GameViewportClient.h"
#include "AssetRegistry/AssetData.h"
#include "EditorClassUtils.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Engine/EngineBaseTypes.h"
#include "UObject/UnrealType.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "IDetailChildrenBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "IDetailsView.h"
#include "ObjectEditorUtils.h"
#include "Animation/AnimSequence.h"

#define LOCTEXT_NAMESPACE "PoseSearchCustomization"




///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// FPoseSearchDatabaseGroupCustomization

void FPoseSearchDatabaseGroupCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FText NamePropertyText;

	TArray<UObject*> Objects;
	InStructPropertyHandle->GetOuterObjects(Objects);
	if (Objects.Num() == 1)
	{
		FPoseSearchDatabaseGroup* PoseSearchDatabaseGroup = (FPoseSearchDatabaseGroup*)InStructPropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);
		NamePropertyText = FText::FromString(PoseSearchDatabaseGroup->Tag.ToString());
	}
	else
	{
		TSharedPtr<IPropertyHandle> NamePropertyHandle = InStructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPoseSearchDatabaseGroup, Tag));
		NamePropertyHandle->GetValueAsFormattedText(NamePropertyText);
	}

	HeaderRow
	.NameContent()
	[
		SNew(STextBlock)
		.Text(NamePropertyText) 
	]
	.ValueContent()
	[
		InStructPropertyHandle->CreatePropertyValueWidget(false)
	];

	const FSimpleDelegate OnValueChanged = FSimpleDelegate::CreateLambda([&StructCustomizationUtils]()
	{
		StructCustomizationUtils.GetPropertyUtilities()->ForceRefresh();
	});

	InStructPropertyHandle->SetOnChildPropertyValueChanged(OnValueChanged);
}

void FPoseSearchDatabaseGroupCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	uint32 NumChildren;
	InStructPropertyHandle->GetNumChildren(NumChildren);
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedPtr<IPropertyHandle> ChildPropertyHandle = InStructPropertyHandle->GetChildHandle(ChildIndex);
		StructBuilder.AddProperty(ChildPropertyHandle.ToSharedRef());
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// FPoseSearchDatabaseSequenceCustomization

void FPoseSearchDatabaseSequenceCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FText SequenceNameText;
	FString GroupsString; // = TEXT("Groups: ");

	TArray<UObject*> Objects;
	InStructPropertyHandle->GetOuterObjects(Objects);

	if (Objects.Num() == 1)
	{
		FPoseSearchDatabaseSequence* PoseSearchDatabaseSequence = (FPoseSearchDatabaseSequence*)InStructPropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);
		check(PoseSearchDatabaseSequence);
		
		if (PoseSearchDatabaseSequence->Sequence)
		{
			SequenceNameText = FText::FromName(PoseSearchDatabaseSequence->Sequence->GetFName());
		}
		else
		{
			SequenceNameText = LOCTEXT("NewSequenceLabel", "New Sequence");
		}
		
		const int32 NumGroups = PoseSearchDatabaseSequence->GroupTags.Num();
		if (NumGroups == 0)
		{
			GroupsString.Append(TEXT("Default"));
		}
		else
		{
			for (int i = 0; i < NumGroups; ++i)
			{
				GroupsString.Append(PoseSearchDatabaseSequence->GroupTags.GetByIndex(i).ToString());
				if (i < NumGroups - 1)
				{
					GroupsString.Append(TEXT(" | "));
				}
			}
		}
	}

	HeaderRow
	.NameContent()
	[
		SNew(STextBlock)
		.Text(SequenceNameText)
	]
	.ValueContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
			.Text(FText::FromString(GroupsString))
		]
	];

	const FSimpleDelegate OnValueChanged = FSimpleDelegate::CreateLambda([&StructCustomizationUtils]()
	{
		StructCustomizationUtils.GetPropertyUtilities()->ForceRefresh();
	});

	InStructPropertyHandle->SetOnChildPropertyValueChanged(OnValueChanged);
}

void FPoseSearchDatabaseSequenceCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	uint32 NumChildren;
	InStructPropertyHandle->GetNumChildren(NumChildren);

	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		StructBuilder.AddProperty(InStructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef());
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// FPoseSearchDatabaseDetails

TSharedRef<IDetailCustomization> FPoseSearchDatabaseDetails::MakeInstance()
{
	return MakeShareable(new FPoseSearchDatabaseDetails);
}

void FPoseSearchDatabaseDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TSharedPtr<IPropertyHandle>> HiddenHandles;
	HiddenHandles.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UPoseSearchDatabase, Sequences)));
	HiddenHandles.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UPoseSearchDatabase, BlendSpaces)));
	HiddenHandles.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UPoseSearchDatabase, Groups)));
	HiddenHandles.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UPoseSearchDatabase, SimpleSequences)));
	HiddenHandles.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UPoseSearchDatabase, SimpleBlendSpaces)));

	for (TSharedPtr<IPropertyHandle> PropertyHandle : HiddenHandles)
	{
		DetailBuilder.HideProperty(PropertyHandle);
	}
}

#undef LOCTEXT_NAMESPACE

