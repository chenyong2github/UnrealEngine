// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAddContentDialog.h"
#include "Widgets/Layout/SBorder.h"
#include "EditorStyleSet.h"
#include "SAddContentWidget.h"

#define LOCTEXT_NAMESPACE "AddContentDialog"

void SAddContentDialog::Construct(const FArguments& InArgs)
{

	SWindow::Construct(SWindow::FArguments()
		.Title(LOCTEXT("AddContentDialogTitle", "Add Content to the Project"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(900, 500))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("Brushes.Panel"))
			.Padding(FMargin(10,0))
			[
				SNew(SAddContentWidget)
			]
		]);
}

SAddContentDialog::~SAddContentDialog()
{
}

#undef LOCTEXT_NAMESPACE
