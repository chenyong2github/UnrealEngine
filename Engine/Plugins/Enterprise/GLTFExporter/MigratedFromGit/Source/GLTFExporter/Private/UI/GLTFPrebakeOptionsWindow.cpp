// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "UI/GLTFPrebakeOptionsWindow.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Interfaces/IMainFrameModule.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "SGLTFPrebakeOptionsWindow"

SGLTFPrebakeOptionsWindow::SGLTFPrebakeOptionsWindow() : bUserCancelled(true)
{

}

void SGLTFPrebakeOptionsWindow::Construct(const FArguments& InArgs)
{
	WidgetWindow = InArgs._WidgetWindow;

	// Retrieve property editor module and create a SDetailsView
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;

	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	// Set provided objects on SDetailsView
	DetailsView->SetObjects(InArgs._SettingsObjects, true);

	this->ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(2)
		.MaxHeight(500.0f)
		[
			DetailsView->AsShared()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(2)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(2)
			+ SUniformGridPanel::Slot(0, 0)
			[
				SAssignNew(ConfirmButton, SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("MaterialBakeOptionWindow_Import", "Confirm"))
				.OnClicked(this, &SGLTFPrebakeOptionsWindow::OnConfirm)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("MaterialBakeOptionWindow_Cancel", "Cancel"))
				.ToolTipText(LOCTEXT("MaterialBakeOptionWindow_Cancel_ToolTip", "Cancels baking out Material"))
				.OnClicked(this, &SGLTFPrebakeOptionsWindow::OnCancel)
			]
		]
	];
}

FReply SGLTFPrebakeOptionsWindow::OnConfirm()
{
	bUserCancelled = false;
	if (WidgetWindow.IsValid())
	{
		WidgetWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SGLTFPrebakeOptionsWindow::OnCancel()
{
	if (WidgetWindow.IsValid())
	{
		WidgetWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SGLTFPrebakeOptionsWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		return OnCancel();
	}

	return FReply::Unhandled();
}

bool SGLTFPrebakeOptionsWindow::WasUserCancelled()
{
	return bUserCancelled;
}

bool SGLTFPrebakeOptionsWindow::ShowDialog(UGLTFPrebakeOptions* PrebakeOptions)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Material Baking Options"))
		.SizingRule(ESizingRule::Autosized);

	TArray<TWeakObjectPtr<UObject>> OptionObjects = { PrebakeOptions };
	TSharedPtr<SGLTFPrebakeOptionsWindow> Options;

	Window->SetContent
	(
		SAssignNew(Options, SGLTFPrebakeOptionsWindow)
		.WidgetWindow(Window)
		.SettingsObjects(OptionObjects)
	);

	TSharedPtr<SWindow> ParentWindow;
	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
		FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);
		return !Options->WasUserCancelled();
	}

	return false;
}

#undef LOCTEXT_NAMESPACE //"SGLTFPrebakeOptionsWindow"

#endif
