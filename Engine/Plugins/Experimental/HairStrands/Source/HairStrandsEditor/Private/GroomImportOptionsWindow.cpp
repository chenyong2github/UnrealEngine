// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomImportOptionsWindow.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "GroomImportOptions.h"
#include "IDetailsView.h"
#include "Interfaces/IMainFrameModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "GroomOptions"

void SGroomImportOptionsWindow::Construct(const FArguments& InArgs)
{
	ImportOptions = InArgs._ImportOptions;
	WidgetWindow = InArgs._WidgetWindow;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(ImportOptions);

	this->ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("CurveEd.LabelFont"))
					.Text(LOCTEXT("GroomOptionsWindow_CurrentFile", "Current File: "))
				]
				+ SHorizontalBox::Slot()
				.Padding(5, 0, 0, 0)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("CurveEd.InfoFont"))
					.Text(InArgs._FullPath)
				]
			]
		]

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
				SAssignNew(ImportButton, SButton)
				.HAlign(HAlign_Center)
				.Text(InArgs._ButtonLabel)
				.IsEnabled(this, &SGroomImportOptionsWindow::CanImport)
				.OnClicked(this, &SGroomImportOptionsWindow::OnImport)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("GroomOptionsWindow_Cancel", "Cancel"))
				.OnClicked(this, &SGroomImportOptionsWindow::OnCancel)
			]
		]
	];
}

bool SGroomImportOptionsWindow::CanImport()  const
{
	return true;
}

enum class EGroomOptionsVisibility : uint8
{
	None = 0x00,
	ConversionOptions = 0x01,
	BuildOptions = 0x02,
	All = ConversionOptions | BuildOptions
};

ENUM_CLASS_FLAGS(EGroomOptionsVisibility);

TSharedPtr<SGroomImportOptionsWindow> DisplayOptions(UGroomImportOptions* ImportOptions, const FString& FilePath, EGroomOptionsVisibility VisibilityFlag, FText WindowTitle, FText InButtonLabel)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(WindowTitle)
		.SizingRule(ESizingRule::Autosized);

	TSharedPtr<SGroomImportOptionsWindow> OptionsWindow;

	FProperty* ConversionOptionsProperty = FindField<FProperty>(ImportOptions->GetClass(), GET_MEMBER_NAME_CHECKED(UGroomImportOptions, ConversionSettings));
	if (ConversionOptionsProperty)
	{
		if (EnumHasAnyFlags(VisibilityFlag, EGroomOptionsVisibility::ConversionOptions))
		{
			ConversionOptionsProperty->SetMetaData(TEXT("ShowOnlyInnerProperties"), TEXT("1"));
			ConversionOptionsProperty->SetMetaData(TEXT("Category"), TEXT("Conversion"));
		}
		else
		{
			// Note that UGroomImportOptions HideCategories named "Hidden",
			// but the hiding doesn't work with ShowOnlyInnerProperties 
			ConversionOptionsProperty->RemoveMetaData(TEXT("ShowOnlyInnerProperties"));
			ConversionOptionsProperty->SetMetaData(TEXT("Category"), TEXT("Hidden"));
		}
	}

	FString FileName = FPaths::GetCleanFilename(FilePath);
	Window->SetContent
	(
		SAssignNew(OptionsWindow, SGroomImportOptionsWindow)
		.ImportOptions(ImportOptions)
		.WidgetWindow(Window)
		.FullPath(FText::FromString(FileName))
		.ButtonLabel(InButtonLabel)
	);

	TSharedPtr<SWindow> ParentWindow;

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	return OptionsWindow;
}

TSharedPtr<SGroomImportOptionsWindow> SGroomImportOptionsWindow::DisplayImportOptions(UGroomImportOptions* ImportOptions, const FString& FilePath)
{
	return DisplayOptions(ImportOptions, FilePath, EGroomOptionsVisibility::All, LOCTEXT("GroomOptionsWindow_WindowTitle", "Groom Import Options"), LOCTEXT("GroomOptionsWindow_Import", "Import"));
}

TSharedPtr<SGroomImportOptionsWindow> SGroomImportOptionsWindow::DisplayRebuildOptions(UGroomImportOptions* ImportOptions, const FString& FilePath)
{
	return DisplayOptions(ImportOptions, FilePath, EGroomOptionsVisibility::BuildOptions, LOCTEXT("GroomBuildOptionsWindow_WindowTitle", "Groom Build Options"), LOCTEXT("GroomOptionsWindow_Build", "Build"));
}


#undef LOCTEXT_NAMESPACE
