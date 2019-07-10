// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkClientPanelToolbar.h"

#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "ILiveLinkSource.h"
#include "ISettingsModule.h"
#include "LiveLinkClient.h"
#include "LiveLinkEditorPrivate.h"
#include "LiveLinkPreset.h"
#include "LiveLinkRole.h"
#include "LiveLinkRoleTrait.h"
#include "LiveLinkSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"

#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/SlateDelegates.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/STextEntryPopup.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SErrorText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"


#define LOCTEXT_NAMESPACE "LiveLinkClientPanel"


/** Dialog to create a new virtual subject */
class SVirtualSubjectCreateDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVirtualSubjectCreateDialog) {}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs)
	{
		static const FName DefaultVirtualSubjectName = TEXT("Virtual");
		bOkClicked = false;
		VirtualSubjectClass = nullptr;
		VirtualSubjectName = DefaultVirtualSubjectName;

		TSharedPtr<STextEntryPopup> TextEntry;
		SAssignNew(TextEntry, STextEntryPopup)
			.Label(LOCTEXT("AddVirtualSubjectName", "New Virtual Subject Name"))
			.DefaultText(FText::FromName(VirtualSubjectName))
			.OnTextChanged(this, &SVirtualSubjectCreateDialog::HandleAddVirtualSubjectChanged);

		VirtualSubjectTextWidget = TextEntry;

		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
			[
				SNew(SBox)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Fill)
					.AutoHeight()
					[
						TextEntry->AsShared()
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.0)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						.Content()
						[
							SAssignNew(RoleClassPicker, SVerticalBox)
						]
					]

					// Ok/Cancel buttons
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Bottom)
					.Padding(8)
					[
						SNew(SUniformGridPanel)
						.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
						.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
						.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
						+ SUniformGridPanel::Slot(0, 0)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
							.OnClicked(this, &SVirtualSubjectCreateDialog::OkClicked)
							.Text(LOCTEXT("AddVirtualSubjectAdd", "Add"))
							.IsEnabled(this, &SVirtualSubjectCreateDialog::IsVirtualSubjectClassSelected)
						]
						+ SUniformGridPanel::Slot(1, 0)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
							.OnClicked(this, &SVirtualSubjectCreateDialog::CancelClicked)
							.Text(LOCTEXT("AddVirtualSubjectCancel", "Cancel"))
						]
					]
				]
			]
		];

		MakeRoleClassPicker();
	}

	bool IsVirtualSubjectClassSelected() const
	{
		return VirtualSubjectClass != nullptr;
	}

	bool ConfigureVirtualSubject(FLiveLinkClient* InLiveLinkClient)
	{
		LiveLinkClient = InLiveLinkClient;

		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(LOCTEXT("CreateVirtualSubjectCreation", "Create Virtual Subject"))
			.ClientSize(FVector2D(400, 300))
			.SupportsMinimize(false)
			.SupportsMaximize(false)
			[
				AsShared()
			];

		PickerWindow = Window;

		GEditor->EditorAddModalWindow(Window);

		return bOkClicked;
	}

private:

	class FLiveLinkRoleClassFilter : public IClassViewerFilter
	{
	public:
		TArray<UClass*> ValidRoles;
		FLiveLinkRoleClassFilter()
		{
			for (TSubclassOf<ULiveLinkVirtualSubject> VSubjectClass : FLiveLinkRoleTrait::GetVirtualSubjectClasses())
			{
				if (VSubjectClass->GetDefaultObject<ULiveLinkVirtualSubject>()->GetRole() != nullptr)
				{
					ValidRoles.AddUnique(VSubjectClass.Get());
				}
			}
		}

		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
		{
			if (ValidRoles.Contains(InClass))
			{
				return !InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_HideDropDown | CLASS_Deprecated);
			}
			return false;
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return InUnloadedClassData->IsChildOf(ULiveLinkVirtualSubject::StaticClass());
		}
	};

	/** Creates the combo menu for the role class */
	void MakeRoleClassPicker()
	{
		// Load the classviewer module to display a class picker
		FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

		// Fill in options
		FClassViewerInitializationOptions Options;
		Options.Mode = EClassViewerMode::ClassPicker;

		Options.ClassFilter = MakeShared<FLiveLinkRoleClassFilter>();

		RoleClassPicker->ClearChildren();
		RoleClassPicker->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("VirtualSubjectRole", "Virtual Subject Role:"))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
		];

		RoleClassPicker->AddSlot()
		[
			ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateSP(this, &SVirtualSubjectCreateDialog::OnClassPicked))
		];
	}

	/** Handler for when a parent class is selected */
	void OnClassPicked(UClass* ChosenClass)
	{
		VirtualSubjectClass = ChosenClass;
	}

	/** Handler for when ok is clicked */
	FReply OkClicked()
	{
		if (LiveLinkClient)
		{
			LiveLinkClient->AddVirtualSubject(VirtualSubjectName, VirtualSubjectClass);
		}

		CloseDialog(true);

		return FReply::Handled();
	}

	void CloseDialog(bool bWasPicked = false)
	{
		bOkClicked = bWasPicked;
		if (PickerWindow.IsValid())
		{
			PickerWindow.Pin()->RequestDestroyWindow();
		}
	}

	/** Handler for when cancel is clicked */
	FReply CancelClicked()
	{
		CloseDialog();
		return FReply::Handled();
	}

	FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
	{
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			CloseDialog();
			return FReply::Handled();
		}
		return SWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}

	void HandleAddVirtualSubjectChanged(const FText& NewSubjectName)
	{
		TSharedPtr<STextEntryPopup> VirtualSubjectTextWidgetPin = VirtualSubjectTextWidget.Pin();
		if (VirtualSubjectTextWidgetPin.IsValid())
		{
			TArray<FLiveLinkSubjectKey> SubjectKey = LiveLinkClient->GetSubjects(true, true);
			FName SubjectName = *NewSubjectName.ToString();
			VirtualSubjectName = SubjectName;

			if (SubjectName.IsNone())
			{
				VirtualSubjectTextWidgetPin->SetError(LOCTEXT("VirtualInvalidName", "Invalid Virtual Subject"));
			}
			else if (SubjectKey.FindByPredicate([SubjectName](const FLiveLinkSubjectKey& Key) { return Key.SubjectName == SubjectName; }))
			{
				VirtualSubjectTextWidgetPin->SetError(LOCTEXT("VirtualExistingName", "Subject already exist"));
			}
			else
			{
				VirtualSubjectTextWidgetPin->SetError(FText::GetEmpty());
			}
		}
	}

private:
	FLiveLinkClient* LiveLinkClient;

	TWeakPtr<STextEntryPopup> VirtualSubjectTextWidget;

	/** A pointer to the window that is asking the user to select a role class */
	TWeakPtr<SWindow> PickerWindow;

	/** The container for the role Class picker */
	TSharedPtr<SVerticalBox> RoleClassPicker;

	/** The virtual subject's class */
	TSubclassOf<ULiveLinkVirtualSubject> VirtualSubjectClass;

	/** The virtual subject's name */
	FName VirtualSubjectName;

	/** True if Ok was clicked */
	bool bOkClicked;
};


void SLiveLinkClientPanelToolbar::Construct(const FArguments& Args, FLiveLinkClient* InClient)
{
	Client = InClient;

	TArray<UClass*> Results;
	GetDerivedClasses(ULiveLinkSourceFactory::StaticClass(), Results, true);
	for (UClass* SourceFactory : Results)
	{
		if (!SourceFactory->HasAllClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			Factories.Add(NewObject<ULiveLinkSourceFactory>(GetTransientPackage(), SourceFactory));
		}
	}

	const int32 ButtonBoxSize = 28;
	FMargin ButtonPadding(4.f);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(.0f)
			.AutoWidth()
			[
				// The green button containing the "+ Add Source" items
				SAssignNew(AddSourceButton, SComboButton)
				.ToolTipText(LOCTEXT("AddSource_ToolTip", "Add a new live link source"))
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
				.ForegroundColor(FLinearColor::White)
				.ContentPadding(FMargin(4, 0))
				.OnGetMenuContent(this, &SLiveLinkClientPanelToolbar::OnGenerateSourceMenu)
				.OnMenuOpenChanged(this, &SLiveLinkClientPanelToolbar::OnGeneratedSourceMenuOpenChanged)
				.HasDownArrow(false)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					// The "+" sign.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0, 1))
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
						.Text(FEditorFontGlyphs::Plus)
					]
					// The "Create Session" text.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(4, 0, 0, 0))
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.Text(LOCTEXT("AddSource", "Source"))
					]
					// The caret sign.
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(4, 0, 0, 0)
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
						.Text(FEditorFontGlyphs::Caret_Down)
					]
				]
			]

			+ SHorizontalBox::Slot()
			.Padding(8.f, 0.f, 0.f, 0.f)
			.AutoWidth()
			[
				SNew(SComboButton)
				.ContentPadding(4.f)
				.ComboButtonStyle(FLiveLinkEditorPrivate::GetStyleSet(), "ComboButton")
				.OnGetMenuContent(this, &SLiveLinkClientPanelToolbar::OnPresetGeneratePresetsMenu)
				.ForegroundColor(FSlateColor::UseForeground())
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.Padding(4.f, 0.f, 4.f, 0.f)
					.AutoWidth()
					[
						SNew(SImage)
						.Image(FSlateIconFinder::FindIconBrushForClass(ULiveLinkPreset::StaticClass()))
					]
					+ SHorizontalBox::Slot()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PresetsToolbarButton", "Presets"))
					]
				]
			]

			+ SHorizontalBox::Slot()
			.Padding(8.f, 0.f, 0.f, 0.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				//.ContentPadding(FMargin(8.f, 0.f, 0.f, 0.f))
				.ToolTipText(LOCTEXT("RevertChanges_Text", "Revert all changes made to this take back its original state (either its original preset, or an empty preset)."))
				.ForegroundColor(FSlateColor::UseForeground())
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.OnClicked(this, &SLiveLinkClientPanelToolbar::OnRevertChanges)
				.IsEnabled(this, &SLiveLinkClientPanelToolbar::HasLoadedLiveLinkPreset)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
					.Text(FEditorFontGlyphs::Undo)
				]
			]

			+ SHorizontalBox::Slot()
			[
				SNew(SSpacer)
			]

			+ SHorizontalBox::Slot()
			.Padding(.0f)
			.AutoWidth()
			.HAlign(HAlign_Right)
			[
				SNew(SBox)
				.WidthOverride(ButtonBoxSize)
				.HeightOverride(ButtonBoxSize)
				[
					SNew(SCheckBox)
					.Padding(4.f)
					.ToolTipText(LOCTEXT("ShowUserSettings_Tip", "Show/Hide the general user settings for LiveLink"))
					.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
					.ForegroundColor(FSlateColor::UseForeground())
					.IsChecked_Lambda([]() { return ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([](ECheckBoxState CheckState){ FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "LiveLink"); })
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.14"))
						.Text(FEditorFontGlyphs::Cogs)
					]
				]
			]
		]
	];
}

void SLiveLinkClientPanelToolbar::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(Factories);
}

TSharedRef<SWidget> SLiveLinkClientPanelToolbar::OnGenerateSourceMenu()
{
	const bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, NULL);

	MenuBuilder.BeginSection("SourceSection", LOCTEXT("Sources", "Live Link Sources"));

	for (int32 FactoryIndex = 0; FactoryIndex < Factories.Num(); ++FactoryIndex)
	{
		ULiveLinkSourceFactory* FactoryInstance = Factories[FactoryIndex];
		if (FactoryInstance)
		{
			ULiveLinkSourceFactory::EMenuType MenuType = FactoryInstance->GetMenuType();

			if (MenuType == ULiveLinkSourceFactory::EMenuType::SubPanel)
			{
				MenuBuilder.AddSubMenu(
					FactoryInstance->GetSourceDisplayName(),
					FactoryInstance->GetSourceTooltip(),
					FNewMenuDelegate::CreateSP(this, &SLiveLinkClientPanelToolbar::RetrieveFactorySourcePanel, FactoryIndex),
					false);
			}
			else if (MenuType == ULiveLinkSourceFactory::EMenuType::MenuEntry)
			{
				MenuBuilder.AddMenuEntry(
					FactoryInstance->GetSourceDisplayName(),
					FactoryInstance->GetSourceTooltip(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &SLiveLinkClientPanelToolbar::ExecuteCreateSource, FactoryIndex)
					),
					NAME_None,
					EUserInterfaceActionType::Button);
			}
			else
			{
				MenuBuilder.AddMenuEntry(
					FactoryInstance->GetSourceDisplayName(),
					FactoryInstance->GetSourceTooltip(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction(),
						FCanExecuteAction::CreateLambda([](){ return false; })
					),
					NAME_None,
					EUserInterfaceActionType::Button);
			}
		}
	}

	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("VirtualSourceSection", LOCTEXT("VirtualSources", "Live Link Virtual Sources"));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddVirtualSubject", "Add Virtual Subject"),
		LOCTEXT("AddVirtualSubject_Tooltip", "Adds a new virtual subject to live link. Instead of coming from a source a virtual subject is a combination of 2 or more real subjects"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SLiveLinkClientPanelToolbar::AddVirtualSubject)
		),
		NAME_None,
		EUserInterfaceActionType::Button);

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SLiveLinkClientPanelToolbar::OnGeneratedSourceMenuOpenChanged(bool bOpen)
{
	if (!bOpen)
	{
		if (AddSourceButton.IsValid())
		{
			AddSourceButton->SetMenuContent(SNullWidget::NullWidget);
		}
	}
}

void SLiveLinkClientPanelToolbar::RetrieveFactorySourcePanel(FMenuBuilder& MenuBuilder, int32 FactoryIndex)
{
	if (Factories.IsValidIndex(FactoryIndex))
	{
		if (ULiveLinkSourceFactory* FactoryInstance = Factories[FactoryIndex])
		{
			TSharedPtr<SWidget> Widget = FactoryInstance->BuildCreationPanel(ULiveLinkSourceFactory::FOnLiveLinkSourceCreated::CreateSP(this, &SLiveLinkClientPanelToolbar::OnSourceCreated, TSubclassOf<ULiveLinkSourceFactory>(FactoryInstance->GetClass())));
			if (Widget.IsValid())
			{
				MenuBuilder.AddWidget(Widget.ToSharedRef()
					, FText()
					, true);
			}
		}
	}
}

void SLiveLinkClientPanelToolbar::ExecuteCreateSource(int32 FactoryIndex)
{
	if (Factories.IsValidIndex(FactoryIndex))
	{
		if (ULiveLinkSourceFactory* FactoryInstance = Factories[FactoryIndex])
		{
			OnSourceCreated(FactoryInstance->CreateSource(FString()), FString(), FactoryInstance->GetClass());
		}
	}
}

void SLiveLinkClientPanelToolbar::OnSourceCreated(TSharedPtr<ILiveLinkSource> NewSource, FString ConnectionString, TSubclassOf<ULiveLinkSourceFactory> Factory)
{
	if (NewSource.IsValid())
	{
		FGuid NewSourceGuid = Client->AddSource(NewSource);
		if (NewSourceGuid.IsValid())
		{
			if (ULiveLinkSourceSettings* Settings = Client->GetSourceSettings(NewSourceGuid))
			{
				Settings->ConnectionString = ConnectionString;
				Settings->Factory = Factory;
			}
		}
	}
	FSlateApplication::Get().DismissAllMenus();
}

void SLiveLinkClientPanelToolbar::AddVirtualSubject()
{
	TSharedRef<SVirtualSubjectCreateDialog> Dialog = SNew(SVirtualSubjectCreateDialog);
	Dialog->ConfigureVirtualSubject(Client);
}

TSharedRef<SWidget> SLiveLinkClientPanelToolbar::OnPresetGeneratePresetsMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SaveAsPreset_Text", "Save As Preset"),
		LOCTEXT("SaveAsPreset_Tip", "Save the current setup as a new preset that can be imported at a later date"),
		FSlateIcon(FEditorStyle::Get().GetStyleSetName(), "AssetEditor.SaveAsset.Greyscale"),
		FUIAction(
			FExecuteAction::CreateSP(this, &SLiveLinkClientPanelToolbar::OnSaveAsPreset)
		)
	);

	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.SelectionMode = ESelectionMode::Single;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::Column;
		AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.bShowBottomToolbar = true;
		AssetPickerConfig.bAutohideSearchBar = false;
		AssetPickerConfig.bAllowDragging = false;
		AssetPickerConfig.bCanShowClasses = false;
		AssetPickerConfig.bShowPathInColumnView = true;
		AssetPickerConfig.bShowTypeInColumnView = false;
		AssetPickerConfig.bSortByPathInColumnView = false;

		AssetPickerConfig.AssetShowWarningText = LOCTEXT("NoPresets_Warning", "No Presets Found");
		AssetPickerConfig.Filter.ClassNames.Add(ULiveLinkPreset::StaticClass()->GetFName());
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SLiveLinkClientPanelToolbar::OnImportPreset);
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("ImportPreset_MenuSection", "Import Preset"));
	{
		TSharedRef<SWidget> PresetPicker = SNew(SBox)
			.MinDesiredWidth(400.f)
			.MinDesiredHeight(400.f)
			[
				ContentBrowser.CreateAssetPicker(AssetPickerConfig)
			];

		MenuBuilder.AddWidget(PresetPicker, FText(), true, false);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

static bool OpenSaveDialog(const FString& InDefaultPath, const FString& InNewNameSuggestion, FString& OutPackageName)
{
	FSaveAssetDialogConfig SaveAssetDialogConfig;
	{
		SaveAssetDialogConfig.DefaultPath = InDefaultPath;
		SaveAssetDialogConfig.DefaultAssetName = InNewNameSuggestion;
		SaveAssetDialogConfig.AssetClassNames.Add(ULiveLinkPreset::StaticClass()->GetFName());
		SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
		SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SaveLiveLinkPresetDialogTitle", "Save LiveLink Preset");
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);

	if (!SaveObjectPath.IsEmpty())
	{
		OutPackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
		return true;
	}

	return false;
}

bool GetSavePresetPackageName(FString& OutName)
{
	ULiveLinkSettings* ConfigSettings = GetMutableDefault<ULiveLinkSettings>();

	FDateTime Today = FDateTime::Now();

	TMap<FString, FStringFormatArg> FormatArgs;
	FormatArgs.Add(TEXT("date"), Today.ToString());

	// determine default package path
	const FString DefaultSaveDirectory = FString::Format(*ConfigSettings->GetPresetSaveDir().Path, FormatArgs);

	FString DialogStartPath;
	FPackageName::TryConvertFilenameToLongPackageName(DefaultSaveDirectory, DialogStartPath);
	if (DialogStartPath.IsEmpty())
	{
		DialogStartPath = TEXT("/Game");
	}

	// determine default asset name
	FString DefaultName = LOCTEXT("NewLiveLinkPreset", "NewLiveLinkPreset").ToString();

	FString UniquePackageName;
	FString UniqueAssetName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(DialogStartPath / DefaultName, TEXT(""), UniquePackageName, UniqueAssetName);

	FString DialogStartName = FPaths::GetCleanFilename(UniqueAssetName);

	FString UserPackageName;
	FString NewPackageName;

	// get destination for asset
	bool bFilenameValid = false;
	while (!bFilenameValid)
	{
		if (!OpenSaveDialog(DialogStartPath, DialogStartName, UserPackageName))
		{
			return false;
		}

		NewPackageName = FString::Format(*UserPackageName, FormatArgs);

		FText OutError;
		bFilenameValid = FFileHelper::IsFilenameValidForSaving(NewPackageName, OutError);
	}

	ConfigSettings->PresetSaveDir.Path = FPackageName::GetLongPackagePath(UserPackageName);
	ConfigSettings->SaveConfig();
	OutName = MoveTemp(NewPackageName);
	return true;
}

void SLiveLinkClientPanelToolbar::OnSaveAsPreset()
{
	FString PackageName;
	if (!GetSavePresetPackageName(PackageName))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SaveAsPreset", "Save As Preset"));

	// Saving into a new package
	const FString NewAssetName = FPackageName::GetLongPackageAssetName(PackageName);
	UPackage* NewPackage = CreatePackage(nullptr, *PackageName);
	ULiveLinkPreset* NewPreset = NewObject<ULiveLinkPreset>(NewPackage, *NewAssetName, RF_Public | RF_Standalone | RF_Transactional);

	if (NewPreset)
	{
		NewPreset->BuildFromClient();

		NewPreset->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(NewPreset);

		FEditorFileUtils::PromptForCheckoutAndSave({ NewPackage }, false, false);
	}
	LiveLinkPreset = NewPreset;
}


void SLiveLinkClientPanelToolbar::OnImportPreset(const FAssetData& InPreset)
{
	FSlateApplication::Get().DismissAllMenus();

	ULiveLinkPreset* ImportedPreset = CastChecked<ULiveLinkPreset>(InPreset.GetAsset());
	if (ImportedPreset)
	{
		FScopedTransaction Transaction(LOCTEXT("ImportPreset_Transaction", "Import LiveLink Preset"));
		ImportedPreset->ApplyToClient();
	}
	LiveLinkPreset = ImportedPreset;
}

FReply SLiveLinkClientPanelToolbar::OnRevertChanges()
{
	FText WarningMessage(LOCTEXT("Warning_RevertChanges", "Are you sure you want to revert changes? Your current changes will be discarded."));
	if (EAppReturnType::No == FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage))
	{
		return FReply::Handled();
	}

	FScopedTransaction Transaction(LOCTEXT("RevertChanges_Transaction", "Revert Changes"));
	ULiveLinkPreset* CurrentPreset = LiveLinkPreset.Get();
	if (CurrentPreset)
	{
		CurrentPreset->ApplyToClient();
	}

	return FReply::Handled();
}

bool SLiveLinkClientPanelToolbar::HasLoadedLiveLinkPreset() const
{
	return LiveLinkPreset.IsValid();
}

#undef LOCTEXT_NAMESPACE