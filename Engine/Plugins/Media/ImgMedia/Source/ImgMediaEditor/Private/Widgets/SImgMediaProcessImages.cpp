// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SImgMediaProcessImages.h"

#include "EditorStyleSet.h"
#include "ImgMediaProcessImagesOptions.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SlateOptMacros.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ImgMediaProcessImages"

SImgMediaProcessImages::~SImgMediaProcessImages()
{
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SImgMediaProcessImages::Construct(const FArguments& InArgs)
{
	// Set up widgets.
	TSharedPtr<SBox> DetailsViewBox;
	
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
			.Padding(0, 20, 0, 0)
			.AutoHeight()

		// Add details view.
		+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(DetailsViewBox, SBox)
			]
			
		// Add process images button.
		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 5, 0, 0)
			[
				SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.ForegroundColor(FSlateColor::UseForeground())
					.OnClicked(this, &SImgMediaProcessImages::OnProcessImagesClicked)
					.Text(LOCTEXT("StartProcessImages", "ProcessImages"))
					.ToolTipText(LOCTEXT("ProcesssImagesButtonToolTip", "Start processing images."))
			]
	];

	// Create object with our options.
	Options = TStrongObjectPtr<UImgMediaProcessImagesOptions>(NewObject<UImgMediaProcessImagesOptions>(GetTransientPackage(), NAME_None));

	// Create detail view with our options.
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(Options.Get());

	DetailsViewBox->SetContent(DetailsView->AsShared());
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FReply SImgMediaProcessImages::OnProcessImagesClicked()
{
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
