// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailsCustomizations/UVTransformToolCustomizations.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "IDetailChildrenBuilder.h"
#include "Internationalization/BreakIterator.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "SAssetView.h"

#include "PropertyRestriction.h"

#include "UVEditorTransformTool.h"


#define LOCTEXT_NAMESPACE "UVEditorDetailsCustomization"

namespace UVEditorDetailsCustomizationLocal
{
	void CustomSortTransformToolCategories(const TMap<FName, IDetailCategoryBuilder*>&  AllCategoryMap )
	{
		(*AllCategoryMap.Find(FName("Quick Translate")))->SetSortOrder(0);
		(*AllCategoryMap.Find(FName("Quick Rotate")))->SetSortOrder(1);
		(*AllCategoryMap.Find(FName("Quick Transform")))->SetSortOrder(2);
	}
}


//
// UVEditorTransformTool
//


TSharedRef<IDetailCustomization> FUVEditorUVTransformToolDetails::MakeInstance()
{
	return MakeShareable(new FUVEditorUVTransformToolDetails);
}


void FUVEditorUVTransformToolDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	using namespace UVEditorDetailsCustomizationLocal;

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	check(ObjectsBeingCustomized.Num() > 0);
	UUVEditorUVQuickTransformProperties* QuickTransformProperties = CastChecked<UUVEditorUVQuickTransformProperties>(ObjectsBeingCustomized[0]);
	UUVEditorTransformTool* Tool = QuickTransformProperties->Tool.Get();
	TargetTool = Tool;

	BuildQuickTranslateMenu(DetailBuilder);
	BuildQuickRotateMenu(DetailBuilder);

	DetailBuilder.SortCategories(&CustomSortTransformToolCategories);
}

void FUVEditorUVTransformToolDetails::BuildQuickTranslateMenu(IDetailLayoutBuilder& DetailBuilder)
{
	const FName QuickTranslateCategoryName = "Quick Translate";
	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(QuickTranslateCategoryName);	

	TSharedPtr<IPropertyHandle> QuickTranslateOffsetHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUVEditorUVQuickTransformProperties, QuickTranslateOffset), UUVEditorUVQuickTransformProperties::StaticClass());
	ensure(QuickTranslateOffsetHandle->IsValidHandle());
	QuickTranslateOffsetHandle->MarkHiddenByCustomization();

	auto ApplyTranslation = [this, QuickTranslateOffsetHandle](const FVector2D& Direction)
	{
		float TranslationValue;
		QuickTranslateOffsetHandle->GetValue(TranslationValue);
		return OnQuickTranslate(TranslationValue, Direction);
	};

	FDetailWidgetRow& CustomRow = CategoryBuilder.AddCustomRow(FText::GetEmpty())
	[
		SNew(SUniformGridPanel)
		.SlotPadding(FMargin(5.0f))
		+ SUniformGridPanel::Slot(0, 0)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickMoveTopLeft", "TL"))
			.ToolTipText(LOCTEXT("QuickMoveTopLeftToolTip", "Applies translation offset in the negative X axis and the positive Y axis"))
			.OnClicked_Lambda([ApplyTranslation]() { return ApplyTranslation(FVector2D(-1.0, 1.0)); })			
		]
		+ SUniformGridPanel::Slot(1, 0)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickMoveTop", "T"))
			.ToolTipText(LOCTEXT("QuickMoveTopToolTip", "Applies the translation offset in the positive Y axis."))
			.OnClicked_Lambda([ApplyTranslation]() { return ApplyTranslation(FVector2D(0.0, 1.0)); })
		]
		+ SUniformGridPanel::Slot(2, 0)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickMoveTopRight", "TR"))
			.ToolTipText(LOCTEXT("QuickMoveTopRightToolTip", "Applies the translation offset in the positive X axis and the positive Y axis."))
			.OnClicked_Lambda([ApplyTranslation]() { return ApplyTranslation(FVector2D(1.0, 1.0)); })
		]
		+ SUniformGridPanel::Slot(0, 1)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickMoveLeft", "L"))
			.ToolTipText(LOCTEXT("QuickMoveLeftToolTip", "Applies translation offset in the negative X axis."))
			.OnClicked_Lambda([ApplyTranslation]() { return ApplyTranslation(FVector2D(-1.0, 0.0)); })
		]
		+ SUniformGridPanel::Slot(1, 1)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SProperty, QuickTranslateOffsetHandle)
			.ShouldDisplayName(false)
		]
		+ SUniformGridPanel::Slot(2, 1)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickMoveRight", "R"))
			.ToolTipText(LOCTEXT("QuickMoveRightToolTip", "Applies the translation offset in the positive X axis."))
			.OnClicked_Lambda([ApplyTranslation]() { return ApplyTranslation(FVector2D(1.0, 0.0)); })
		]
		+ SUniformGridPanel::Slot(0, 2)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickMoveBottomLeft", "BL"))
			.ToolTipText(LOCTEXT("QuickMoveBottomLeftToolTip", "Applies translation offset in the negative X axis and the negative Y axis"))
			.OnClicked_Lambda([ApplyTranslation]() { return ApplyTranslation(FVector2D(-1.0, -1.0)); })
		]
		+ SUniformGridPanel::Slot(1, 2)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickMoveBottom", "B"))
			.ToolTipText(LOCTEXT("QuickMoveBottomToolTip", "Applies the translation offset in the negative Y axis."))
			.OnClicked_Lambda([ApplyTranslation]() { return ApplyTranslation(FVector2D(0.0, -1.0)); })
		]
		+ SUniformGridPanel::Slot(2, 2)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickMoveBottomRight", "BR"))
			.ToolTipText(LOCTEXT("QuickMoveBottomRightToolTip", "Applies the translation offset in the positive X axis and the negative Y axis."))
			.OnClicked_Lambda([ApplyTranslation]() { return ApplyTranslation(FVector2D(1.0, -1.0)); })
		]

	];

}


void FUVEditorUVTransformToolDetails::BuildQuickRotateMenu(IDetailLayoutBuilder& DetailBuilder)
{
	const FName QuickRotateCategoryName = "Quick Rotate";
	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(QuickRotateCategoryName);

	TSharedPtr<IPropertyHandle> QuickRotationOffsetHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUVEditorUVQuickTransformProperties, QuickRotationOffset), UUVEditorUVQuickTransformProperties::StaticClass());
	ensure(QuickRotationOffsetHandle->IsValidHandle());
	QuickRotationOffsetHandle->MarkHiddenByCustomization();

	FDetailWidgetRow& CustomRow = CategoryBuilder.AddCustomRow(FText::GetEmpty())
	[
		SNew(SUniformGridPanel)
		.SlotPadding(FMargin(5.0f))
		+ SUniformGridPanel::Slot(0, 0)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickRotateClockwise", "CW"))
			.ToolTipText(LOCTEXT("QuickRotateClockwiseToolTip", "Applies the rotation in a clockwise orientation"))
			.OnClicked_Lambda([this, QuickRotationOffsetHandle]() {
				float RotationValue;
				QuickRotationOffsetHandle->GetValue(RotationValue);
				return OnQuickRotate(-RotationValue); 
			})
		]
		+ SUniformGridPanel::Slot(1, 0)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SProperty, QuickRotationOffsetHandle)
			.ShouldDisplayName(false)
		]
		+ SUniformGridPanel::Slot(2, 0)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickRotateCounterclockwise", "CCW"))
			.ToolTipText(LOCTEXT("QuickRotateCounterclockwiseToolTip", "Applies the rotation in a counter clockwise orientation"))
			.OnClicked_Lambda([this, QuickRotationOffsetHandle]() {
				float RotationValue;
				QuickRotationOffsetHandle->GetValue(RotationValue);
				return OnQuickRotate(RotationValue); 
			})
		]

		+ SUniformGridPanel::Slot(0, 1)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickRotateClockwise10Deg", "10°"))
			.ToolTipText(LOCTEXT("QuickRotateClockwise10DegToolTip", "Applies a 10 degree clockwise orientation"))
			.OnClicked_Lambda([this]() { return OnQuickRotate(-10.0); })
		]
		+ SUniformGridPanel::Slot(2, 1)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickRotateCounterclockwise10Deg", "10°"))
			.ToolTipText(LOCTEXT("QuickRotateCounterclockwise10DegToolTip", "Applies a 10 degree counter clockwise orientation"))
			.OnClicked_Lambda([this]() { return OnQuickRotate(10.0); })
		]
		+ SUniformGridPanel::Slot(0, 2)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickRotateClockwise45Deg", "45°"))
			.ToolTipText(LOCTEXT("QuickRotateClockwise45DegToolTip", "Applies a 45 degree clockwise orientation"))
			.OnClicked_Lambda([this]() { return OnQuickRotate(-45.0); })
		]
		+ SUniformGridPanel::Slot(2, 2)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickRotateCounterclockwise45Deg", "45°"))
			.ToolTipText(LOCTEXT("QuickRotateCounterclockwise45DegToolTip", "Applies a 45 degree counter clockwise orientation"))
			.OnClicked_Lambda([this]() { return OnQuickRotate(45.0); })
		]
		+ SUniformGridPanel::Slot(0, 3)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickRotateClockwise90Deg", "90°"))
			.ToolTipText(LOCTEXT("QuickRotateClockwise90DegToolTip", "Applies a 90 degree clockwise orientation"))
			.OnClicked_Lambda([this]() { return OnQuickRotate(-90.0); })
		]
		+ SUniformGridPanel::Slot(2, 3)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("QuickRotateCounterclockwise90Deg", "90°"))
			.ToolTipText(LOCTEXT("QuickRotateCounterclockwise90DegToolTip", "Applies a 90 degree counter clockwise orientation"))
			.OnClicked_Lambda([this]() { return OnQuickRotate(90.0); })
		]
	];
}


FReply  FUVEditorUVTransformToolDetails::OnQuickTranslate(float TranslationValue, const FVector2D& Direction)
{
	ensure(TargetTool.IsValid());
	TargetTool->InitiateQuickTranslate(TranslationValue, Direction);
	return FReply::Handled();
}

FReply  FUVEditorUVTransformToolDetails::OnQuickRotate(float RotationValue)
{
	ensure(TargetTool.IsValid());
	TargetTool->InitiateQuickRotation(RotationValue);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE