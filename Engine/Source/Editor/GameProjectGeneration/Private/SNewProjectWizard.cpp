// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNewProjectWizard.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "DesktopPlatformModule.h"
#include "Dialogs/SOutputLogDialog.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "GameProjectGenerationLog.h"
#include "GameProjectGenerationModule.h"
#include "GameProjectUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "HardwareTargetingModule.h"
#include "IDocumentation.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Internationalization/BreakIterator.h"
#include "Layout/WidgetPath.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/MessageDialog.h"
#include "Misc/Parse.h"
#include "ProjectDescriptor.h"
#include "SGameProjectDialog.h"
#include "SGetSuggestedIDEWidget.h"
#include "Settings/EditorSettings.h"
#include "SlateOptMacros.h"
#include "SourceCodeNavigation.h"
#include "TemplateCategory.h"
#include "TemplateItem.h"
#include "TemplateProjectDefs.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Workflow/SWizard.h"

#define LOCTEXT_NAMESPACE "NewProjectWizard"

FName SNewProjectWizard::TemplatePageName = TEXT("Template");
FName SNewProjectWizard::NameAndLocationPageName = TEXT("NameAndLocation");

namespace NewProjectWizardDefs
{
	const float ThumbnailSize = 64.f, ThumbnailPadding = 5.f;
	const float ItemWidth = ThumbnailSize + 2*ThumbnailPadding;
	const float ItemHeight = ItemWidth + 30;
	const FName DefaultCategoryName = "Games";
}

/**
 * Simple widget used to display a folder path, and a name of a file:
 * __________________________  ____________________
 * | C:\Users\Joe.Bloggs    |  | SomeFile.txt     |
 * |-------- Folder --------|  |------ Name ------|
 */
class SFilepath : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS( SFilepath )
		: _LabelBackgroundColor(FLinearColor::Black)
		, _LabelBackgroundBrush(FEditorStyle::GetBrush("WhiteBrush"))
	{}
		/** Attribute specifying the text to display in the folder input */
		SLATE_ATTRIBUTE(FText, FolderPath)

		/** Attribute specifying the text to display in the name input */
		SLATE_ATTRIBUTE(FText, Name)

		/** Background label tint for the folder/name labels */
		SLATE_ATTRIBUTE(FSlateColor, LabelBackgroundColor)

		/** Background label brush for the folder/name labels */
		SLATE_ATTRIBUTE(const FSlateBrush*, LabelBackgroundBrush)

		/** Event that is triggered when the browser for folder button is clicked */
		SLATE_EVENT(FOnClicked, OnBrowseForFolder)

		/** Events for when the name field is manipulated */
		SLATE_EVENT(FOnTextChanged, OnNameChanged)
		SLATE_EVENT(FOnTextCommitted, OnNameCommitted)
		
		/** Events for when the folder field is manipulated */
		SLATE_EVENT(FOnTextChanged, OnFolderChanged)
		SLATE_EVENT(FOnTextCommitted, OnFolderCommitted)

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs )
	{
		ChildSlot
		[
			SNew(SGridPanel)
			.FillColumn(0, 2.f)
			.FillColumn(1, 1.f)

			// Folder input
			+ SGridPanel::Slot(0, 0)
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				[
					SNew(SEditableTextBox)
					.Text(InArgs._FolderPath)
					// Large right hand padding to make room for the browse button
					.Padding(FMargin(5.f, 3.f, 25.f, 3.f))
					.OnTextChanged(InArgs._OnFolderChanged)
					.OnTextCommitted(InArgs._OnFolderCommitted)
				]
					
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "FilePath.FolderButton")
					.ContentPadding(FMargin(4.f, 0.f))
					.OnClicked(InArgs._OnBrowseForFolder)
					.ToolTipText(LOCTEXT("BrowseForFolder", "Browse for a folder"))
					.Text(LOCTEXT("...", "..."))
				]
			]

			// Folder label
			+ SGridPanel::Slot(0, 1)
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.HeightOverride(5)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("FilePath.GroupIndicator"))
						.BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.5f))
						.Padding(FMargin(150.f, 0.f))
					]
				]
					
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.Padding(5.f)
					.BorderImage(InArgs._LabelBackgroundBrush)
					.BorderBackgroundColor(InArgs._LabelBackgroundColor)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Folder", "Folder"))
					]
				]
			]

			// Name input
			+ SGridPanel::Slot(1, 0)
			.Padding(FMargin(5.f, 0.f, 0.f, 0.f))
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.Text(InArgs._Name)
				.Padding(FMargin(5.f, 3.f))
				.OnTextChanged(InArgs._OnNameChanged)
				.OnTextCommitted(InArgs._OnNameCommitted)
			]

			// Name label
			+ SGridPanel::Slot(1, 1)
			.Padding(FMargin(5.f, 0.f, 0.f, 0.f))
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.HeightOverride(5)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("FilePath.GroupIndicator"))
						.BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.5f))
						.Padding(FMargin(75.f, 0.f))
					]
				]
					
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.Padding(5.f)
					.BorderImage(InArgs._LabelBackgroundBrush)
					.BorderBackgroundColor(InArgs._LabelBackgroundColor)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Name", "Name"))
					]
				]
			]
		];
	}
};

/** Slate tile widget for template projects */
class STemplateTile : public STableRow<TSharedPtr<FTemplateItem>>
{
public:
	SLATE_BEGIN_ARGS( STemplateTile ){}
		SLATE_ARGUMENT( TSharedPtr<FTemplateItem>, Item )
	SLATE_END_ARGS()

private:
	TWeakPtr<FTemplateItem> Item;

public:
	/** Static build function */
	static TSharedRef<ITableRow> BuildTile(TSharedPtr<FTemplateItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
	{
		if (!ensure(Item.IsValid()))
		{
			return SNew(STableRow<TSharedPtr<FTemplateItem>>, OwnerTable);
		}

		return SNew(STemplateTile, OwnerTable).Item(Item);
	}

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable )
	{
		check(InArgs._Item.IsValid())
		Item = InArgs._Item;

		STableRow::Construct(
			STableRow::FArguments()
			.Style(FEditorStyle::Get(), "GameProjectDialog.TemplateListView.TableRow")
			.Content()
			[
				SNew(SVerticalBox)

				// Thumbnail
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(NewProjectWizardDefs::ThumbnailPadding)
				[
					SNew(SBox)
					.WidthOverride( NewProjectWizardDefs::ThumbnailSize )
					.HeightOverride( NewProjectWizardDefs::ThumbnailSize )
					[
						SNew(SImage)
						.Image(this, &STemplateTile::GetThumbnail)
					]
				]

				// Name
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Top)
				.Padding(FMargin(NewProjectWizardDefs::ThumbnailPadding, 0))
				[
					SNew(STextBlock)
					.WrapTextAt( NewProjectWizardDefs::ThumbnailSize )
					.Justification(ETextJustify::Center)
					.LineBreakPolicy(FBreakIterator::CreateCamelCaseBreakIterator())
					//.HighlightText(this, &SNewProjectWizard::GetItemHighlightText)
					.Text(InArgs._Item->Name)
				]
			],
			OwnerTable
		);
	}

private:

	/** Get this item's thumbnail or return the default */
	const FSlateBrush* GetThumbnail() const
	{
		auto ItemPtr = Item.Pin();
		if (ItemPtr.IsValid() && ItemPtr->Thumbnail.IsValid())
		{
			return ItemPtr->Thumbnail.Get();
		}
		return FEditorStyle::GetBrush("GameProjectDialog.DefaultGameThumbnail.Small");
	}
	
};

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SNewProjectWizard::Construct( const FArguments& InArgs )
{
	bLastGlobalValidityCheckSuccessful = true;
	bLastNameAndLocationValidityCheckSuccessful = true;
	bCopyStarterContent = GEditor ? GetDefault<UEditorSettings>()->bCopyStarterContentPreference : true;

	SelectedHardwareClassTarget = EHardwareClass::Desktop;
	SelectedGraphicsPreset = EGraphicsPreset::Maximum;

	OnTemplateDoubleClick = InArgs._OnTemplateDoubleClick;

	// Find all template projects
	FindTemplateProjects();
	SetDefaultProjectLocation();

	TemplateListView = SNew(STileView<TSharedPtr<FTemplateItem>>)
	.ListItemsSource(&FilteredTemplateList)
	.SelectionMode(ESelectionMode::Single)
	.ClearSelectionOnClick(false)
	.OnGenerateTile_Static(&STemplateTile::BuildTile)
	.ItemHeight(NewProjectWizardDefs::ItemHeight)
	.ItemWidth(NewProjectWizardDefs::ItemWidth)
	.OnMouseButtonDoubleClick(this, &SNewProjectWizard::HandleTemplateListViewDoubleClick)
	.OnSelectionChanged(this, &SNewProjectWizard::HandleTemplateListViewSelectionChanged);

	const EVisibility StarterContentVisiblity = GameProjectUtils::IsStarterContentAvailableForNewProjects() ? EVisibility::Visible : EVisibility::Collapsed;

	TSharedRef<SSeparator> Separator = SNew(SSeparator).Orientation(EOrientation::Orient_Vertical);
	Separator->SetBorderBackgroundColor(FLinearColor::White.CopyWithNewOpacity(0.25f));

	ChildSlot
	.Padding(0)
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		[
			SNew(SVerticalBox)

			// Templates list
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0)
			[
				SNew(SBorder)
				.Padding(8)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					[
						SNew(SScrollBorder, TemplateListView.ToSharedRef())
						[
							TemplateListView.ToSharedRef()
						]
					]
										
					+ SHorizontalBox::Slot()
					.Padding(8, 0.0f)
					.AutoWidth()
					[
						Separator
					]

					// Selected template details
					+ SHorizontalBox::Slot()
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						.Padding(8, 0.0f)
						[
							SNew(SVerticalBox)

							// Preview image
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(FMargin(0.0f, 0.0f, 0.0f, 15.f))
							[
								SNew(SBox)
								.Visibility(this, &SNewProjectWizard::GetSelectedTemplatePreviewVisibility)
								.WidthOverride(400)
								.HeightOverride(200)
								[
									SNew(SBorder)
									.Padding(FMargin(0.0f, 0.0f, 0.0f, 4.f))
									.BorderImage(FEditorStyle::GetBrush("ContentBrowser.ThumbnailShadow"))
									[
										SNew(SImage)
										.Image(this, &SNewProjectWizard::GetSelectedTemplatePreviewImage)
									]
								]
							]

							// Template Name
							+ SVerticalBox::Slot()
							.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
							.AutoHeight()
							[
								SNew(STextBlock)
								.AutoWrapText(true)
								.TextStyle(FEditorStyle::Get(), "GameProjectDialog.FeatureText")
								.Text(this, &SNewProjectWizard::GetSelectedTemplateProperty, &FTemplateItem::Name)
							]
						
							// Template Description
							+ SVerticalBox::Slot()
							[
								SNew(STextBlock)
								.AutoWrapText(true)
								.Text(this, &SNewProjectWizard::GetSelectedTemplateProperty, &FTemplateItem::Description)
							]
											
							// Asset types
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(FMargin(0.0f, 5.0f, 0.0f, 5.0f))
							[
								SNew(SBox)
								.Visibility(this, &SNewProjectWizard::GetSelectedTemplateAssetVisibility)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot()
									[
										SNew(STextBlock)
										.TextStyle(FEditorStyle::Get(), "GameProjectDialog.FeatureText")
										.Text(LOCTEXT("ProjectTemplateAssetTypes", "Asset Type References:"))
									]
									+ SVerticalBox::Slot()
									.AutoHeight()
									[
										SNew(STextBlock)
										.AutoWrapText(true)
										.Text(this, &SNewProjectWizard::GetSelectedTemplateAssetTypes)
									]
								]
							]
							// Class types
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(FMargin(0.0f, 5.0f, 0.0f, 5.0f))
							[
								SNew(SBox)
								.Visibility(this, &SNewProjectWizard::GetSelectedTemplateClassVisibility)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot()
									[
										SNew(STextBlock)
										.TextStyle(FEditorStyle::Get(), "GameProjectDialog.FeatureText")
										.Text(LOCTEXT("ProjectTemplateClassTypes", "Class Type References:"))
									]
									+ SVerticalBox::Slot()
									.AutoHeight()
									[
										SNew(STextBlock)
										.AutoWrapText(true)
										.Text(this, &SNewProjectWizard::GetSelectedTemplateClassTypes)
									]
								]
							]
						]
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility(this, &SNewProjectWizard::GetTemplateListLocationBoxVisibility)
			[
				MakeProjectLocationWidget()
			]
		]
	];

	SetCurrentCategory(ActiveCategory);

	UpdateProjectFileValidity();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

bool SNewProjectWizard::ShouldShowProjectSettingsPage() const
{
	const TArray<ETemplateSetting>& HiddenSettings = GetSelectedTemplateProperty(&FTemplateItem::HiddenSettings);
	return !HiddenSettings.Contains(ETemplateSetting::All);
}

void SNewProjectWizard::OnSetCopyStarterContent(int32 InCopyStarterContent)
{
	bCopyStarterContent = InCopyStarterContent != 0;
}

EVisibility SNewProjectWizard::GetTemplateListLocationBoxVisibility() const
{
	bool bShowSettings = ShouldShowProjectSettingsPage();
	return bShowSettings ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SNewProjectWizard::GetStarterContentWarningVisibility() const
{
	return (bCopyStarterContent && (SelectedHardwareClassTarget == EHardwareClass::Mobile)) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SNewProjectWizard::GetStarterContentWarningTooltip() const
{
	if (SelectedGraphicsPreset == EGraphicsPreset::Maximum)
	{
		return LOCTEXT("StarterContentMobileWarning_Maximum", "Note: Starter content will be inserted first time the project is opened, and can increase the packaged size significantly, removing the example maps will result in only packaging content that is actually used");
	}
	else
	{
		return LOCTEXT("StarterContentMobileWarning_Scalable", "Warning: Starter content content will be inserted first time the project is opened, and is not optimized for scalable mobile projects");
	}
}

void SNewProjectWizard::HandleTemplateListViewSelectionChanged(TSharedPtr<FTemplateItem> TemplateItem, ESelectInfo::Type SelectInfo)
{
	UpdateProjectFileValidity();

	if (TemplateItem.IsValid())
	{
		if (TemplateItem->HiddenSettings.Contains(ETemplateSetting::StarterContent))
		{
			bCopyStarterContent = false;
		}
	}
}

TSharedPtr<FTemplateItem> SNewProjectWizard::GetSelectedTemplateItem() const
{
	TArray< TSharedPtr<FTemplateItem> > SelectedItems = TemplateListView->GetSelectedItems();
	if ( SelectedItems.Num() > 0 )
	{
		return SelectedItems[0];
	}
	
	return nullptr;
}

FText SNewProjectWizard::GetSelectedTemplateClassTypes() const
{
	return FText::FromString(GetSelectedTemplateProperty(&FTemplateItem::ClassTypes));
}

EVisibility SNewProjectWizard::GetSelectedTemplateClassVisibility() const
{
	return GetSelectedTemplateProperty(&FTemplateItem::ClassTypes).IsEmpty() == false? EVisibility::Visible : EVisibility::Collapsed;
}

FText SNewProjectWizard::GetSelectedTemplateAssetTypes() const
{
	return FText::FromString(GetSelectedTemplateProperty(&FTemplateItem::AssetTypes));
}

EVisibility SNewProjectWizard::GetSelectedTemplateAssetVisibility() const
{
	return GetSelectedTemplateProperty(&FTemplateItem::AssetTypes).IsEmpty() == false ? EVisibility::Visible : EVisibility::Collapsed;
}

const FSlateBrush* SNewProjectWizard::GetSelectedTemplatePreviewImage() const
{
	auto PreviewImage = GetSelectedTemplateProperty(&FTemplateItem::PreviewImage);
	return PreviewImage.IsValid() ? PreviewImage.Get() : nullptr;
}

EVisibility SNewProjectWizard::GetSelectedTemplatePreviewVisibility() const
{
	auto PreviewImage = GetSelectedTemplateProperty(&FTemplateItem::PreviewImage);
	return PreviewImage.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SNewProjectWizard::GetCurrentProjectFileName() const
{
	return FText::FromString( CurrentProjectFileName );
}

FString SNewProjectWizard::GetCurrentProjectFileNameStringWithExtension() const
{
	return CurrentProjectFileName + TEXT(".") + FProjectDescriptor::GetExtension();
}

void SNewProjectWizard::OnCurrentProjectFileNameChanged(const FText& InValue)
{
	CurrentProjectFileName = InValue.ToString();
	UpdateProjectFileValidity();
}

FText SNewProjectWizard::GetCurrentProjectFilePath() const
{
	return FText::FromString(CurrentProjectFilePath);
}

FString SNewProjectWizard::GetCurrentProjectFileParentFolder() const
{
	if ( CurrentProjectFilePath.EndsWith(TEXT("/")) || CurrentProjectFilePath.EndsWith("\\") )
	{
		return FPaths::GetCleanFilename( CurrentProjectFilePath.LeftChop(1) );
	}
	else
	{
		return FPaths::GetCleanFilename( CurrentProjectFilePath );
	}
}

void SNewProjectWizard::OnCurrentProjectFilePathChanged(const FText& InValue)
{
	CurrentProjectFilePath = InValue.ToString();
	FPaths::MakePlatformFilename(CurrentProjectFilePath);
	UpdateProjectFileValidity();
}

FString SNewProjectWizard::GetProjectFilenameWithPathLabelText() const
{
	return GetProjectFilenameWithPath();
}

FString SNewProjectWizard::GetProjectFilenameWithPath() const
{
	if ( CurrentProjectFilePath.IsEmpty() )
	{
		// Don't even try to assemble the path or else it may be relative to the binaries folder!
		return TEXT("");
	}
	else
	{
		const FString ProjectName = CurrentProjectFileName;
		const FString ProjectPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*CurrentProjectFilePath);
		const FString Filename = ProjectName + TEXT(".") + FProjectDescriptor::GetExtension();
		FString ProjectFilename = FPaths::Combine( *ProjectPath, *ProjectName, *Filename );
		FPaths::MakePlatformFilename(ProjectFilename);
		return ProjectFilename;
	}
}


FReply SNewProjectWizard::HandleBrowseButtonClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if ( DesktopPlatform )
	{
		FString FolderName;
		const FString Title = LOCTEXT("NewProjectBrowseTitle", "Choose a project location").ToString();
		const bool bFolderSelected = DesktopPlatform->OpenDirectoryDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),
			Title,
			LastBrowsePath,
			FolderName
			);

		if ( bFolderSelected )
		{
			if ( !FolderName.EndsWith(TEXT("/")) )
			{
				FolderName += TEXT("/");
			}
			
			FPaths::MakePlatformFilename(FolderName);
			LastBrowsePath = FolderName;
			CurrentProjectFilePath = FolderName;
		}
	}

	return FReply::Handled();
}


void SNewProjectWizard::HandleTemplateListViewDoubleClick( TSharedPtr<FTemplateItem> TemplateItem )
{
	OnTemplateDoubleClick.ExecuteIfBound();
}

bool SNewProjectWizard::CanCreateProject() const
{
	return bLastGlobalValidityCheckSuccessful && bLastNameAndLocationValidityCheckSuccessful;
}

EVisibility SNewProjectWizard::GetGlobalErrorLabelVisibility() const
{
	const bool bIsVisible = GetNameAndLocationErrorLabelText().IsEmpty() && !GetGlobalErrorLabelText().IsEmpty();
	return bIsVisible ? EVisibility::Visible : EVisibility::Hidden;
}


EVisibility SNewProjectWizard::GetGlobalErrorLabelCloseButtonVisibility() const
{
	return PersistentGlobalErrorLabelText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SNewProjectWizard::GetGlobalErrorLabelText() const
{
	if ( !PersistentGlobalErrorLabelText.IsEmpty() )
	{
		return PersistentGlobalErrorLabelText;
	}

	if ( !bLastGlobalValidityCheckSuccessful )
	{
		return LastGlobalValidityErrorText;
	}

	return FText::GetEmpty();
}

FReply SNewProjectWizard::OnCloseGlobalErrorLabelClicked()
{
	PersistentGlobalErrorLabelText = FText();

	return FReply::Handled();
}

EVisibility SNewProjectWizard::GetNameAndLocationErrorLabelVisibility() const
{
	return GetNameAndLocationErrorLabelText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SNewProjectWizard::GetNameAndLocationErrorLabelText() const
{
	if (!bLastNameAndLocationValidityCheckSuccessful)
	{
		return LastNameAndLocationValidityErrorText;
	}

	return FText::GetEmpty();
}

TMap<FName, TArray<TSharedPtr<FTemplateItem>> >& SNewProjectWizard::FindTemplateProjects()
{
	// Clear the list out first - or we could end up with duplicates
	Templates.Empty();

	// Now discover and all data driven templates
	TArray<FString> TemplateRootFolders;

	// @todo rocket make template folder locations extensible.
	TemplateRootFolders.Add(FPaths::RootDir() + TEXT("Templates"));

	// Add the Enterprise templates
	TemplateRootFolders.Add(FPaths::EnterpriseDir() + TEXT("Templates"));

	// allow plugins to define templates
	TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPlugins();
	for (const TSharedRef<IPlugin>& Plugin : Plugins)
	{
		FString PluginDirectory = Plugin->GetBaseDir();
		if (!PluginDirectory.IsEmpty())
		{
			const FString PluginTemplatesDirectory = FPaths::Combine(*PluginDirectory, TEXT("Templates"));

			if (IFileManager::Get().DirectoryExists(*PluginTemplatesDirectory))
			{
				TemplateRootFolders.Add(PluginTemplatesDirectory);
			}
		}
	}

	// Form a list of all folders that could contain template projects
	TArray<FString> AllTemplateFolders;
	for ( auto TemplateRootFolderIt = TemplateRootFolders.CreateConstIterator(); TemplateRootFolderIt; ++TemplateRootFolderIt )
	{
		const FString Root = *TemplateRootFolderIt;
		
		const FString SearchString = Root / TEXT("*");
		TArray<FString> TemplateFolders;
		IFileManager::Get().FindFiles(TemplateFolders, *SearchString, /*Files=*/false, /*Directories=*/true);
		for ( auto TemplateFolderIt = TemplateFolders.CreateConstIterator(); TemplateFolderIt; ++TemplateFolderIt )
		{
			AllTemplateFolders.Add( Root / (*TemplateFolderIt) );
		}
	}

	TArray<TSharedPtr<FTemplateItem>> FoundTemplates;

	// Add a template item for every discovered project
	for ( auto TemplateFolderIt = AllTemplateFolders.CreateConstIterator(); TemplateFolderIt; ++TemplateFolderIt )
	{
		const FString SearchString = (*TemplateFolderIt) / TEXT("*.") + FProjectDescriptor::GetExtension();
		TArray<FString> FoundProjectFiles;
		IFileManager::Get().FindFiles(FoundProjectFiles, *SearchString, /*Files=*/true, /*Directories=*/false);
		if ( FoundProjectFiles.Num() > 0 )
		{
			if ( ensure(FoundProjectFiles.Num() == 1) )
			{
				// Make sure a TemplateDefs ini file exists
				const FString Root = *TemplateFolderIt;
				UTemplateProjectDefs* TemplateDefs = GameProjectUtils::LoadTemplateDefs(Root);
				if (TemplateDefs)
				{
					// Ignore any templates whose definition says we cannot use to create a project
					if( TemplateDefs->bAllowProjectCreation == false )
						continue;

					FString ProjectFile = Root / FoundProjectFiles[0];

					// If no template name was specified for the current culture, just use the project name
					TArray<FName> TemplateCategories = TemplateDefs->Categories;
					if (TemplateCategories.Num() == 0)
					{
						TemplateCategories.Add(NewProjectWizardDefs::DefaultCategoryName);
					}

					FString TemplateKey = Root;
					TemplateKey.RemoveFromEnd("BP");

					TSharedPtr<FTemplateItem>* ExistingTemplate = FoundTemplates.FindByPredicate([&TemplateKey](TSharedPtr<FTemplateItem> Item)
					{
						return Item->Key == TemplateKey;
					});

					if (ExistingTemplate != nullptr)
					{
						if (TemplateDefs->GeneratesCode(Root))
						{
							(*ExistingTemplate)->CodeProjectFile = ProjectFile;
						}
						else
						{
							(*ExistingTemplate)->BlueprintProjectFile = ProjectFile;
						}

						continue;
					}
					
					// Did not find an existing template. Create a new one to add to the template list.
					TSharedPtr<FTemplateItem> Template = MakeShareable(new FTemplateItem());
					Template->Key = TemplateKey;
					Template->Categories = TemplateCategories;
					Template->Description = TemplateDefs->GetLocalizedDescription();
					Template->ClassTypes = TemplateDefs->ClassTypes;
					Template->AssetTypes = TemplateDefs->AssetTypes;
					Template->HiddenSettings = TemplateDefs->HiddenSettings;
					Template->bIsEnterprise = TemplateDefs->bIsEnterprise;
					Template->bIsBlankTemplate = TemplateDefs->bIsBlank;

					Template->Name = TemplateDefs->GetDisplayNameText();
					if (Template->Name.IsEmpty())
					{
						Template->Name = FText::FromString(FPaths::GetBaseFilename(ProjectFile));
					}

					// Only generate code if the template has a source folder
					if (TemplateDefs->GeneratesCode(Root))
					{
						Template->CodeProjectFile = ProjectFile;
					}
					else
					{
						Template->BlueprintProjectFile = ProjectFile;
					}

					const FString ThumbnailPNGFile = (Root + TEXT("/Media/") + FoundProjectFiles[0]).Replace(TEXT(".uproject"), TEXT(".png"));
					if ( FPlatformFileManager::Get().GetPlatformFile().FileExists(*ThumbnailPNGFile) )
					{
						const FName BrushName = FName(*ThumbnailPNGFile);
						Template->Thumbnail = MakeShareable( new FSlateDynamicImageBrush(BrushName, FVector2D(128,128) ) );
					}

					TSharedPtr<FSlateDynamicImageBrush> PreviewBrush;
					const FString PreviewPNGFile = (Root + TEXT("/Media/") + FoundProjectFiles[0]).Replace(TEXT(".uproject"), TEXT("_Preview.png"));
					if ( FPlatformFileManager::Get().GetPlatformFile().FileExists(*PreviewPNGFile) )
					{
						const FName BrushName = FName(*PreviewPNGFile);
						Template->PreviewImage = MakeShareable( new FSlateDynamicImageBrush(BrushName, FVector2D(512,256) ) );
					}

					// Get the sort key
					FString SortKey = TemplateDefs->SortKey;
					const FString CleanFilename = FPaths::GetCleanFilename(ProjectFile);
					if(SortKey.Len() == 0)
					{
						SortKey = CleanFilename;
					}

					if (CleanFilename == GameProjectUtils::GetDefaultProjectTemplateFilename())
					{
						SortKey = TEXT("_0");
					}
					Template->SortKey = SortKey;

					FoundTemplates.Add(Template);
				}
			}
			else
			{
				// More than one project file in this template? This is not legal, skip it.
				continue;
			}
		}
	}

	for (const TSharedPtr<FTemplateItem>& Template : FoundTemplates)
	{
		for (const FName& Category : Template->Categories)
		{
			Templates.FindOrAdd(Category).Add(Template);
		}
	}

	TArray<TSharedPtr<FTemplateCategory>> AllTemplateCategories;
	SGameProjectDialog::GetAllTemplateCategories(AllTemplateCategories);

	// Validate that all our templates have a category defined
	TArray<FName> CategoryKeys;
	Templates.GetKeys(CategoryKeys);
	for (const FName& CategoryKey : CategoryKeys)
	{
		bool bCategoryExists = AllTemplateCategories.ContainsByPredicate([&CategoryKey](const TSharedPtr<FTemplateCategory>& Category)
			{
				return Category->Key == CategoryKey;
			});

		if (!bCategoryExists)
		{
			UE_LOG(LogGameProjectGeneration, Warning, TEXT("Failed to find category definition named '%s', it is not defined in any TemplateCategories.ini."), *CategoryKey.ToString());
		}
	}

	// add blank template to empty categories
	{
		TSharedPtr<FTemplateItem> BlankTemplate = MakeShareable(new FTemplateItem());
		BlankTemplate->Name = LOCTEXT("BlankProjectName", "Blank");
		BlankTemplate->Description = LOCTEXT("BlankProjectDescription", "A clean empty project with no code and default settings.");
		BlankTemplate->Key = TEXT("Blank");
		BlankTemplate->SortKey = TEXT("_1");
		BlankTemplate->Thumbnail = MakeShareable(new FSlateBrush(*FEditorStyle::GetBrush("GameProjectDialog.BlankProjectThumbnail")));
		BlankTemplate->PreviewImage = MakeShareable(new FSlateBrush(*FEditorStyle::GetBrush("GameProjectDialog.BlankProjectPreview")));
		BlankTemplate->BlueprintProjectFile = TEXT("");
		BlankTemplate->CodeProjectFile = TEXT("");
		BlankTemplate->bIsEnterprise = false;
		BlankTemplate->bIsBlankTemplate = true;

		for (const TSharedPtr<FTemplateCategory>& Category : AllTemplateCategories)
		{
			const TArray<TSharedPtr<FTemplateItem>>* CategoryEntry = Templates.Find(Category->Key);
			if (CategoryEntry == nullptr)
			{
				Templates.Add(Category->Key).Add(BlankTemplate);
			}
		}
	}

	return Templates;
}

void SNewProjectWizard::SetDefaultProjectLocation()
{
	FString DefaultProjectFilePath;
	
	// First, try and use the first previously used path that still exists
	for ( const FString& CreatedProjectPath : GetDefault<UEditorSettings>()->CreatedProjectPaths )
	{
		if ( IFileManager::Get().DirectoryExists(*CreatedProjectPath) )
		{
			DefaultProjectFilePath = CreatedProjectPath;
			break;
		}
	}

	if ( DefaultProjectFilePath.IsEmpty() )
	{
		// No previously used path, decide a default path.
		DefaultProjectFilePath = FDesktopPlatformModule::Get()->GetDefaultProjectCreationPath();
		IFileManager::Get().MakeDirectory(*DefaultProjectFilePath, true);
	}

	if ( !DefaultProjectFilePath.IsEmpty() && DefaultProjectFilePath.Right(1) == TEXT("/") )
	{
		DefaultProjectFilePath.LeftChop(1);
	}

	FPaths::NormalizeFilename(DefaultProjectFilePath);
	FPaths::MakePlatformFilename(DefaultProjectFilePath);
	const FString GenericProjectName = LOCTEXT("DefaultProjectName", "MyProject").ToString();
	FString ProjectName = GenericProjectName;

	// Check to make sure the project file doesn't already exist
	FText FailReason;
	if ( !GameProjectUtils::IsValidProjectFileForCreation(DefaultProjectFilePath / ProjectName / ProjectName + TEXT(".") + FProjectDescriptor::GetExtension(), FailReason) )
	{
		// If it exists, find an appropriate numerical suffix
		const int MaxSuffix = 1000;
		int32 Suffix;
		for ( Suffix = 2; Suffix < MaxSuffix; ++Suffix )
		{
			ProjectName = GenericProjectName + FString::Printf(TEXT("%d"), Suffix);
			if ( GameProjectUtils::IsValidProjectFileForCreation(DefaultProjectFilePath / ProjectName / ProjectName + TEXT(".") + FProjectDescriptor::GetExtension(), FailReason) )
			{
				// Found a name that is not taken. Break out.
				break;
			}
		}

		if (Suffix >= MaxSuffix)
		{
			UE_LOG(LogGameProjectGeneration, Warning, TEXT("Failed to find a suffix for the default project name"));
			ProjectName = TEXT("");
		}
	}

	if ( !DefaultProjectFilePath.IsEmpty() )
	{
		CurrentProjectFileName = ProjectName;
		CurrentProjectFilePath = DefaultProjectFilePath;
		FPaths::MakePlatformFilename(CurrentProjectFilePath);
		LastBrowsePath = CurrentProjectFilePath;
	}
}

void SNewProjectWizard::UpdateProjectFileValidity()
{
	// Global validity
	{
		bLastGlobalValidityCheckSuccessful = true;

		TSharedPtr<FTemplateItem> SelectedTemplate = GetSelectedTemplateItem();
		if ( !SelectedTemplate.IsValid() )
		{
			bLastGlobalValidityCheckSuccessful = false;
			LastGlobalValidityErrorText = LOCTEXT("NoTemplateSelected", "No Template Selected");
		}
		else
		{
			if (IsCompilerRequired())
			{
				if ( !FSourceCodeNavigation::IsCompilerAvailable() )
				{
					bLastGlobalValidityCheckSuccessful = false;
					LastGlobalValidityErrorText = FText::Format( LOCTEXT("NoCompilerFound", "No compiler was found. In order to use a C++ template, you must first install {0}."), FSourceCodeNavigation::GetSuggestedSourceCodeIDE() );
				}
				else if ( !FDesktopPlatformModule::Get()->IsUnrealBuildToolAvailable() )
				{
					bLastGlobalValidityCheckSuccessful = false;
					LastGlobalValidityErrorText = LOCTEXT("UBTNotFound", "Engine source code was not found. In order to use a C++ template, you must have engine source code in Engine/Source.");
				}
			}
		}
	}

	// Name and Location Validity
	{
		bLastNameAndLocationValidityCheckSuccessful = true;

		if ( !FPlatformMisc::IsValidAbsolutePathFormat(CurrentProjectFilePath) )
		{
			bLastNameAndLocationValidityCheckSuccessful = false;
			LastNameAndLocationValidityErrorText = LOCTEXT( "InvalidFolderPath", "The folder path is invalid" );
		}
		else
		{
			FText FailReason;
			if ( !GameProjectUtils::IsValidProjectFileForCreation(GetProjectFilenameWithPath(), FailReason) )
			{
				bLastNameAndLocationValidityCheckSuccessful = false;
				LastNameAndLocationValidityErrorText = FailReason;
			}
		}

		if ( CurrentProjectFileName.Contains(TEXT("/")) || CurrentProjectFileName.Contains(TEXT("\\")) )
		{
			bLastNameAndLocationValidityCheckSuccessful = false;
			LastNameAndLocationValidityErrorText = LOCTEXT("SlashOrBackslashInProjectName", "The project name may not contain a slash or backslash");
		}
		else
		{
			FText FailReason;
			if ( !GameProjectUtils::IsValidProjectFileForCreation(GetProjectFilenameWithPath(), FailReason) )
			{
				bLastNameAndLocationValidityCheckSuccessful = false;
				LastNameAndLocationValidityErrorText = FailReason;
			}
		}
	}
}

bool SNewProjectWizard::IsCompilerRequired() const
{
	TSharedPtr<FTemplateItem> SelectedTemplate = GetSelectedTemplateItem();

	if (SelectedTemplate.IsValid())
	{
		return bShouldGenerateCode && !SelectedTemplate->CodeProjectFile.IsEmpty();
	}
	return false;
}

bool SNewProjectWizard::CreateProject(const FString& ProjectFile)
{
	// Get the selected template
	TSharedPtr<FTemplateItem> SelectedTemplate = GetSelectedTemplateItem();

	if (!ensure(SelectedTemplate.IsValid()))
	{
		// A template must be selected.
		return false;
	}

	FText FailReason, FailLog;

	const TArray<ETemplateSetting>& HiddenSettings = GetSelectedTemplateProperty(&FTemplateItem::HiddenSettings);

	FProjectInformation ProjectInfo;
	ProjectInfo.ProjectFilename = ProjectFile;
	ProjectInfo.bShouldGenerateCode = bShouldGenerateCode;
	ProjectInfo.bCopyStarterContent = bCopyStarterContent;
	ProjectInfo.TemplateFile = bShouldGenerateCode ? SelectedTemplate->CodeProjectFile : SelectedTemplate->BlueprintProjectFile;
	ProjectInfo.TemplateCategory = ActiveCategory;
	ProjectInfo.bIsEnterpriseProject = SelectedTemplate->bIsEnterprise;
	ProjectInfo.bIsBlankTemplate = SelectedTemplate->bIsBlankTemplate;
	ProjectInfo.bForceExtendedLuminanceRange = SelectedTemplate->bIsBlankTemplate;

	if (!HiddenSettings.Contains(ETemplateSetting::All))
	{
		if (!HiddenSettings.Contains(ETemplateSetting::HardwareTarget))
		{
			ProjectInfo.TargetedHardware = SelectedHardwareClassTarget;
		}

		if (!HiddenSettings.Contains(ETemplateSetting::GraphicsPreset))
		{
			ProjectInfo.DefaultGraphicsPerformance = SelectedGraphicsPreset;
		}

		if (!HiddenSettings.Contains(ETemplateSetting::XR))
		{
			ProjectInfo.bEnableXR = bEnableXR;
		}

		if (!HiddenSettings.Contains(ETemplateSetting::Raytracing))
		{
			ProjectInfo.bEnableRaytracing = bEnableRaytracing;
		}
	}

	if (!GameProjectUtils::CreateProject(ProjectInfo, FailReason, FailLog))
	{
		SOutputLogDialog::Open(LOCTEXT("CreateProject", "Create Project"), FailReason, FailLog, FText::GetEmpty());
		return false;
	}

	// Successfully created the project. Update the last created location string.
	FString CreatedProjectPath = FPaths::GetPath(FPaths::GetPath(ProjectFile)); 

	// if the original path was the drives root (ie: C:/) the double path call strips the last /
	if (CreatedProjectPath.EndsWith(":"))
	{
		CreatedProjectPath.AppendChar('/');
	}

	UEditorSettings* Settings = GetMutableDefault<UEditorSettings>();
	Settings->CreatedProjectPaths.Remove(CreatedProjectPath);
	Settings->CreatedProjectPaths.Insert(CreatedProjectPath, 0);
	Settings->bCopyStarterContentPreference = bCopyStarterContent;
	Settings->PostEditChange();

	return true;
}

void SNewProjectWizard::CreateAndOpenProject()
{
	if (!CanCreateProject())
	{
		return;
	}

	FString ProjectFile = GetProjectFilenameWithPath();
	if (!CreateProject(ProjectFile))
	{
		return;
	}

	if (bShouldGenerateCode)
	{
	    // If the engine is installed it is already compiled, so we can try to build and open a new project immediately. Non-installed situations might require building
	    // the engine (especially the case when binaries came from P4), so we only open the IDE for that.
		if (FApp::IsEngineInstalled())
		{
			if (GameProjectUtils::BuildCodeProject(ProjectFile))
			{
				OpenCodeIDE(ProjectFile);
				OpenProject(ProjectFile);
			}
			else
			{
				// User will have already been prompted to open the IDE
			}
		}
		else
		{
			OpenCodeIDE(ProjectFile);
		}
	}
	else
	{
		OpenProject(ProjectFile );
	}
}

bool SNewProjectWizard::OpenProject(const FString& ProjectFile)
{
	FText FailReason;
	if (GameProjectUtils::OpenProject(ProjectFile, FailReason))
	{
		// Successfully opened the project, the editor is closing.
		// Close this window in case something prevents the editor from closing (save dialog, quit confirmation, etc)
		CloseWindowIfAppropriate();
		return true;
	}

	DisplayError( FailReason );
	return false;
}

bool SNewProjectWizard::OpenCodeIDE(const FString& ProjectFile)
{
	FText FailReason;

	if (GameProjectUtils::OpenCodeIDE(ProjectFile, FailReason))
	{
		// Successfully opened code editing IDE, the editor is closing
		// Close this window in case something prevents the editor from closing (save dialog, quit confirmation, etc)
		CloseWindowIfAppropriate(true);
		return true;
	}

	DisplayError(FailReason);
	return false;
}

void SNewProjectWizard::CloseWindowIfAppropriate( bool ForceClose )
{
	if ( ForceClose || FApp::HasProjectName() )
	{
		TSharedPtr<SWindow> ContainingWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());

		if ( ContainingWindow.IsValid() )
		{
			ContainingWindow->RequestDestroyWindow();
		}
	}
}

void SNewProjectWizard::DisplayError( const FText& ErrorText )
{
	FString ErrorString = ErrorText.ToString();
	UE_LOG(LogGameProjectGeneration, Log, TEXT("%s"), *ErrorString);
	if(ErrorString.Contains("\n"))
	{
		FMessageDialog::Open(EAppMsgType::Ok, ErrorText);
	}
	else
	{
		PersistentGlobalErrorLabelText = ErrorText;
	}
}

/* SNewProjectWizard event handlers
 *****************************************************************************/

void SNewProjectWizard::SetCurrentCategory(FName Category)
{
	FilteredTemplateList = Templates.FindRef(Category);

	// Sort the template folders
	FilteredTemplateList.Sort([](const TSharedPtr<FTemplateItem>& A, const TSharedPtr<FTemplateItem>& B){
		return A->SortKey < B->SortKey;
	});

	if (FilteredTemplateList.Num() > 0)
	{
		TemplateListView->SetSelection(FilteredTemplateList[0]);
	}
	TemplateListView->RequestListRefresh();

	ActiveCategory = Category;
}

TSharedRef<SWidget> SNewProjectWizard::MakeProjectLocationWidget()
{
	TSharedRef<SWidget> Widget = SNew(SVerticalBox)
	+ SVerticalBox::Slot()
	.HAlign(HAlign_Center)
	.Padding(8)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 8)
		[
			SNew(SRichTextBlock)
			.Text(LOCTEXT("ProjectPathDescription", "Select a <RichTextBlock.BoldHighlight>location</> for your project to be stored."))
			.AutoWrapText(true)
			.DecoratorStyleSet(&FEditorStyle::Get())
			.ToolTip(IDocumentation::Get()->CreateToolTip(LOCTEXT("ProjectPathDescriptionTooltip", "All of your project content and code will be stored here."), nullptr, TEXT("Shared/Editor/NewProjectWizard"), TEXT("ProjectPath")))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			// File path widget
			SNew(SFilepath)
			.OnBrowseForFolder(this, &SNewProjectWizard::HandleBrowseButtonClicked)
			.LabelBackgroundBrush(FEditorStyle::GetBrush("ProjectBrowser.Background"))
			.LabelBackgroundColor(FLinearColor::White)
			.FolderPath(this, &SNewProjectWizard::GetCurrentProjectFilePath)
			.Name(this, &SNewProjectWizard::GetCurrentProjectFileName)
			.OnFolderChanged(this, &SNewProjectWizard::OnCurrentProjectFilePathChanged)
			.OnNameChanged(this, &SNewProjectWizard::OnCurrentProjectFileNameChanged)
		]
	];

	Widget->RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[this](double, float)
		{
			UpdateProjectFileValidity();
			return EActiveTimerReturnType::Continue;
		}));

	return Widget;
}

TSharedRef<SWidget> SNewProjectWizard::CreateProjectSettingsPage()
{
	const float UniformPadding = 16.f;

	TSharedRef<SWidget> PageWidget = SNew(SOverlay)
	+ SOverlay::Slot()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.Padding(UniformPadding)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				.Padding(0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.Padding(FMargin(0, 0, 0, UniformPadding))
					[
						MakeProjectSettingsOptionsBox()
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.Padding(0)
		.AutoHeight()
		[
			MakeProjectLocationWidget()
		]
	]
	// Global Error label
	+ SOverlay::Slot()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Bottom)
	.Padding(0, 0, 0, 82) // manually sized to be above the project location box
	[
		SNew(SBorder)
		.Visibility(this, &SNewProjectWizard::GetGlobalErrorLabelVisibility)
		.BorderImage(FEditorStyle::GetBrush("GameProjectDialog.ErrorLabelBorder"))
		.Padding(4)
		[
			SNew(SHorizontalBox)
						
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.f)
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("MessageLog.Warning"))
			]

			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(this, &SNewProjectWizard::GetGlobalErrorLabelText)
				.TextStyle(FEditorStyle::Get(), TEXT("GameProjectDialog.ErrorLabelFont"))
			]

			// Button/link to the suggested IDE
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.AutoWidth()
			.Padding(5.f, 0.f)
			[
				SNew(SGetSuggestedIDEWidget)
			]
									
			// A button to close the persistent global error text
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "NoBorder")
				.ContentPadding(0.0f) 
				.OnClicked(this, &SNewProjectWizard::OnCloseGlobalErrorLabelClicked)
				.Visibility(this, &SNewProjectWizard::GetGlobalErrorLabelCloseButtonVisibility)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("GameProjectDialog.ErrorLabelCloseButton"))
				]
			]
		]
	]
	// Project filename error
	+SOverlay::Slot()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Bottom)
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("GameProjectDialog.ErrorLabelBorder"))
		.Visibility(this, &SNewProjectWizard::GetNameAndLocationErrorLabelVisibility)
		.Padding(4)
		[
			SNew(SHorizontalBox)
					
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.f)
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("MessageLog.Warning"))
			]

			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(this, &SNewProjectWizard::GetNameAndLocationErrorLabelText)
				.TextStyle(FEditorStyle::Get(), "GameProjectDialog.ErrorLabelFont")
			]
		]
	];

	return PageWidget;
}

void SNewProjectWizard::SetHardwareClassTarget(EHardwareClass::Type InHardwareClass)
{
	SelectedHardwareClassTarget = InHardwareClass;
}

void SNewProjectWizard::SetGraphicsPreset(EGraphicsPreset::Type InGraphicsPreset)
{
	SelectedGraphicsPreset = InGraphicsPreset;
}

int32 SNewProjectWizard::OnGetBlueprintOrCppIndex() const
{
	return bShouldGenerateCode ? 1 : 0;
}

void SNewProjectWizard::OnSetBlueprintOrCppIndex(int32 Index)
{
	bShouldGenerateCode = Index == 1;
}

static void AddToProjectSettingsGrid(TSharedRef<SGridPanel> Grid, const TSharedRef<SWidget>& Enum, const TSharedRef<SWidget>& Description, FIntPoint& Slot)
{
	Grid->AddSlot(Slot.X, Slot.Y)
	.Padding(8)
	[
		Enum
	];

	Grid->AddSlot(Slot.X + 1, Slot.Y)
	.VAlign(VAlign_Center)
	.Padding(8)
	[
		Description
	];

	Slot.X += 2;

	if (Slot.X > 2)
	{
		Slot.X = 0;
		Slot.Y++;
	}
}

TSharedRef<SWidget> SNewProjectWizard::MakeProjectSettingsOptionsBox()
{
	static const int EnumWidth = 160;

	IHardwareTargetingModule& HardwareTargeting = IHardwareTargetingModule::Get();

	FIntPoint CurrentSlot(0, 0);

	TSharedRef<SGridPanel> GridPanel = SNew(SGridPanel)
		.FillColumn(1, 1.0f)
		.FillColumn(3, 1.0f);

	const TArray<ETemplateSetting>& HiddenSettings = GetSelectedTemplateProperty(&FTemplateItem::HiddenSettings);

	if (!HiddenSettings.Contains(ETemplateSetting::Languages))
	{
		bool bIsBlueprintAvailable = !GetSelectedTemplateProperty(&FTemplateItem::BlueprintProjectFile).IsEmpty();
		bool bIsCodeAvailable = !GetSelectedTemplateProperty(&FTemplateItem::CodeProjectFile).IsEmpty();

		// if neither is available, then this is a blank template, so both are available
		if (!bIsBlueprintAvailable && !bIsCodeAvailable)
		{
			bIsBlueprintAvailable = true;
			bIsCodeAvailable = true;
		}

		bShouldGenerateCode = !bIsBlueprintAvailable;

		TArray<SDecoratedEnumCombo<int32>::FComboOption> BlueprintOrCppOptions;
		BlueprintOrCppOptions.Add(SDecoratedEnumCombo<int32>::FComboOption(
			0, 
			FSlateIcon(FEditorStyle::GetStyleSetName(), "GameProjectDialog.BlueprintImage_64"), 
			LOCTEXT("ProjectDialog_Blueprint", "Blueprint"), 
			bIsBlueprintAvailable));

		BlueprintOrCppOptions.Add(SDecoratedEnumCombo<int32>::FComboOption(
			1,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "GameProjectDialog.CodeImage_64"),
			LOCTEXT("ProjectDialog_Code", "C++"), 
			bIsCodeAvailable));

		TSharedRef<SDecoratedEnumCombo<int32>> Enum = SNew(SDecoratedEnumCombo<int32>, MoveTemp(BlueprintOrCppOptions))
			.SelectedEnum(this, &SNewProjectWizard::OnGetBlueprintOrCppIndex)
			.OnEnumChanged(this, &SNewProjectWizard::OnSetBlueprintOrCppIndex)
			.Orientation(Orient_Vertical);

		TSharedRef<SRichTextBlock> Description = SNew(SRichTextBlock)
			.Text(LOCTEXT("ProjectDialog_BlueprintOrCppDescription", "Choose whether to create a Blueprint or C++ project."))
			.AutoWrapText(true)
			.DecoratorStyleSet(&FEditorStyle::Get());

		AddToProjectSettingsGrid(GridPanel, Enum, Description, CurrentSlot);
	}

	if (!HiddenSettings.Contains(ETemplateSetting::HardwareTarget))
	{
		TSharedRef<SWidget> Enum = HardwareTargeting.MakeHardwareClassTargetCombo(
			FOnHardwareClassChanged::CreateSP(this, &SNewProjectWizard::SetHardwareClassTarget),
			TAttribute<EHardwareClass::Type>(this, &SNewProjectWizard::GetHardwareClassTarget),
			Orient_Vertical);
		
		TSharedRef<SRichTextBlock> Description = SNew(SRichTextBlock)
			.Text(LOCTEXT("ProjectDialog_HardwareClassTargetDescription", "Choose the closest equivalent target platform. Don't worry, you can change this later in the <RichTextBlock.BoldHighlight>Target Hardware</> section of <RichTextBlock.BoldHighlight>Project Settings</>."))
			.AutoWrapText(true)
			.DecoratorStyleSet(&FEditorStyle::Get());

		AddToProjectSettingsGrid(GridPanel, Enum, Description, CurrentSlot);
	}

	if (!HiddenSettings.Contains(ETemplateSetting::GraphicsPreset))
	{
		TSharedRef<SWidget> Enum = HardwareTargeting.MakeGraphicsPresetTargetCombo(
			FOnGraphicsPresetChanged::CreateSP(this, &SNewProjectWizard::SetGraphicsPreset),
			TAttribute<EGraphicsPreset::Type>(this, &SNewProjectWizard::GetGraphicsPreset),
			Orient_Vertical);

		TSharedRef<SRichTextBlock> Description = SNew(SRichTextBlock)
			.Text(LOCTEXT("ProjectDialog_GraphicsPresetDescription", "Choose the performance characteristics of your project."))
			.AutoWrapText(true)
			.DecoratorStyleSet(&FEditorStyle::Get());

		AddToProjectSettingsGrid(GridPanel, Enum, Description, CurrentSlot);
	}

	if (!HiddenSettings.Contains(ETemplateSetting::StarterContent))
	{
		TArray<SDecoratedEnumCombo<int32>::FComboOption> StarterContentOptions;
		StarterContentOptions.Add(SDecoratedEnumCombo<int32>::FComboOption(
			0,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "GameProjectDialog.NoStarterContent"),
			LOCTEXT("NoStarterContent", "No Starter Content")));

		// Only add the option to add starter content if its there to add !
		bool bIsStarterAvailable = GameProjectUtils::IsStarterContentAvailableForNewProjects();
		StarterContentOptions.Add(SDecoratedEnumCombo<int32>::FComboOption(
			1,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "GameProjectDialog.IncludeStarterContent"),
			LOCTEXT("IncludeStarterContent", "With Starter Content"),
			bIsStarterAvailable));

		TSharedRef<SWidget> Enum = SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SDecoratedEnumCombo<int32>, MoveTemp(StarterContentOptions))
				.SelectedEnum(this, &SNewProjectWizard::GetCopyStarterContentIndex)
				.OnEnumChanged(this, &SNewProjectWizard::OnSetCopyStarterContent)
				.Orientation(Orient_Vertical)
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(4)
			[
				// Warning when enabled for mobile, since the current starter content is bad for mobile
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Icons.Warning"))
				.ToolTipText(this, &SNewProjectWizard::GetStarterContentWarningTooltip)
				.Visibility(this, &SNewProjectWizard::GetStarterContentWarningVisibility)
			];

		TSharedRef<SRichTextBlock> Description =
			SNew(SRichTextBlock)
			.Text(LOCTEXT("CopyStarterContent_ToolTip", "Enable to include an additional content pack containing simple placeable meshes with basic materials and textures.\nYou can also add the <RichTextBlock.BoldHighlight>Starter Content</> to your project later using <RichTextBlock.BoldHighlight>Content Browser</>."))
			.AutoWrapText(true)
			.DecoratorStyleSet(&FEditorStyle::Get());
		
		AddToProjectSettingsGrid(GridPanel, Enum, Description, CurrentSlot);
	}

#if 0 // @todo: XR settings cannot be shown at the moment as the setting causes issues with binary builds.
	if (!HiddenSettings.Contains(ETemplateSetting::XR))
	{
		TArray<SDecoratedEnumCombo<int32>::FComboOption> VirtualRealityOptions;
		VirtualRealityOptions.Add(SDecoratedEnumCombo<int32>::FComboOption(
			0, FSlateIcon(FEditorStyle::GetStyleSetName(), "GameProjectDialog.XRDisabled"),
			LOCTEXT("XRDisabled", "XR Disabled")));

		VirtualRealityOptions.Add(SDecoratedEnumCombo<int32>::FComboOption(
			1, 
			FSlateIcon(FEditorStyle::GetStyleSetName(), "GameProjectDialog.XREnabled"),
			LOCTEXT("XREnabled", "XR Enabled")));

		TSharedRef<SDecoratedEnumCombo<int32>> Enum = SNew(SDecoratedEnumCombo<int32>, MoveTemp(VirtualRealityOptions))
			.SelectedEnum(this, &SNewProjectWizard::OnGetXREnabled)
			.OnEnumChanged(this, &SNewProjectWizard::OnSetXREnabled)
			.Orientation(Orient_Vertical);

		TSharedRef<SRichTextBlock> Description = SNew(SRichTextBlock)
			.Text(LOCTEXT("ProjectDialog_XREnabledDescription", "Choose if XR should be enabled in the new project."))
			.AutoWrapText(true)
			.DecoratorStyleSet(&FEditorStyle::Get());

		AddToProjectSettingsGrid(GridPanel, Enum, Description, CurrentSlot);
	}
#endif 

	if (!HiddenSettings.Contains(ETemplateSetting::Raytracing))
	{
		TArray<SDecoratedEnumCombo<int32>::FComboOption> RaytracingOptions;
		RaytracingOptions.Add(SDecoratedEnumCombo<int32>::FComboOption(
			0, FSlateIcon(FEditorStyle::GetStyleSetName(), "GameProjectDialog.RaytracingDisabled"),
			LOCTEXT("ProjectDialog_RaytracingDisabled", "Raytracing Disabled")));

		RaytracingOptions.Add(SDecoratedEnumCombo<int32>::FComboOption(
			1,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "GameProjectDialog.RaytracingEnabled"),
			LOCTEXT("ProjectDialog_RaytracingEnabled", "Raytracing Enabled")));

		TSharedRef<SDecoratedEnumCombo<int32>> Enum = SNew(SDecoratedEnumCombo<int32>, MoveTemp(RaytracingOptions))
			.SelectedEnum(this, &SNewProjectWizard::OnGetRaytracingEnabled)
			.OnEnumChanged(this, &SNewProjectWizard::OnSetRaytracingEnabled)
			.Orientation(Orient_Vertical);

		TSharedRef<SRichTextBlock> Description = SNew(SRichTextBlock)
			.Text(LOCTEXT("ProjectDialog_RaytracingDescription", "Choose if real-time raytracing should be enabled in the new project."))
			.AutoWrapText(true)
			.DecoratorStyleSet(&FEditorStyle::Get());

		AddToProjectSettingsGrid(GridPanel, Enum, Description, CurrentSlot);
	}

	return GridPanel;
}
#undef LOCTEXT_NAMESPACE
