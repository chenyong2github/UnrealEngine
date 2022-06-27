// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMPropertyPath.h"

#include "Blueprint/WidgetTree.h"
#include "Editor.h"
#include "MVVMBlueprintView.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "Styling/MVVMEditorStyle.h"
#include "Widgets/SMVVMFieldIcon.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMVVMPropertyPath"

void SMVVMPropertyPath::Construct(const FArguments& InArgs)
{
	WidgetBlueprint = InArgs._WidgetBlueprint;
	check(InArgs._WidgetBlueprint != nullptr);
	PropertyPath = InArgs._PropertyPath;
	check(PropertyPath);

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(STextBlock)
			.TextStyle(FMVVMEditorStyle::Get(), "PropertyPath.ContextText")
			.Text(this, &SMVVMPropertyPath::GetSourceDisplayName)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(5, 0, 0, 0))
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SMVVMFieldIcon)
			.Field(GetLastField())
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(2, 0, 5, 0))
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(STextBlock)
			.TextStyle(FMVVMEditorStyle::Get(), "PropertyPath.ContextText")
			.Text(this, &SMVVMPropertyPath::GetFieldDisplayName)
		]
	];
}

FText SMVVMPropertyPath::GetSourceDisplayName() const
{
	if (PropertyPath->IsFromWidget())
	{
		const UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(PropertyPath->GetWidgetName());
		return FText::FromString(Widget->GetDisplayLabel());
	}
	else if (PropertyPath->IsFromViewModel())
	{
		const UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
		if (const UMVVMBlueprintView* View = Subsystem->GetView(WidgetBlueprint.Get()))
		{
			const FMVVMBlueprintViewModelContext* ViewModel = View->FindViewModel(PropertyPath->GetViewModelId());
			return ViewModel->GetDisplayName();
		}
	}
	return FText::GetEmpty();
}

FText SMVVMPropertyPath::GetFieldDisplayName() const
{
	return FText::FromString(PropertyPath->GetBasePropertyPath());
}

UE::MVVM::FMVVMConstFieldVariant SMVVMPropertyPath::GetLastField() const
{
	return PropertyPath->GetFields().Last();
}

#undef LOCTEXT_NAMESPACE