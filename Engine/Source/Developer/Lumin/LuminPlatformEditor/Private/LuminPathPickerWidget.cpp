// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminPathPickerWidget.h"
#include "EditorDirectories.h"
#include "DesktopPlatformModule.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "SLuminPathPickerWidget"

void SLuminPathPickerWidget::Construct(const FArguments& Args)
{
	FilePickerTitle = Args._FilePickerTitle.Get();
	FileFilter = Args._FileFilter.Get();
	CurrDirectory = Args._StartDirectory;
	OnPickPath = Args._OnPickPath.Get();
	OnClearPath = Args._OnClearPath.Get();

	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(Args._PathLabel.Get())
				.Margin(2.0f)
			]
		]

		+ SHorizontalBox::Slot()
		.MaxWidth(300.0f)
		[
			SNew(STextBlock)
			.ToolTipText(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([this]()->FText { return FText::FromString(CurrDirectory.Get()); })))
			.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([this]()->FText { return FText::FromString(CurrDirectory.Get()); })))
			.Margin(2.0f)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ToolTipText(Args._ToolTipText)
			.ContentPadding(2.0f)
			.ForegroundColor(FSlateColor::UseForeground())
			.IsFocusable(false)
			.OnClicked(this, &SLuminPathPickerWidget::OnPickDirectory)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ToolTipText(LOCTEXT("ClearSelectedDirectory", "Clear the selected directory."))
			.ContentPadding(2.0f)
			.ForegroundColor(FSlateColor::UseForeground())
			.IsFocusable(false)
			.OnClicked(this, &SLuminPathPickerWidget::OnClearSelectedDirectory)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("PropertyWindow.Button_Clear"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]
	];
}

FReply SLuminPathPickerWidget::OnPickDirectory()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		void* ParentWindowHandle = (ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid()) ? ParentWindow->GetNativeWindow()->GetOSWindowHandle() : nullptr;
		FString StartDirectory = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT);
		if (FileFilter.Len() > 0)
		{
			TArray<FString> OutFiles;
			if (DesktopPlatform->OpenFileDialog(
				ParentWindowHandle,
				FilePickerTitle,
				TEXT(""),
				TEXT(""),
				FileFilter,
				EFileDialogFlags::None,
				OutFiles))
			{
				return OnPickPath.Execute(OutFiles[0]);
			}
		}
		else
		{
			FString SelectedDirectory;
			if (DesktopPlatform->OpenDirectoryDialog(
				ParentWindowHandle,
				FilePickerTitle,
				StartDirectory,
				SelectedDirectory))
			{
				FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, SelectedDirectory);
				if (OnPickPath.Execute(SelectedDirectory).IsEventHandled())
				{
					CurrDirectory.Set(SelectedDirectory);
					return FReply::Handled();
				}
			}
		}
	}

	return FReply::Unhandled();
}

FReply SLuminPathPickerWidget::OnClearSelectedDirectory()
{
	CurrDirectory.Set(TEXT(""));
	return OnClearPath.Execute();
}

#undef LOCTEXT_NAMESPACE

