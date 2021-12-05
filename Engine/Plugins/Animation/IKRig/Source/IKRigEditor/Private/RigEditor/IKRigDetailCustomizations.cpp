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
		LOCTEXT("ReferenceBoneTransformTooltip", "The reference transform of a bone")
	};

	static TSharedRef<TArray<EIKRigTransformType::Type>> VisibleTransforms =
		MakeShareable(new TArray<EIKRigTransformType::Type>({EIKRigTransformType::Current}));

	TArray<TSharedRef<IPropertyHandle>> Properties;
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UIKRigBoneDetails, CurrentTransform)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UIKRigBoneDetails, ReferenceTransform)));

	for(TSharedRef<IPropertyHandle> Property : Properties)
	{
		DetailBuilder.HideProperty(Property);
	}

	TSharedPtr<SSegmentedControl<EIKRigTransformType::Type>> TransformChoiceWidget;

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
			SAssignNew(TransformChoiceWidget, SSegmentedControl<EIKRigTransformType::Type>)
			.SupportsMultiSelection(true)
			.Values_Lambda([]()
			{
				return VisibleTransforms.Get();
			})
			.OnValuesChanged_Lambda([](TArray<EIKRigTransformType::Type> Values)
			{
				VisibleTransforms.Get() = Values;
			})
			+ SSegmentedControl<EIKRigTransformType::Type>::Slot(EIKRigTransformType::Current)
			.Text(ButtonLabels[0])
			.ToolTip(ButtonTooltips[0])
			+ SSegmentedControl<EIKRigTransformType::Type>::Slot(EIKRigTransformType::Reference)
			.Text(ButtonLabels[1])
			.ToolTip(ButtonTooltips[1])
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

		TransformWidgetArgs
		.OnGetIsComponentRelative_UObject(BoneDetails, &UIKRigBoneDetails::IsComponentRelative, TransformType)
		.OnIsComponentRelativeChanged_UObject(BoneDetails, &UIKRigBoneDetails::OnComponentRelativeChanged, TransformType)
		.Transform_UObject(BoneDetails, &UIKRigBoneDetails::GetTransform, TransformType);
		
		IDetailGroup& Group = CategoryBuilder.AddGroup(*ButtonLabels[PropertyIndex].ToString(), ButtonLabels[PropertyIndex], false, true);
		Group.HeaderRow()
		.Visibility( TAttribute<EVisibility>::CreateLambda([TransformChoiceWidget, TransformType]() -> EVisibility
		{
			return TransformChoiceWidget->HasValue(TransformType) ? EVisibility::Visible : EVisibility::Collapsed;
		}))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(ButtonLabels[PropertyIndex])
			.ToolTipText(ButtonTooltips[PropertyIndex])
		];

		Group.AddPropertyRow(Properties[PropertyIndex])
		.CustomWidget()
		.NameContent()
		.HAlign(HAlign_Fill)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructLabel(TransformWidgetArgs, ESlateTransformComponent::Location)
		]
		.ValueContent()
		.MinDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		.MaxDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructWidget(TransformWidgetArgs, ESlateTransformComponent::Location)
		];

		Group.AddPropertyRow(Properties[PropertyIndex])
		.CustomWidget()
		.NameContent()
		.HAlign(HAlign_Fill)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructLabel(TransformWidgetArgs, ESlateTransformComponent::Rotation)
		]
		.ValueContent()
		.MinDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		.MaxDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructWidget(TransformWidgetArgs, ESlateTransformComponent::Rotation)
		];

		Group.AddPropertyRow(Properties[PropertyIndex])
		.CustomWidget()
		.NameContent()
		.HAlign(HAlign_Fill)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructLabel(TransformWidgetArgs, ESlateTransformComponent::Scale)
		]
		.ValueContent()
		.MinDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		.MaxDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructWidget(TransformWidgetArgs, ESlateTransformComponent::Scale)
		];
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
		LOCTEXT("CurrentBoneTransformTooltip", "The current transform of the bone"),
		LOCTEXT("ReferenceBoneTransformTooltip", "The reference transform of a bone")
	};

	static TSharedRef<TArray<EIKRigTransformType::Type>> VisibleTransforms =
		MakeShareable(new TArray<EIKRigTransformType::Type>({EIKRigTransformType::Current}));

	TArray<TSharedRef<IPropertyHandle>> Properties;
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UIKRigEffectorGoal, CurrentTransform)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UIKRigEffectorGoal, InitialTransform)));

	for(TSharedRef<IPropertyHandle> Property : Properties)
	{
		DetailBuilder.HideProperty(Property);
	}

	TSharedPtr<SSegmentedControl<EIKRigTransformType::Type>> TransformChoiceWidget;

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
			SAssignNew(TransformChoiceWidget, SSegmentedControl<EIKRigTransformType::Type>)
			.SupportsMultiSelection(true)
			.Values_Lambda([]()
			{
				return VisibleTransforms.Get();
			})
			.OnValuesChanged_Lambda([](TArray<EIKRigTransformType::Type> Values)
			{
				VisibleTransforms.Get() = Values;
			})
			+ SSegmentedControl<EIKRigTransformType::Type>::Slot(EIKRigTransformType::Current)
			.Text(ButtonLabels[0])
			.ToolTip(ButtonTooltips[0])
			+ SSegmentedControl<EIKRigTransformType::Type>::Slot(EIKRigTransformType::Reference)
			.Text(ButtonLabels[1])
			.ToolTip(ButtonTooltips[1])
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

		TransformWidgetArgs
		.OnGetNumericValue_UObject(EffectorGoal, &UIKRigEffectorGoal::GetNumericValue, TransformType);
		
		IDetailGroup& Group = CategoryBuilder.AddGroup(*ButtonLabels[PropertyIndex].ToString(), ButtonLabels[PropertyIndex], false, true);
		Group.HeaderRow()
		.Visibility( TAttribute<EVisibility>::CreateLambda([TransformChoiceWidget, TransformType]() -> EVisibility
		{
			return TransformChoiceWidget->HasValue(TransformType) ? EVisibility::Visible : EVisibility::Collapsed;
		}))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(ButtonLabels[PropertyIndex])
			.ToolTipText(ButtonTooltips[PropertyIndex])
		];

		Group.AddPropertyRow(Properties[PropertyIndex])
		.OverrideResetToDefault(FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateUObject(EffectorGoal, &UIKRigEffectorGoal::TransformDiffersFromDefault, ESlateTransformComponent::Location),
			FResetToDefaultHandler::CreateUObject(EffectorGoal, &UIKRigEffectorGoal::ResetTransformToDefault, ESlateTransformComponent::Location)))
		.CustomWidget()
		.NameContent()
		.HAlign(HAlign_Fill)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructLabel(TransformWidgetArgs, ESlateTransformComponent::Location)
		]
		.ValueContent()
		.MinDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		.MaxDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructWidget(TransformWidgetArgs, ESlateTransformComponent::Location)
		];

		Group.AddPropertyRow(Properties[PropertyIndex])
		.OverrideResetToDefault(FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateUObject(EffectorGoal, &UIKRigEffectorGoal::TransformDiffersFromDefault, ESlateTransformComponent::Rotation),
			FResetToDefaultHandler::CreateUObject(EffectorGoal, &UIKRigEffectorGoal::ResetTransformToDefault, ESlateTransformComponent::Rotation)))
		.CustomWidget()
		.NameContent()
		.HAlign(HAlign_Fill)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructLabel(TransformWidgetArgs, ESlateTransformComponent::Rotation)
		]
		.ValueContent()
		.MinDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		.MaxDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructWidget(TransformWidgetArgs, ESlateTransformComponent::Rotation)
		];

		Group.AddPropertyRow(Properties[PropertyIndex])
		.OverrideResetToDefault(FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateUObject(EffectorGoal, &UIKRigEffectorGoal::TransformDiffersFromDefault, ESlateTransformComponent::Scale),
			FResetToDefaultHandler::CreateUObject(EffectorGoal, &UIKRigEffectorGoal::ResetTransformToDefault, ESlateTransformComponent::Scale)))
		.CustomWidget()
		.NameContent()
		.HAlign(HAlign_Fill)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructLabel(TransformWidgetArgs, ESlateTransformComponent::Scale)
		]
		.ValueContent()
		.MinDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		.MaxDesiredWidth(IKRigDetailCustomizationsConstants::ItemWidth * 3.0f)
		[
			SAdvancedTransformInputBox<FTransform>::ConstructWidget(TransformWidgetArgs, ESlateTransformComponent::Scale)
		];
	}
}

#undef LOCTEXT_NAMESPACE
