// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMPropertyPath.h"

#include "MVVMBlueprintView.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "Widgets/SMVVMFieldIcon.h"
#include "Styling/MVVMEditorStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMVVMPropertyPath"

void SMVVMPropertyPathBase::Construct(const FArguments& InArgs, const UWidgetBlueprint* InWidgetBlueprint)
{
	WidgetBlueprint = InWidgetBlueprint;

	const UE::MVVM::IFieldPathHelper& PathHelper = GetPathHelper();
	SelectedSource = PathHelper.GetSelectedSource();
	SelectedField = PathHelper.GetSelectedField();

	auto GetContextText = [this]()
	{
		if (SelectedSource.IsSet())
		{
			return SelectedSource.GetValue().DisplayName;
		}
		return FText::GetEmpty();
	};

	auto GetPathText = [this]()
	{
		if (SelectedField.IsProperty())
		{
			return SelectedField.GetProperty()->GetDisplayNameText();
		}

		if (SelectedField.IsFunction())
		{
			return SelectedField.GetFunction()->GetDisplayNameText();
		}

		return FText::GetEmpty();
	};

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(STextBlock)
			.TextStyle(FMVVMEditorStyle::Get(), "PropertyPath.ContextText")
			.Text_Lambda(GetContextText)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(5, 0, 0, 0))
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SMVVMFieldIcon)
			.Field(SelectedField)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(2, 0, 5, 0))
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(STextBlock)
			.TextStyle(FMVVMEditorStyle::Get(), "PropertyPath.ContextText")
			.Text_Lambda(GetPathText)
		]
	];
}

void SMVVMWidgetPropertyPath::Construct(const FArguments& InArgs, const UWidgetBlueprint* InWidgetBlueprint)
{
	PathHelper = UE::MVVM::FWidgetFieldPathHelper(InArgs._WidgetPath, InWidgetBlueprint);

	SMVVMPropertyPathBase::Construct(SMVVMPropertyPathBase::FArguments(), InWidgetBlueprint);
}

void SMVVMViewModelPropertyPath::Construct(const FArguments& InArgs, const UWidgetBlueprint* InWidgetBlueprint)
{
	PathHelper = UE::MVVM::FViewModelFieldPathHelper(InArgs._ViewModelPath, InWidgetBlueprint);

	SMVVMPropertyPathBase::Construct(SMVVMPropertyPathBase::FArguments(), InWidgetBlueprint);
}


#undef LOCTEXT_NAMESPACE