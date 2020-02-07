// Copyright Epic Games, Inc. All Rights Reserved.

#include "CreateBlueprintFromActorDialog.h"
#include "GameFramework/Actor.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Editor/EditorEngine.h"
#include "Engine/Selection.h"
#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "SClassViewer.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "PackageTools.h"
#include "DetailLayoutBuilder.h"

#define LOCTEXT_NAMESPACE "CreateBlueprintFromActorDialog"

TWeakObjectPtr<AActor> FCreateBlueprintFromActorDialog::ActorOverride = nullptr;

class SSCreateBlueprintPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSCreateBlueprintPicker)
	{}

	SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ARGUMENT(FClassViewerInitializationOptions, Options)
		SLATE_ARGUMENT(ECreateBlueprintFromActorMode, CreateMode)
		SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/** Handler for when a class is picked in the class picker */
	void OnClassPicked(UClass* InChosenClass);

	/** Handler for when "Ok" we selected in the class viewer */
	FReply OnClassPickerConfirmed();

	/** Handler for when "Cancel" we selected in the class viewer */
	FReply OnClassPickerCanceled();

	/** Handler for when "..." is clicked to pick an asset path */
	FReply OnPathPickerSummoned();

	/** Handler for the custom button to hide/unhide the class viewer */
	void OnCustomAreaExpansionChanged(bool bExpanded);

	/** Callback when the user changes the filename for the Blueprint */
	void OnFilenameChanged(const FText& InNewName);

	/** Common function to evaluate whether the asset name given the asset path context, is valid */
	void UpdateFilenameStatus();

	/** select button visibility delegate */
	EVisibility GetSelectButtonVisibility() const;

	ECheckBoxState IsCreateModeChecked(ECreateBlueprintFromActorMode InCreateMode) const
	{
		return (CreateMode == InCreateMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	};

	void OnCreateModeChanged(ECheckBoxState NewCheckedState, ECreateBlueprintFromActorMode InCreateMode) 
	{ 
		if (NewCheckedState == ECheckBoxState::Checked) 
		{ 
			CreateMode = InCreateMode; 
		} 	
	};

	FSlateColor GetCreateModeTextColor(ECreateBlueprintFromActorMode InCreateMode) const
	{ 
		return (CreateMode == InCreateMode ? FSlateColor(FLinearColor(0, 0, 0)) : FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.f))); 
	};

	/** Overridden from SWidget: Called when a key is pressed down - capturing copy */
	FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);

	/** A pointer to the window that is asking the user to select a parent class */
	TWeakPtr<SWindow> WeakParentWindow;

	/** A pointer to a class viewer **/
	TSharedPtr<SClassViewer> ClassViewer;

	/** Filename textbox widget */
	TSharedPtr<SEditableTextBox> FileNameWidget;

	/** The class that was last clicked on */
	UClass* ChosenClass;

	/** The path the asset should be created at */
	FString AssetPath;

	/** The the name for the new asset */
	FString AssetName;

	/** The method to use when creating the actor */
	ECreateBlueprintFromActorMode CreateMode;

	/** A flag indicating that Ok was selected */
	bool bPressedOk;

	/** A flag indicating the current asset name is invalid */
	bool bIsReportingError;
};

void SSCreateBlueprintPicker::Construct(const FArguments& InArgs)
{
	WeakParentWindow = InArgs._ParentWindow;

	bPressedOk = false;
	ChosenClass = nullptr;
	CreateMode = InArgs._CreateMode;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	ClassViewer = StaticCastSharedRef<SClassViewer>(ClassViewerModule.CreateClassViewer(InArgs._Options, FOnClassPicked::CreateSP(this, &SSCreateBlueprintPicker::OnClassPicked)));

	FString PackageName;
	AssetPath = ContentBrowserModule.Get().GetCurrentPath();

	int32 NumSelectedActors = 0;

	bool bCanHarvestComponents = false;
	bool bCanSubclass = true;
	bool bCanCreatePrefab = true;

	for (FSelectionIterator Iter(*GEditor->GetSelectedActors()); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			if (NumSelectedActors == 0)
			{
				AssetName += Actor->GetActorLabel();
				AssetName += TEXT("_");

				bCanSubclass = FKismetEditorUtilities::CanCreateBlueprintOfClass(Actor->GetClass());
			}

			if (Actor->GetClass()->HasAnyClassFlags(CLASS_NotPlaceable))
			{
				bCanCreatePrefab = false;
			}

			bCanHarvestComponents = true;

			++NumSelectedActors;
		}
	}

	bCanSubclass = bCanSubclass && NumSelectedActors == 1;

	AssetName = UPackageTools::SanitizePackageName(AssetName + TEXT("Blueprint"));

	FString BasePath = AssetPath / AssetName;
	AssetToolsModule.Get().CreateUniqueAssetName(BasePath, TEXT(""), PackageName, AssetName);

	TSharedPtr<SGridPanel> CreationMethodSection;

	struct FCreateModeDetails
	{
		FText Label;
		FText Description;
		ECreateBlueprintFromActorMode CreateMode;
		bool bEnabled;
	};

	const FCreateModeDetails CreateModeDetails[3] =
	{
		{ LOCTEXT("CreateMode_Subclass", "New Subclass"), LOCTEXT("CreateMode_Subclass_Description", "Replace the selected actor with an instance of a new Blueprint Class inherited from the selected parent class."), ECreateBlueprintFromActorMode::Subclass, bCanSubclass },
		{ LOCTEXT("CreateMode_ChildActor", "Child Actors"), LOCTEXT("CreateMode_ChildActor_Description", "Replace the selected actors with an instance of a new Blueprint Class inherited from the selected parent class with each of the selected Actors as a Child Actor."), ECreateBlueprintFromActorMode::ChildActor, bCanCreatePrefab },
		{ LOCTEXT("CreateMode_Harvest", "Harvest Components"), LOCTEXT("CreateMode_Harvest_Description", "Replace the selected actors with an instance of a new Blueprint Class inherited from the selected parent class that contains the components."), ECreateBlueprintFromActorMode::Harvest, bCanHarvestComponents }
	};

	const FCheckBoxStyle& RadioStyle = FEditorStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Property.ToggleButton");

	SAssignNew(CreationMethodSection, SGridPanel)
	.FillColumn(1, 1.f);

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(CreateModeDetails); ++Index)
	{
		CreationMethodSection->AddSlot(0, Index)
		.Padding(10.0f, 5.0f, 5.0f, 5.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.Style(&RadioStyle)
			.IsEnabled(CreateModeDetails[Index].bEnabled)
			.IsChecked_Raw(this, &SSCreateBlueprintPicker::IsCreateModeChecked, CreateModeDetails[Index].CreateMode)
			.OnCheckStateChanged(this, &SSCreateBlueprintPicker::OnCreateModeChanged, CreateModeDetails[Index].CreateMode)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.Padding(6, 2)
				[
					SNew(STextBlock)
					.Text(CreateModeDetails[Index].Label)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(this, &SSCreateBlueprintPicker::GetCreateModeTextColor, CreateModeDetails[Index].CreateMode)
				]
			]
		];

		CreationMethodSection->AddSlot(1, Index)
		.Padding(1.0f, 5.0f, 1.0f, 5.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(CreateModeDetails[Index].Description)
			.IsEnabled(CreateModeDetails[Index].bEnabled)
			.AutoWrapText(true)
		];
	}

	ChildSlot
	[
		SNew(SBorder)
		.Visibility(EVisibility::Visible)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		[
			SNew(SBox)
			.Visibility(EVisibility::Visible)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SGridPanel)
					.FillColumn(1, 1.f)
					+SGridPanel::Slot(0, 0)
					.Padding(0.0f, 0.0f, 5.0f, 2.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CreateBlueprintFromActor_NameLabel", "Blueprint Name"))
					]
					+SGridPanel::Slot(1, 0)
					.Padding(0.0f, 0.0f, 0.0f, 5.0f)
					[
						SAssignNew(FileNameWidget, SEditableTextBox)
						.Text(FText::FromString(AssetName))
						.OnTextChanged(this, &SSCreateBlueprintPicker::OnFilenameChanged)
					]
					+SGridPanel::Slot(0, 1)
					.Padding(0.0f, 0.0f, 5.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CreateBlueprintFromActor_PathLabel", "Path"))
					]
					+SGridPanel::Slot(1, 1)
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SNew(SEditableTextBox)
							.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([this]() { return FText::FromString(AssetPath); })))
							.IsReadOnly(true)
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("...")))
							.OnClicked(this, &SSCreateBlueprintPicker::OnPathPickerSummoned)
						]
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SExpandableArea)
					.AreaTitle(LOCTEXT("CreationMethod", "Creation Method"))
					.BodyContent()
					[
						CreationMethodSection.ToSharedRef()
					]
				]
				+SVerticalBox::Slot()
				.FillHeight(1.f)
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SExpandableArea)
					.MaxHeight(320.f)
					.InitiallyCollapsed(false)
					.AreaTitle(NSLOCTEXT("SClassPickerDialog", "AllClassesAreaTitle", "Parent Class"))
					.OnAreaExpansionChanged(this, &SSCreateBlueprintPicker::OnCustomAreaExpansionChanged)
					.BodyContent()
					[
						ClassViewer.ToSharedRef()
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				.Padding(8)
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
					+SUniformGridPanel::Slot(0,0)
					[
						SNew(SButton)
						.Text(NSLOCTEXT("SClassPickerDialog", "ClassPickerSelectButton", "Select"))
						.HAlign(HAlign_Center)
						.Visibility( this, &SSCreateBlueprintPicker::GetSelectButtonVisibility )
						.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
						.OnClicked(this, &SSCreateBlueprintPicker::OnClassPickerConfirmed)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
						.TextStyle(FEditorStyle::Get(), "FlatButton.DefaultTextStyle")
					]
					+SUniformGridPanel::Slot(1,0)
					[
						SNew(SButton)
						.Text(NSLOCTEXT("SClassPickerDialog", "ClassPickerCancelButton", "Cancel"))
						.HAlign(HAlign_Center)
						.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
						.OnClicked(this, &SSCreateBlueprintPicker::OnClassPickerCanceled)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton.Default")
						.TextStyle(FEditorStyle::Get(), "FlatButton.DefaultTextStyle")
					]
				]
			]
		]
	];

	if (WeakParentWindow.IsValid())
	{
		WeakParentWindow.Pin().Get()->SetWidgetToFocusOnActivate(ClassViewer);
	}
}

void SSCreateBlueprintPicker::OnClassPicked(UClass* InChosenClass)
{
	ChosenClass = InChosenClass;
}

FReply SSCreateBlueprintPicker::OnClassPickerConfirmed()
{
	if (ChosenClass == nullptr)
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("EditorFactories", "MustChooseClassWarning", "You must choose a class."));
	}
	else if (bIsReportingError)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidAssetname", "You must specify a valid asset name."));
	}
	else
	{
		bPressedOk = true;

		if (WeakParentWindow.IsValid())
		{
			WeakParentWindow.Pin()->RequestDestroyWindow();
		}
	}
	return FReply::Handled();
}

FReply SSCreateBlueprintPicker::OnClassPickerCanceled()
{
	if (WeakParentWindow.IsValid())
	{
		WeakParentWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

class SSCreateBlueprintPathPicker : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SSCreateBlueprintPathPicker)
	{}

	SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ARGUMENT(FString, AssetPath)
		SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Callback when the selected asset path has changed. */
	void OnSelectAssetPath(const FString& Path) { AssetPath = Path; }

	/** Callback when the "ok" button is clicked. */
	FReply OnClickOk();

	/** Destroys the window when the operation is cancelled. */
	FReply OnClickCancel();

	/** A pointer to the window that is asking the user to select a parent class */
	TWeakPtr<SWindow> WeakParentWindow;

	FString AssetPath;

	bool bPressedOk;
};

void SSCreateBlueprintPathPicker::Construct(const FArguments& InArgs)
{
	WeakParentWindow = InArgs._ParentWindow;

	bPressedOk = false;
	AssetPath = InArgs._AssetPath;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FPathPickerConfig PathPickerConfig;
	PathPickerConfig.DefaultPath = AssetPath;
	PathPickerConfig.OnPathSelected = FOnPathSelected::CreateRaw(this, &SSCreateBlueprintPathPicker::OnSelectAssetPath);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			ContentBrowserModule.Get().CreatePathPicker(PathPickerConfig)
		]
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Right)
		.Padding(0, 20, 0, 0)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(0, 2, 6, 0)
			.AutoWidth()
			[
				SNew(SButton)
				.VAlign(VAlign_Bottom)
				.ContentPadding(FMargin(8, 2, 8, 2))
				.OnClicked(this, &SSCreateBlueprintPathPicker::OnClickOk)
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
				.TextStyle(FEditorStyle::Get(), "FlatButton.DefaultTextStyle")
				.Text(LOCTEXT("OkButtonText", "OK"))
			]
			+ SHorizontalBox::Slot()
			.Padding(0, 2, 0, 0)
			.AutoWidth()
			[
				SNew(SButton)
				.VAlign(VAlign_Bottom)
				.ContentPadding(FMargin(8, 2, 8, 2))
				.OnClicked(this, &SSCreateBlueprintPathPicker::OnClickCancel)
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Default")
				.TextStyle(FEditorStyle::Get(), "FlatButton.DefaultTextStyle")
				.Text(LOCTEXT("CancelButtonText", "Cancel"))
			]
		]
	];
};

FReply SSCreateBlueprintPathPicker::OnClickOk()
{
	bPressedOk = true;

	if (WeakParentWindow.IsValid())
	{
		WeakParentWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SSCreateBlueprintPathPicker::OnClickCancel()
{
	if (WeakParentWindow.IsValid())
	{
		WeakParentWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SSCreateBlueprintPicker::OnPathPickerSummoned()
{
	// Create the window to pick the class
	TSharedRef<SWindow> PickerWindow = SNew(SWindow)
		.Title(LOCTEXT("CreateBlueprintFromActors_PickPath", "Select Path"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(300.f, 400.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SSCreateBlueprintPathPicker> PathPickerDialog = SNew(SSCreateBlueprintPathPicker)
		.ParentWindow(PickerWindow)
		.AssetPath(AssetPath);

	PickerWindow->SetContent(PathPickerDialog);

	GEditor->EditorAddModalWindow(PickerWindow);

	if (PathPickerDialog->bPressedOk)
	{
		AssetPath = PathPickerDialog->AssetPath;
		UpdateFilenameStatus();
	}

	return FReply::Handled();
}

void SSCreateBlueprintPicker::OnCustomAreaExpansionChanged(bool bExpanded)
{
	if (bExpanded && WeakParentWindow.IsValid())
	{
		WeakParentWindow.Pin().Get()->SetWidgetToFocusOnActivate(ClassViewer);
	}
}

void SSCreateBlueprintPicker::OnFilenameChanged(const FText& InNewName)
{
	AssetName = InNewName.ToString();
	UpdateFilenameStatus();
}

void SSCreateBlueprintPicker::UpdateFilenameStatus()
{
	FText ErrorText;
	if (!FFileHelper::IsFilenameValidForSaving(AssetName, ErrorText) || !FName(*AssetName).IsValidObjectName(ErrorText))
	{
		FileNameWidget->SetError(ErrorText);
		bIsReportingError = true;
		return;
	}
	else
	{
		TArray<FAssetData> AssetData;
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().GetAssetsByPath(FName(*AssetPath), AssetData);

		// Check to see if the name conflicts
		for (const FAssetData& Data : AssetData)
		{
			const FString& AssetNameStr = Data.AssetName.ToString();
			if (Data.AssetName.ToString() == AssetName)
			{
				FileNameWidget->SetError(LOCTEXT("AssetInUseError", "Asset name already in use!"));
				bIsReportingError = true;
				return;
			}
		}
	}

	FileNameWidget->SetError(FText::GetEmpty());
	bIsReportingError = false;
}

EVisibility SSCreateBlueprintPicker::GetSelectButtonVisibility() const
{
	EVisibility ButtonVisibility = EVisibility::Hidden;
	if (ChosenClass != nullptr && !bIsReportingError)
	{
		ButtonVisibility = EVisibility::Visible;
	}
	return ButtonVisibility;
}

/** Overridden from SWidget: Called when a key is pressed down - capturing copy */
FReply SSCreateBlueprintPicker::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	WeakParentWindow.Pin().Get()->SetWidgetToFocusOnActivate(ClassViewer);

	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		return OnClassPickerCanceled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		OnClassPickerConfirmed();
	}
	else
	{
		return ClassViewer->OnKeyDown(MyGeometry, InKeyEvent);
	}
	return FReply::Handled();
}

void FCreateBlueprintFromActorDialog::OpenDialog(ECreateBlueprintFromActorMode CreateMode, AActor* InActorOverride )
{
	ActorOverride = InActorOverride;

	FClassViewerInitializationOptions ClassViewerOptions;
	ClassViewerOptions.Mode = EClassViewerMode::ClassPicker;
	ClassViewerOptions.DisplayMode = EClassViewerDisplayMode::TreeView;
	ClassViewerOptions.bShowObjectRootClass = true;
	ClassViewerOptions.bIsPlaceableOnly = true;
	ClassViewerOptions.bIsBlueprintBaseOnly = true;
	ClassViewerOptions.bShowUnloadedBlueprints = true;
	ClassViewerOptions.bEnableClassDynamicLoading = true;
	ClassViewerOptions.NameTypeToDisplay = EClassViewerNameTypeToDisplay::Dynamic;

	if (InActorOverride)
	{
		class FBlueprintFromActorParentFilter : public IClassViewerFilter
		{
		public:
			TSet<const UClass*> AllowedClass;

			virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
			{
				return InFilterFuncs->IfInChildOfClassesSet(AllowedClass, InClass) == EFilterReturn::Passed;
			}

			virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
			{
				return InFilterFuncs->IfInChildOfClassesSet(AllowedClass, InUnloadedClassData) == EFilterReturn::Passed;
			}
		};

		TSharedPtr<FBlueprintFromActorParentFilter> Filter = MakeShareable(new FBlueprintFromActorParentFilter);
		ClassViewerOptions.ClassFilter = Filter;
		Filter->AllowedClass.Add(InActorOverride->GetClass());

		ClassViewerOptions.InitiallySelectedClass = InActorOverride->GetClass();
	}
	else
	{
		ClassViewerOptions.InitiallySelectedClass = AActor::StaticClass();
	}

	// Create the window to pick the class
	TSharedRef<SWindow> PickerWindow = SNew(SWindow)
		.Title(LOCTEXT("CreateBlueprintFromActors","Create Blueprint From Selection"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(600.f, 600.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SSCreateBlueprintPicker> ClassPickerDialog = SNew(SSCreateBlueprintPicker)
		.ParentWindow(PickerWindow)
		.Options(ClassViewerOptions)
		.CreateMode(CreateMode);

	PickerWindow->SetContent(ClassPickerDialog);

//	FSlateApplication::Get().AddWindow(PickerWindow);
	GEditor->EditorAddModalWindow(PickerWindow);

	if (ClassPickerDialog->bPressedOk)
	{
		FString NewAssetName = ClassPickerDialog->AssetPath / ClassPickerDialog->AssetName;

		OnCreateBlueprint(NewAssetName, ClassPickerDialog->ChosenClass, ClassPickerDialog->CreateMode);
	}
}

void FCreateBlueprintFromActorDialog::OnCreateBlueprint(const FString& InAssetPath, UClass* ParentClass, ECreateBlueprintFromActorMode CreateMode)
{
	UBlueprint* Blueprint = nullptr;

	switch (CreateMode) 
	{
		case ECreateBlueprintFromActorMode::Harvest:
	{
		TArray<AActor*> Actors;

		USelection* SelectedActors = GEditor->GetSelectedActors();
		for(FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
		{
			// We only care about actors that are referenced in the world for literals, and also in the same level as this blueprint
				if (AActor* Actor = Cast<AActor>(*Iter))
			{
				Actors.Add(Actor);
			}
		}

			const bool bReplaceActor = true;
			Blueprint = FKismetEditorUtilities::HarvestBlueprintFromActors(InAssetPath, Actors, bReplaceActor, ParentClass);
	}
		break;

		case ECreateBlueprintFromActorMode::Subclass:
	{
		AActor* ActorToUse = ActorOverride.Get();

		if( !ActorToUse )
		{
			TArray< UObject* > SelectedActors;
			GEditor->GetSelectedActors()->GetSelectedObjects(AActor::StaticClass(), SelectedActors);
				check(SelectedActors.Num() == 1);
			ActorToUse =  Cast<AActor>(SelectedActors[0]);
		}

		const bool bReplaceActor = true;
		Blueprint = FKismetEditorUtilities::CreateBlueprintFromActor(InAssetPath, ActorToUse, bReplaceActor, false, ParentClass);
	}
		break;

		case ECreateBlueprintFromActorMode::ChildActor:
		{
			TArray<AActor*> Actors;

			USelection* SelectedActors = GEditor->GetSelectedActors();
			for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
			{
				// We only care about actors that are referenced in the world for literals, and also in the same level as this blueprint
				if (AActor* Actor = Cast<AActor>(*Iter))
				{
					Actors.Add(Actor);
				}
			}

			const bool bReplaceActor = true;
			Blueprint = FKismetEditorUtilities::CreateBlueprintFromActors(InAssetPath, Actors, bReplaceActor, ParentClass);
		}
		break;
	}

	if(Blueprint)
	{
		// Select the newly created blueprint in the content browser, but don't activate the browser
		TArray<UObject*> Objects;
		Objects.Add(Blueprint);
		GEditor->SyncBrowserToObjects( Objects, false );
	}
	else
	{
		FNotificationInfo Info( LOCTEXT("CreateBlueprintFromActorFailed", "Unable to create a blueprint from actor.") );
		Info.ExpireDuration = 3.0f;
		Info.bUseLargeFont = false;
		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if ( Notification.IsValid() )
		{
			Notification->SetCompletionState( SNotificationItem::CS_Fail );
		}
	}
}


#undef LOCTEXT_NAMESPACE
