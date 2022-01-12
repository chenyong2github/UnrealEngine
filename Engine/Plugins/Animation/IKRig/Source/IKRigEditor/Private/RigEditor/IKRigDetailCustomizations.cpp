// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigDetailCustomizations.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "Widgets/SWidget.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "IKRigDetailCustomizations"

namespace IKRigDetailCustomizationsConstants
{
	static const float ItemWidth = 125.0f;
}

void FIKRigGenericDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized = DetailBuilder.GetSelectedObjects();

	// make sure all types are of the same class
	UClass* DetailsClass = nullptr;
	for(const TWeakObjectPtr<UObject>& ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(!ObjectBeingCustomized.IsValid())
		{
			continue;
		}

		if(DetailsClass)
		{
			if(ObjectBeingCustomized->GetClass() != DetailsClass)
			{
				// multiple different things - fall back to default
				// details panel behavior
				return;
			}
		}
		else
		{
			DetailsClass = ObjectBeingCustomized->GetClass();
		}
	}

	// assuming the classes are all the same 
	for(const TWeakObjectPtr<UObject>& ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(!ObjectBeingCustomized.IsValid())
		{
			continue;
		}

		if(ObjectBeingCustomized->IsA<UIKRigBoneDetails>())
		{
			CustomizeDetailsForClass<UIKRigBoneDetails>(DetailBuilder, ObjectsBeingCustomized);	
		}
		else if(ObjectBeingCustomized->IsA<UIKRigEffectorGoal>())
		{
			CustomizeDetailsForClass<UIKRigEffectorGoal>(DetailBuilder, ObjectsBeingCustomized);	
		}

		break;
	}
}

template <>
void FIKRigGenericDetailCustomization::CustomizeDetailsForClass<UIKRigBoneDetails>(
	IDetailLayoutBuilder& DetailBuilder,
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized)
{
	UIKRigBoneDetails* BoneDetails = Cast<UIKRigBoneDetails>(ObjectsBeingCustomized[0].Get());
	if(BoneDetails == nullptr)
	{
		return;
		
	}
	const TArray<FText> ButtonLabels =
	{
		LOCTEXT("CurrentTransform", "Current"),
		LOCTEXT("ReferenceTransform", "Reference")
	};
	
	const TArray<FText> ButtonTooltips =
	{
		LOCTEXT("CurrentBoneTransformTooltip", "The current transform of the bone"),
		LOCTEXT("ReferenceBoneTransformTooltip", "The reference transform of the bone")
	};

	const TArray<EIKRigTransformType::Type> TransformTypes =
	{
		EIKRigTransformType::Current,
		EIKRigTransformType::Reference
	};

	static TAttribute<TArray<EIKRigTransformType::Type>> VisibleTransforms =
		TArray<EIKRigTransformType::Type>({EIKRigTransformType::Current});

	TArray<TSharedRef<IPropertyHandle>> Properties;
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UIKRigBoneDetails, CurrentTransform)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UIKRigBoneDetails, ReferenceTransform)));

	for(TSharedRef<IPropertyHandle> Property : Properties)
	{
		DetailBuilder.HideProperty(Property);
	}

	TSharedPtr<SSegmentedControl<EIKRigTransformType::Type>> TransformChoiceWidget =
		SSegmentedControl<EIKRigTransformType::Type>::Create(
			TransformTypes,
			ButtonLabels,
			ButtonTooltips,
			VisibleTransforms
		);

	DetailBuilder.EditCategory(TEXT("Selection")).SetSortOrder(1);

	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(TEXT("Transforms"));
	CategoryBuilder.SetSortOrder(2);
	CategoryBuilder.AddCustomRow(FText::FromString(TEXT("TransformType")))
	.ValueContent()
	.MinDesiredWidth(375.f)
	.MaxDesiredWidth(375.f)
	.HAlign(HAlign_Left)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			TransformChoiceWidget.ToSharedRef()
		]
	];

	SAdvancedTransformInputBox<FTransform>::FArguments TransformWidgetArgs = SAdvancedTransformInputBox<FTransform>::FArguments()
	.IsEnabled(false)
	.DisplayRelativeWorld(true)
	.DisplayScaleLock(false)
	.AllowEditRotationRepresentation(true)
	.Font(IDetailLayoutBuilder::GetDetailFont())
	.UseQuaternionForRotation(true);
	
	for(int32 PropertyIndex=0;PropertyIndex<Properties.Num();PropertyIndex++)
	{
		const EIKRigTransformType::Type TransformType = (EIKRigTransformType::Type)PropertyIndex; 

		TransformWidgetArgs.OnGetIsComponentRelative_UObject(BoneDetails, &UIKRigBoneDetails::IsComponentRelative, TransformType);
		TransformWidgetArgs.OnIsComponentRelativeChanged_UObject(BoneDetails, &UIKRigBoneDetails::OnComponentRelativeChanged, TransformType);
		TransformWidgetArgs.Transform_UObject(BoneDetails, &UIKRigBoneDetails::GetTransform, TransformType);
		TransformWidgetArgs.OnCopyToClipboard_UObject(BoneDetails, &UIKRigBoneDetails::OnCopyToClipboard, TransformType);
		TransformWidgetArgs.OnPasteFromClipboard_UObject(BoneDetails, &UIKRigBoneDetails::OnPasteFromClipboard, TransformType);

		TransformWidgetArgs.Visibility_Lambda([TransformChoiceWidget, TransformType]() -> EVisibility
		{
			return TransformChoiceWidget->HasValue(TransformType) ? EVisibility::Visible : EVisibility::Collapsed;
		});

		SAdvancedTransformInputBox<FTransform>::ConstructGroupedTransformRows(
			CategoryBuilder, 
			ButtonLabels[PropertyIndex], 
			ButtonTooltips[PropertyIndex], 
			TransformWidgetArgs);
	}
}

template <>
void FIKRigGenericDetailCustomization::CustomizeDetailsForClass<UIKRigEffectorGoal>(
	IDetailLayoutBuilder& DetailBuilder,
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized)
{
	UIKRigEffectorGoal* EffectorGoal = Cast<UIKRigEffectorGoal>(ObjectsBeingCustomized[0].Get());
	if(EffectorGoal == nullptr)
	{
		return;
		
	}
	const TArray<FText> ButtonLabels =
	{
		LOCTEXT("CurrentTransform", "Current"),
		LOCTEXT("ReferenceTransform", "Reference")
	};
	
	const TArray<FText> ButtonTooltips =
	{
		LOCTEXT("CurrentGoalTransformTooltip", "The current transform of the goal"),
		LOCTEXT("ReferenceGoalTransformTooltip", "The reference transform of the goal")
	};

	const TArray<EIKRigTransformType::Type> TransformTypes =
	{
		EIKRigTransformType::Current,
		EIKRigTransformType::Reference
	};

	static TAttribute<TArray<EIKRigTransformType::Type>> VisibleTransforms =
		TArray<EIKRigTransformType::Type>({EIKRigTransformType::Current});

	TArray<TSharedRef<IPropertyHandle>> Properties;
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UIKRigEffectorGoal, CurrentTransform)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UIKRigEffectorGoal, InitialTransform)));

	for(TSharedRef<IPropertyHandle> Property : Properties)
	{
		DetailBuilder.HideProperty(Property);
	}

	TSharedPtr<SSegmentedControl<EIKRigTransformType::Type>> TransformChoiceWidget =
		SSegmentedControl<EIKRigTransformType::Type>::Create(
			TransformTypes,
			ButtonLabels,
			ButtonTooltips,
			VisibleTransforms
		);

	DetailBuilder.EditCategory(TEXT("Goal Settings")).SetSortOrder(1);
	DetailBuilder.EditCategory(TEXT("Viewport Goal Settings")).SetSortOrder(3);

	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(TEXT("Transforms"));
	CategoryBuilder.SetSortOrder(2);
	CategoryBuilder.AddCustomRow(FText::FromString(TEXT("TransformType")))
	.ValueContent()
	.MinDesiredWidth(375.f)
	.MaxDesiredWidth(375.f)
	.HAlign(HAlign_Left)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			TransformChoiceWidget.ToSharedRef()
		]
	];

	SAdvancedTransformInputBox<FTransform>::FArguments TransformWidgetArgs = SAdvancedTransformInputBox<FTransform>::FArguments()
	.IsEnabled(true)
	.DisplayRelativeWorld(false)
	.DisplayScaleLock(true)
	.AllowEditRotationRepresentation(true)
	.Font(IDetailLayoutBuilder::GetDetailFont())
	.UseQuaternionForRotation(true);
	
	for(int32 PropertyIndex=0;PropertyIndex<Properties.Num();PropertyIndex++)
	{
		const EIKRigTransformType::Type TransformType = (EIKRigTransformType::Type)PropertyIndex;

		if(PropertyIndex > 0)
		{
			TransformWidgetArgs
			.IsEnabled(false)
			.DisplayScaleLock(false);
		}
		
		TransformWidgetArgs.OnGetNumericValue_UObject(EffectorGoal, &UIKRigEffectorGoal::GetNumericValue, TransformType);
		TransformWidgetArgs.OnNumericValueChanged_UObject(EffectorGoal, &UIKRigEffectorGoal::OnNumericValueChanged, ETextCommit::Default, TransformType);
		TransformWidgetArgs.OnNumericValueCommitted_UObject(EffectorGoal, &UIKRigEffectorGoal::OnNumericValueChanged, TransformType);

		TransformWidgetArgs.OnCopyToClipboard_UObject(EffectorGoal, &UIKRigEffectorGoal::OnCopyToClipboard, TransformType);
		TransformWidgetArgs.OnPasteFromClipboard_UObject(EffectorGoal, &UIKRigEffectorGoal::OnPasteFromClipboard, TransformType);

		const TSharedPtr<IPropertyHandle> PropertyHandle = Properties[PropertyIndex];
		TransformWidgetArgs.DiffersFromDefault_UObject(EffectorGoal, &UIKRigEffectorGoal::TransformDiffersFromDefault, PropertyHandle);
		TransformWidgetArgs.OnResetToDefault_UObject(EffectorGoal, &UIKRigEffectorGoal::ResetTransformToDefault, PropertyHandle);

		TransformWidgetArgs.Visibility_Lambda([TransformChoiceWidget, TransformType]() -> EVisibility
		{
			return TransformChoiceWidget->HasValue(TransformType) ? EVisibility::Visible : EVisibility::Collapsed;
		});

		SAdvancedTransformInputBox<FTransform>::ConstructGroupedTransformRows(
			CategoryBuilder, 
			ButtonLabels[PropertyIndex], 
			ButtonTooltips[PropertyIndex], 
			TransformWidgetArgs);
	}
}

#undef LOCTEXT_NAMESPACE
