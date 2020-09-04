// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageImportOptionsWindow.h"

#include "USDStageImportOptions.h"

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IMainFrameModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "USDStageImportOptionsWindow"

bool SUsdOptionsWindow::ShowImportOptions(UUsdStageImportOptions& ImportOptions)
{
	TSharedPtr<SWindow> ParentWindow;

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("USDImportSettings", "USD Import Options"))
		.SizingRule(ESizingRule::Autosized);


	TSharedPtr<SUsdOptionsWindow> OptionsWindow;
	Window->SetContent
	(
		SAssignNew(OptionsWindow, SUsdOptionsWindow)
		.ImportOptions(&ImportOptions)
		.WidgetWindow(Window)
	);

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	return OptionsWindow->ShouldImport();
}

void SUsdOptionsWindow::Construct(const FArguments& InArgs)
{
	ImportOptions = InArgs._ImportOptions;
	Window = InArgs._WidgetWindow;
	bShouldImport = false;

	TSharedPtr<SBox> DetailsViewBox;
	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SAssignNew(DetailsViewBox, SBox)
			.MaxDesiredHeight(450.0f)
			.MinDesiredWidth(550.0f)
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(2)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(2)

			+SUniformGridPanel::Slot(0, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("USDOptionWindow_Import", "Import"))
				.OnClicked(this, &SUsdOptionsWindow::OnImport)
			]

			+SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("USDOptionWindow_Cancel", "Cancel"))
				.ToolTipText(LOCTEXT("USDOptionWindow_Cancel_ToolTip", "Cancels importing this USD file"))
				.OnClicked(this, &SUsdOptionsWindow::OnCancel)
			]
		]
	];

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	TSharedPtr<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	DetailsViewBox->SetContent(DetailsView.ToSharedRef());
	DetailsView->SetObject(ImportOptions);
}

bool SUsdOptionsWindow::SupportsKeyboardFocus() const
{
	return true;
}

FReply SUsdOptionsWindow::OnImport()
{
	bShouldImport = true;
	if (Window.IsValid())
	{
		Window.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SUsdOptionsWindow::OnCancel()
{
	bShouldImport = false;
	if (Window.IsValid())
	{
		Window.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SUsdOptionsWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		return OnCancel();
	}

	return FReply::Unhandled();
}

bool SUsdOptionsWindow::ShouldImport() const
{
	return bShouldImport;
}

#undef LOCTEXT_NAMESPACE