// Copyright Epic Games, Inc. All Rights Reserved.


#include "SControlRigBaseListWidget.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AssetData.h"
#include "EditorStyleSet.h"

#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "ScopedTransaction.h"

#include "ControlRig.h"
#include "UnrealEdGlobals.h"
#include "ControlRigEditMode.h"
#include "Tools/ControlRigPose.h"
#include "EditorModeManager.h"

#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "Tools/ControlRigPoseProjectSettings.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "ControlRigEditModeCommands.h"
#include "Tools/CreateControlAssetRigSettings.h"
#include "FileHelpers.h"
#include "Tools/ControlRigPoseMirrorSettings.h"
#include "ObjectTools.h"


#define LOCTEXT_NAMESPACE "ControlRigBaseListWidget"

enum class FControlRigAssetType {
	ControlRigPose,
	ControlRigAnimation,
	ControlRigSelectionSet

};

/////////////////////////////////////////////////////
// SControlRigPoseAnimSelectionToolbar

DECLARE_DELEGATE_OneParam(FCreateControlAssetDelegate,
FString);

struct FCreateControlAssetRigDialog
{
	static void GetControlAssetParams(FControlRigAssetType Type, FCreateControlAssetDelegate& Delegate);

};

class SCreateControlAssetRigDialog : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SCreateControlAssetRigDialog) :
		_AssetType(FControlRigAssetType::ControlRigPose) {}

	SLATE_ARGUMENT(FControlRigAssetType, AssetType)

		SLATE_END_ARGS()

		FControlRigAssetType AssetType;

	~SCreateControlAssetRigDialog()
	{
	}

	void Construct(const FArguments& InArgs)
	{
		AssetType = InArgs._AssetType;

		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bShowPropertyMatrixButton = false;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.ViewIdentifier = "Create Control Asset";

		DetailView = PropertyEditor.CreateDetailView(DetailsViewArgs);

		ChildSlot
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
			[
				DetailView.ToSharedRef()
			]

		+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(5.f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(10, 5))
			.Text(LOCTEXT("CreateControlAssetRig", "Create Asset"))
			.OnClicked(this, &SCreateControlAssetRigDialog::OnCreateControlAssetRig)
			]

			];

		switch (AssetType)
		{
		case FControlRigAssetType::ControlRigPose:
		{
			UCreateControlPoseAssetRigSettings* AssetSettings = GetMutableDefault<UCreateControlPoseAssetRigSettings>();
			DetailView->SetObject(AssetSettings);
			break;
		}
		/*
		case FControlRigAssetType::ControlRigAnimation:
		{
			UCreateControlAnimationAssetRigSettings* AssetSettings = GetMutableDefault<UCreateControlAnimationAssetRigSettings>();
			DetailView->SetObject(AssetSettings);
			break;
		}
		case FControlRigAssetType::ControlRigSelectionSet:
		{
			UCreateControlSelectionSetAssetRigSettings* AssetSettings = GetMutableDefault<UCreateControlSelectionSetAssetRigSettings>();
			DetailView->SetObject(AssetSettings);
			break;
		}
		*/
		};

	}

	void SetDelegate(FCreateControlAssetDelegate& InDelegate)
	{
		Delegate = InDelegate;
	}

private:

	FReply OnCreateControlAssetRig()
	{
		FString AssetName("");
		switch (AssetType)
		{
		case FControlRigAssetType::ControlRigPose:
		{
			UCreateControlPoseAssetRigSettings* AssetSettings = GetMutableDefault<UCreateControlPoseAssetRigSettings>();
			AssetName = AssetSettings->AssetName;
			break;
		}
		/*
		case FControlRigAssetType::ControlRigAnimation:
		{
			UCreateControlAnimationAssetRigSettings* AssetSettings = GetMutableDefault<UCreateControlAnimationAssetRigSettings>();
			AssetName = AssetSettings->AssetName;
			break;
		}
		case FControlRigAssetType::ControlRigSelectionSet:
		{
			UCreateControlSelectionSetAssetRigSettings* AssetSettings = GetMutableDefault<UCreateControlSelectionSetAssetRigSettings>();
			AssetName = AssetSettings->AssetName;
			break;
		}
		*/
		};

		TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
		if (Delegate.IsBound())
		{
			Delegate.Execute(AssetName);
		}
		if (Window.IsValid())
		{
			Window->RequestDestroyWindow();
		}
		return FReply::Handled();
	}
	TSharedPtr<IDetailsView> DetailView;
	FCreateControlAssetDelegate  Delegate;

};


void FCreateControlAssetRigDialog::GetControlAssetParams(FControlRigAssetType Type, FCreateControlAssetDelegate& InDelegate)
{
	FText TitleText;
	switch (Type)
	{
	case FControlRigAssetType::ControlRigPose:
		TitleText = NSLOCTEXT("ControlRig", "CreateControlAssetRig", "Create Control Rig Pose");
		break;
	case FControlRigAssetType::ControlRigAnimation:
		TitleText = NSLOCTEXT("ControlRig", "CreateControlAssetRig", "Create Control Rig Animation");
		break;
	case FControlRigAssetType::ControlRigSelectionSet:
		TitleText = NSLOCTEXT("ControlRig", "CreateControlAssetRig", "Create Control Rig Selection Set");
		break;
	};


	// Create the window to choose our options
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(TitleText)
		.HasCloseButton(true)
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(400.0f, 200.0f))
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMinimize(false);

	TSharedRef<SCreateControlAssetRigDialog> DialogWidget = SNew(SCreateControlAssetRigDialog).AssetType(Type);
	DialogWidget->SetDelegate(InDelegate);
	Window->SetContent(DialogWidget);

	FSlateApplication::Get().AddWindow(Window);

}


/////////////////////////////////////////////////////
// SControlRigPoseAnimSelectionToolbar

class SControlRigPoseAnimSelectionToolbar : public SCompoundWidget
{

public:
	/** Default constructor. */
	SControlRigPoseAnimSelectionToolbar();

	/** Virtual destructor. */
	virtual ~SControlRigPoseAnimSelectionToolbar();

	SLATE_BEGIN_ARGS(SControlRigPoseAnimSelectionToolbar) {}
	SLATE_ARGUMENT(SControlRigBaseListWidget*, OwningControlRigWidget)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs);


	void MakeControlRigAssetDialog(FControlRigAssetType Type, bool bSelectAll);
	
	//void ToggleFilter(FControlRigAssetType Type);
	//bool IsOnToggleFilter(FControlRigAssetType Type) const;

	//It's parent so will always be there..
	SControlRigBaseListWidget* OwningControlRigWidget;

};


SControlRigPoseAnimSelectionToolbar::SControlRigPoseAnimSelectionToolbar() :OwningControlRigWidget(nullptr)
{
}

SControlRigPoseAnimSelectionToolbar::~SControlRigPoseAnimSelectionToolbar()
{
}


void SControlRigPoseAnimSelectionToolbar::Construct(const FArguments& InArgs)
{

	OwningControlRigWidget = InArgs._OwningControlRigWidget;

	FToolBarBuilder ToolbarBuilder(TSharedPtr<const FUICommandList>(), FMultiBoxCustomization::None, TSharedPtr<FExtender>(), true);
	FUIAction CreatePoseDialog(
		FExecuteAction::CreateRaw(this, &SControlRigPoseAnimSelectionToolbar::MakeControlRigAssetDialog, FControlRigAssetType::ControlRigPose,false));
	/*
	FUIAction CreatePoseFromAllDialog(
		FExecuteAction::CreateRaw(this, &SControlRigPoseAnimSelectionToolbar::MakeControlRigAssetDialog, FControlRigAssetType::ControlRigPose,true));
	*/
	/*
	FUIAction CreateAnimationDialog(
		FExecuteAction::CreateRaw(this, &SControlRigPoseAnimSelectionToolbar::MakeControlRigAssetDialog, FControlRigAssetType::ControlRigAnimation));
	FUIAction CreateSelectionSetDialog(
		FExecuteAction::CreateRaw(this, &SControlRigPoseAnimSelectionToolbar::MakeControlRigAssetDialog, FControlRigAssetType::ControlRigSelectionSet));
	*/

	ToolbarBuilder.BeginSection("Create");
	{
		ToolbarBuilder.AddToolBarButton(CreatePoseDialog,
			NAME_None,
			LOCTEXT("CreatePose", "Create Pose From Selection"),
			LOCTEXT("CreatePoseTooltip", "Create Pose Asset From Selection"),
			FSlateIcon(),
			EUserInterfaceActionType::Button
		);
		/*
		ToolbarBuilder.AddToolBarButton(CreatePoseFromAllDialog,
			NAME_None,
			LOCTEXT("CreatePoseAll", "Create Pose From All Controls"),
			LOCTEXT("CreatePoseAllTooltip", "Create Pose Asset From All Controls Not Just Selected"),
			FSlateIcon(),
			EUserInterfaceActionType::Button
			
		);
		*/
		/** For now just making pose assets
		ToolbarBuilder.AddToolBarButton(CreateAnimationDialog,
			NAME_None,
			LOCTEXT("CreateAnimation", "Create Animation"),
			LOCTEXT("CreateAnimationTooltip", "Create Control Rig Animation Asset"),
			FSlateIcon(),
			EUserInterfaceActionType::Button
		);
		ToolbarBuilder.AddToolBarButton(CreateSelectionSetDialog,
			NAME_None,
			LOCTEXT("CreateSelectionSet", "Create Selection Set"),
			LOCTEXT("CreateSelectionSetTooltip", "Create Selection Set Asset"),
			FSlateIcon(),
			EUserInterfaceActionType::Button
		);
		*/

	}
	/** TODO if we have multiple asset types we will put back the filters.
	ToolbarBuilder.EndSection();
	FUIAction ToggleFilterPose(
		FExecuteAction::CreateRaw(this, &SControlRigPoseAnimSelectionToolbar::ToggleFilter, FControlRigAssetType::ControlRigPose),
		FCanExecuteAction(),
		FIsActionChecked::CreateRaw(this, &SControlRigPoseAnimSelectionToolbar::IsOnToggleFilter, FControlRigAssetType::ControlRigPose));
	FUIAction ToggleFilterAnimation(
		FExecuteAction::CreateRaw(this, &SControlRigPoseAnimSelectionToolbar::ToggleFilter, FControlRigAssetType::ControlRigAnimation),
		FCanExecuteAction(),
		FIsActionChecked::CreateRaw(this, &SControlRigPoseAnimSelectionToolbar::IsOnToggleFilter, FControlRigAssetType::ControlRigAnimation));
	FUIAction ToggleFilterSelectionSet(
		FExecuteAction::CreateRaw(this, &SControlRigPoseAnimSelectionToolbar::ToggleFilter, FControlRigAssetType::ControlRigSelectionSet),
		FCanExecuteAction(),
		FIsActionChecked::CreateRaw(this, &SControlRigPoseAnimSelectionToolbar::IsOnToggleFilter, FControlRigAssetType::ControlRigSelectionSet));

	FToolBarBuilder RightToolbarBuilder(TSharedPtr<const FUICommandList>(), FMultiBoxCustomization::None, TSharedPtr<FExtender>(), true);
	ToolbarBuilder.BeginSection("Filters");
	{
		RightToolbarBuilder.AddToolBarButton(ToggleFilterPose,
			NAME_None,
			LOCTEXT("FilterPose", "Filter Pose"),
			LOCTEXT("FilterPoseTooltip", "Toggle to Show or Hide Pose Assets"),
			FSlateIcon(),
			EUserInterfaceActionType::ToggleButton
		);
		RightToolbarBuilder.AddToolBarButton(ToggleFilterAnimation,
			NAME_None,
			LOCTEXT("FilterAnimation", "Filter Animation"),
			LOCTEXT("FilterAnimationTooltip", "Toggle to Show or Hide Animation Assets"),
			FSlateIcon(),
			EUserInterfaceActionType::ToggleButton
		);
		RightToolbarBuilder.AddToolBarButton(ToggleFilterSelectionSet,
			NAME_None,
			LOCTEXT("FilterSelectionSet", "Filter SelectionSet"),
			LOCTEXT("FilterSelectionaTooltip", "Toggle to Show or Hide Selction Set Assets"),
			FSlateIcon(),
			EUserInterfaceActionType::ToggleButton
		);

	}
	ToolbarBuilder.EndSection();
	*/
	// Create the tool bar!
	ChildSlot
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.FillWidth(1.0)
		.Padding(0.0f)
		[
			SNew(SBorder)
			.Padding(0)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
		[
			ToolbarBuilder.MakeWidget()
		]
		]
	/*
		+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(0.0f)
			[
				SNew(SBorder)
				.Padding(0)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
			[
				RightToolbarBuilder.MakeWidget()
			]
			]*/
		];
}

void SControlRigPoseAnimSelectionToolbar::MakeControlRigAssetDialog(FControlRigAssetType Type, bool bSelectAll)
{
	FCreateControlAssetDelegate GetNameCallback = FCreateControlAssetDelegate::CreateLambda([this, Type, bSelectAll](FString AssetName)
	{
		if (OwningControlRigWidget)
		{
			FString Path = OwningControlRigWidget->GetCurrentlySelectedPath();

			FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
			if (ControlRigEditMode && ControlRigEditMode->GetControlRig(true))
			{
				UObject* NewAsset = nullptr;
				switch (Type)
				{
				case FControlRigAssetType::ControlRigPose:
					NewAsset = FControlRigToolAsset::SaveAsset<UControlRigPoseAsset>(ControlRigEditMode->GetControlRig(true), Path, AssetName, bSelectAll);

					break;
				case FControlRigAssetType::ControlRigAnimation:
					break;
				case FControlRigAssetType::ControlRigSelectionSet:
					break;
				default:
					break;
				};
				if (NewAsset)
				{
					FControlRigView::CaptureThumbnail(NewAsset);
				}
				OwningControlRigWidget->SelectThisAsset(NewAsset);

			}
		}
	});

	FCreateControlAssetRigDialog::GetControlAssetParams(Type, GetNameCallback);
}
/*
void SControlRigPoseAnimSelectionToolbar::ToggleFilter(FControlRigAssetType Type)
{
	UControlRigPoseProjectSettings* PoseSettings = GetMutableDefault<UControlRigPoseProjectSettings>();
	if (PoseSettings)
	{
		switch (Type)
		{
		case FControlRigAssetType::ControlRigPose:
			PoseSettings->bFilterPoses = PoseSettings->bFilterPoses ? false : true;
			break;
		case FControlRigAssetType::ControlRigAnimation:
			PoseSettings->bFilterAnimations = PoseSettings->bFilterAnimations ? false : true;
			break;
		case FControlRigAssetType::ControlRigSelectionSet:
			PoseSettings->bFilterSelectionSets = PoseSettings->bFilterSelectionSets ? false : true;
			break;
		default:
			break;
		};
	}
	if (OwningControlRigWidget)
	{
		OwningControlRigWidget->FilterChanged();
	}
}

bool SControlRigPoseAnimSelectionToolbar::IsOnToggleFilter(FControlRigAssetType Type) const
{
	const UControlRigPoseProjectSettings* PoseSettings = GetDefault<UControlRigPoseProjectSettings>();
	if (PoseSettings)
	{
		switch (Type)
		{
		case FControlRigAssetType::ControlRigPose:
			return PoseSettings->bFilterPoses;
		case FControlRigAssetType::ControlRigAnimation:
			return PoseSettings->bFilterAnimations;
		case FControlRigAssetType::ControlRigSelectionSet:
			return PoseSettings->bFilterSelectionSets;
		default:
			break;
		};
	}

	return false;
}
*/

/////////////////////////////////////////////////////
// SControlRigBaseListWidget, Main Dialog Window holding the path picker, asset view and pose view.

void SControlRigBaseListWidget::Construct(const FArguments& InArgs)
{

	FControlRigEditMode* EditMode = GetEditMode();
	BindCommands();


	const UControlRigPoseProjectSettings* PoseSettings = GetDefault<UControlRigPoseProjectSettings>();

	FString PosesDir = PoseSettings->GetAssetPath();
	CurrentlySelectedPath = PosesDir;
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	// Configure filter for asset picker
	FAssetPickerConfig AssetPickerConfig;

	//AssetPickerConfig.Filter.bRecursiveClasses = true;
	AssetPickerConfig.Filter.ClassNames.Add(UControlRigPoseAsset::StaticClass()->GetFName());

	AssetPickerConfig.InitialAssetViewType = EAssetViewType::Tile;
	AssetPickerConfig.bAllowDragging = false;
	AssetPickerConfig.bCanShowFolders = true;
	AssetPickerConfig.bCanShowRealTimeThumbnails = true;
	AssetPickerConfig.ThumbnailLabel = EThumbnailLabel::AssetName;
	AssetPickerConfig.bFocusSearchBoxWhenOpened = false;
	AssetPickerConfig.Filter.PackagePaths.Add(FName(*PosesDir));
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SControlRigBaseListWidget::OnAssetSelected);
	AssetPickerConfig.OnAssetsActivated = FOnAssetsActivated::CreateSP(this, &SControlRigBaseListWidget::OnAssetsActivated);
	AssetPickerConfig.SaveSettingsName = TEXT("ControlPoseDialog");
	AssetPickerConfig.bCanShowDevelopersFolder = true;
	AssetPickerConfig.OnFolderEntered = FOnPathSelected::CreateSP(this, &SControlRigBaseListWidget::HandleAssetViewFolderEntered);
	AssetPickerConfig.OnGetAssetContextMenu = FOnGetAssetContextMenu::CreateSP(this, &SControlRigBaseListWidget::OnGetAssetContextMenu);	
	AssetPickerConfig.SetFilterDelegates.Add(&SetFilterDelegate);
	AssetPickerConfig.GetCurrentSelectionDelegates.Add(&GetCurrentSelectionDelegate);
	AssetPickerConfig.SelectionMode = ESelectionMode::Multi;
	AssetPickerConfig.bAllowDragging = false;
	AssetPickerConfig.AssetShowWarningText = LOCTEXT("NoPoses_Warning", "No Poses Found, Create One Using Button In Upper Left Corner");
	AssetPickerConfig.OnIsAssetValidForCustomToolTip = FOnIsAssetValidForCustomToolTip::CreateLambda([](const FAssetData& AssetData) {return AssetData.IsAssetLoaded(); });
	//AssetPickerConfig.OnGetCustomAssetToolTip = FOnGetCustomAssetToolTip::CreateSP(this, &SControlRigBaseListWidget::CreateCustomAssetToolTip);

	FPathPickerConfig PathPickerConfig;
	PathPickerConfig.bAddDefaultPath = true;
	PathPickerConfig.DefaultPath = PosesDir;
	PathPickerConfig.CustomFolderBlacklist = MakeShared<FBlacklistPaths>();
	PathPickerConfig.CustomFolderBlacklist.Get()->AddWhitelistItem("PoseLibrary", PosesDir);

	PathPickerConfig.bFocusSearchBoxWhenOpened = false;
	PathPickerConfig.OnPathSelected = FOnPathSelected::CreateSP(this, &SControlRigBaseListWidget::HandlePathSelected);
	PathPickerConfig.SetPathsDelegates.Add(&SetPathsDelegate);
	PathPickerConfig.bAllowContextMenu = true;

	// The root widget in this dialog.
	TSharedRef<SVerticalBox> MainVerticalBox = SNew(SVerticalBox);

	// Toolbar on Top
	MainVerticalBox->AddSlot()
		.AutoHeight()
		[
			SNew(SControlRigPoseAnimSelectionToolbar).OwningControlRigWidget(this)
		];

	// Path/Asset view
	MainVerticalBox->AddSlot()
		.HAlign(HAlign_Fill)
		.FillHeight(0.7)
		.Padding(0, 0, 0, 4)
		[
			SNew(SSplitter)

			+ SSplitter::Slot()
		.Value(0.33f)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			ContentBrowserModule.Get().CreatePathPicker(PathPickerConfig)
		]
		]

	+ SSplitter::Slot()
		.Value(0.66f)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		]
		]
		];


	//Bottom Area View Container to hold specific View (Pose/Animation).
	MainVerticalBox->AddSlot()
		.HAlign(HAlign_Fill)
		.FillHeight(0.3)
		[
			SAssignNew(ViewContainer, SBox)
			.Padding(FMargin(5.0f, 0, 0, 0))
		];

	ChildSlot
		[
			MainVerticalBox
		];

	CurrentViewType = ESelectedControlAsset::Type::None;
	CreateCurrentView(nullptr);
}

SControlRigBaseListWidget::~SControlRigBaseListWidget()
{
}

void SControlRigBaseListWidget::NotifyUser(FNotificationInfo& NotificationInfo)
{
	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

UControlRig* SControlRigBaseListWidget::GetControlRig()
{
	if (FControlRigEditMode* EditMode = GetEditMode())
	{
		return EditMode->GetControlRig(true);
	}
	return nullptr;
}
FControlRigEditMode* SControlRigBaseListWidget::GetEditMode()
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	return ControlRigEditMode;
}

FText SControlRigBaseListWidget::GetAssetNameText() const
{
	return FText::FromString(CurrentlyEnteredAssetName);
}

FText SControlRigBaseListWidget::GetPathNameText() const
{
	return FText::FromString(CurrentlySelectedPath);
}

void SControlRigBaseListWidget::SetCurrentlySelectedPath(const FString& NewPath)
{
	CurrentlySelectedPath = NewPath;
	UpdateInputValidity();
}

FString SControlRigBaseListWidget::GetCurrentlySelectedPath() const
{
	return CurrentlySelectedPath;
}
void SControlRigBaseListWidget::SetCurrentlyEnteredAssetName(const FString& NewName)
{
	CurrentlyEnteredAssetName = NewName;
	UpdateInputValidity();
}

//todo to be used with renaming when it comes in reuse. (next thing to do).
void SControlRigBaseListWidget::UpdateInputValidity()
{
	bLastInputValidityCheckSuccessful = true;

	if (CurrentlyEnteredAssetName.IsEmpty())
	{
		bLastInputValidityCheckSuccessful = false;
	}

	if (bLastInputValidityCheckSuccessful)
	{
		if (CurrentlySelectedPath.IsEmpty())
		{
			bLastInputValidityCheckSuccessful = false;
		}
	}

	/**
	const FString ObjectPath = GetObjectPathForSave();
	FText ErrorMessage;
	const bool bAllowExistingAsset = true;

	FName AssetClassName = AssetClassNames.Num() == 1 ? AssetClassNames[0] : NAME_None;
	UClass* AssetClass = AssetClassName != NAME_None ? FindObject<UClass>(ANY_PACKAGE, *AssetClassName.ToString(), true) : nullptr;

	if (!ContentBrowserUtils::IsValidObjectPathForCreate(ObjectPath, AssetClass, ErrorMessage, bAllowExistingAsset))
	{
		LastInputValidityErrorText = ErrorMessage;
		bLastInputValidityCheckSuccessful = false;
	}
	*/
}


FString SControlRigBaseListWidget::GetObjectPathForSave() const
{
	return CurrentlySelectedPath / CurrentlyEnteredAssetName + TEXT(".") + CurrentlyEnteredAssetName;
}

void SControlRigBaseListWidget::SelectThisAsset(UObject* Asset)
{
	if (Asset != nullptr)
	{
		if (UControlRigPoseAsset* PoseAsset = Cast<UControlRigPoseAsset>(Asset))
		{
			CurrentViewType = ESelectedControlAsset::Type::Pose;
		}
		else
		{
			CurrentViewType = ESelectedControlAsset::Type::None;
		}
		FString Path = FPaths::GetPath(Asset->GetOutermost()->GetPathName());
		SetCurrentlySelectedPath(Path);
		SetCurrentlyEnteredAssetName(Asset->GetName());
	}
	else
	{
		CurrentViewType = ESelectedControlAsset::Type::None;
	}
	CreateCurrentView(Asset);
}

void SControlRigBaseListWidget::OnAssetSelected(const FAssetData& AssetData)
{
	UObject* Asset = nullptr;
	if (AssetData.IsValid())
	{
		Asset = AssetData.GetAsset();
	}
	SelectThisAsset(Asset);
}

void SControlRigBaseListWidget::OnAssetsActivated(const TArray<FAssetData>& SelectedAssets, EAssetTypeActivationMethod::Type ActivationType)
{
	if (SelectedAssets.Num() == 1 && (ActivationType == EAssetTypeActivationMethod::DoubleClicked))
	{
		UObject* Asset = nullptr;

		if (SelectedAssets[0].IsValid())
		{
			Asset = SelectedAssets[0].GetAsset();
			UControlRigPoseAsset* PoseAsset = Cast<UControlRigPoseAsset>(Asset);
			if (PoseAsset)
			{
				ExecutePastePose(PoseAsset);
			}
		}
		SelectThisAsset(Asset);
	}
}
/*
void SControlRigBaseListWidget::FilterChanged()
{
	FARFilter NewFilter;
	NewFilter.PackagePaths.Add(FName(*CurrentlySelectedPath));

	const UControlRigPoseProjectSettings* PoseSettings = GetDefault<UControlRigPoseProjectSettings>();
	if (PoseSettings)
	{
		if (PoseSettings->bFilterPoses)
		{
			NewFilter.ClassNames.Add(UControlRigPoseAsset::StaticClass()->GetFName());
		}
		if (PoseSettings->bFilterAnimations)
		{
			//NewFilter.ClassNames.Add(UControlRigAnimationAsset::StaticClass()->GetFName());
		}
		if (PoseSettings->bFilterSelectionSets)
		{
			//NewFilter.ClassNames.Add(UControlRigSelectionSetAsset::StaticClass()->GetFName());
		}
	}
	SetFilterDelegate.ExecuteIfBound(NewFilter);
}
*/

void SControlRigBaseListWidget::HandlePathSelected(const FString& NewPath)
{
	SetCurrentlySelectedPath(NewPath);
	//FilterChanged();

}

void SControlRigBaseListWidget::HandleAssetViewFolderEntered(const FString& NewPath)
{
	SetCurrentlySelectedPath(NewPath);

	TArray<FString> NewPaths;
	NewPaths.Add(NewPath);
	SetPathsDelegate.Execute(NewPaths);
}


TSharedPtr<SWidget> SControlRigBaseListWidget::OnGetFolderContextMenu(const TArray<FString>& SelectedPaths, FContentBrowserMenuExtender_SelectedPaths InMenuExtender, FOnCreateNewFolder InOnCreateNewFolder)
{

	TSharedPtr<FExtender> Extender;
	if (InMenuExtender.IsBound())
	{
		Extender = InMenuExtender.Execute(SelectedPaths);
	}

	FMenuBuilder MenuBuilder(true /*bInShouldCloseWindowAfterMenuSelection*/, Commands, Extender);
	MenuBuilder.BeginSection("AssetDialogOptions", LOCTEXT("AssetDialogMenuHeading", "Options"));

	if (SelectedPaths.Num() == 1)
	{
		FUIAction Action = FUIAction(FExecuteAction::CreateRaw((this), &SControlRigBaseListWidget::ExecuteAddFolder, SelectedPaths[0]));
		const FText Label = LOCTEXT("AddFolder", "Add Folder");
		const FText ToolTipText = LOCTEXT("AddFolder Tooltip", "Add Folder to the current Selected Folder");
		MenuBuilder.AddMenuEntry(Label, ToolTipText, FSlateIcon(), Action);

		FUIAction Action2 = FUIAction(FExecuteAction::CreateRaw((this), &SControlRigBaseListWidget::ExecuteRenameFolder, SelectedPaths[0]));
		const FText Label2 = LOCTEXT("RenameFolder", "Rename Folder");
		const FText ToolTipText2 = LOCTEXT("RenameFolderTooltip", "Rename Selected Folder.");
		MenuBuilder.AddMenuEntry(Label2, ToolTipText2, FSlateIcon(), Action2);
	}
	else if (SelectedPaths.Num() > 0)
	{
		FUIAction Action = FUIAction(FExecuteAction::CreateRaw((this), &SControlRigBaseListWidget::ExecuteDeleteFolder, SelectedPaths));
		const FText Label = LOCTEXT("DeleteFolder", "Delete Folder");
		const FText ToolTipText = LOCTEXT("DeleteFolderTooltip", "Delete Selecte Folder(s), Note this will delete content.");
		MenuBuilder.AddMenuEntry(Label, ToolTipText, FSlateIcon(), Action);

	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedPtr<SWidget> SControlRigBaseListWidget::OnGetAssetContextMenu(const TArray<FAssetData>& SelectedAssets)
{
	FMenuBuilder MenuBuilder(true /*bInShouldCloseWindowAfterMenuSelection*/, Commands);
	if (SelectedAssets.Num() == 0)
	{
		return nullptr;
	}
	if (SelectedAssets.Num() > 0)
	{
		MenuBuilder.BeginSection("PoseDialogOptions", LOCTEXT("Asset", "Asset"));
		{
			{
				FUIAction Action = FUIAction(FExecuteAction::CreateRaw((this), &SControlRigBaseListWidget::ExecuteSaveAssets, SelectedAssets));
				const FText Label = LOCTEXT("SaveAssetButton", "Save Asset");
				const FText ToolTipText = LOCTEXT("SaveAssetButtonTooltip", "Save the Selected Assets.");
				MenuBuilder.AddMenuEntry(Label, ToolTipText, FSlateIcon(), Action);
			}
			{
				FUIAction Action = FUIAction(FExecuteAction::CreateRaw((this), &SControlRigBaseListWidget::ExecuteDeleteAssets, SelectedAssets));
				const FText Label = LOCTEXT("DeleteAssetButton", "Delete Asset");
				const FText ToolTipText = LOCTEXT("DeleteAssetButtonTooltip", "Delete the Selected Assets.");
				MenuBuilder.AddMenuEntry(Label, ToolTipText, FSlateIcon(), Action);
			}
		}
		MenuBuilder.EndSection();

	}
	if (SelectedAssets.Num() == 1)
	{
		UObject* SelectedAsset = SelectedAssets[0].GetAsset();
		if (SelectedAsset == nullptr)
		{
			return nullptr;
		}

		UControlRigPoseAsset* PoseAsset = Cast<UControlRigPoseAsset>(SelectedAsset);
		if (PoseAsset)
		{

			MenuBuilder.BeginSection("PoseDialogOptions", LOCTEXT("PoseDialogMenuHeading", "Paste"));
			{

				FUIAction Action = FUIAction(FExecuteAction::CreateRaw((this), &SControlRigBaseListWidget::ExecutePastePose, PoseAsset),
					FCanExecuteAction::CreateRaw(this, &SControlRigBaseListWidget::CanExecutePastePose, PoseAsset));
				const FText Label = LOCTEXT("PastePoseButton", "Paste Pose");
				const FText ToolTipText = LOCTEXT("PastePoseButtonTooltip", "Paste the Selected Pose.");
				MenuBuilder.AddMenuEntry(Label, ToolTipText, FSlateIcon(), Action);
			}

			{
				FUIAction Action = FUIAction(FExecuteAction::CreateRaw((this), &SControlRigBaseListWidget::ExecutePasteMirrorPose, PoseAsset),
					FCanExecuteAction::CreateRaw(this, &SControlRigBaseListWidget::CanExecutePasteMirrorPose, PoseAsset));
				const FText Label = LOCTEXT("PasteMirrorPoseButton", "Paste Mirror Pose");
				const FText ToolTipText = LOCTEXT("PastePoseButtonTooltip", "Paste the Mirror Pose.");
				MenuBuilder.AddMenuEntry(Label, ToolTipText, FSlateIcon(), Action);
			}

			MenuBuilder.EndSection();

			MenuBuilder.BeginSection("PoseDialogOptions", LOCTEXT("PoseDialogSelectHeading", "Selection"));
			{
				FUIAction Action = FUIAction(FExecuteAction::CreateRaw((this), &SControlRigBaseListWidget::ExecuteSelectControls, PoseAsset));
				const FText Label = LOCTEXT("SelectControls", "Select Controls");
				const FText ToolTipText = LOCTEXT("SelectControlsTooltip", "Select Controls in this Pose on Active Control Rig");
				MenuBuilder.AddMenuEntry(Label, ToolTipText, FSlateIcon(), Action);
			}
			MenuBuilder.EndSection();

		}
	}
	return MenuBuilder.MakeWidget();

}


void SControlRigBaseListWidget::BindCommands()
{
	Commands = TSharedPtr< FUICommandList >(new FUICommandList);


}
void SControlRigBaseListWidget::ExecuteRenameFolder(const FString SelectedPath)
{

}

void SControlRigBaseListWidget::ExecuteAddFolder(const FString SelectedPath)
{

}
void SControlRigBaseListWidget::ExecuteSaveAssets(const TArray<FAssetData> SelectedAssets)
{
	TArray<UPackage*> PackagesToSave;
	for (const FAssetData& AssetData : SelectedAssets)
	{
		if (AssetData.IsValid() && AssetData.GetPackage())
		{
			PackagesToSave.Add(AssetData.GetPackage());
			FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false /*bCheckDirty*/, false /*bPromptToSave*/);
		}
	}

}
void SControlRigBaseListWidget::ExecuteDeleteAssets(const TArray<FAssetData> SelectedAssets)
{
	ObjectTools::DeleteAssets(SelectedAssets);
	SelectThisAsset(nullptr);

}
void SControlRigBaseListWidget::ExecuteDeleteFolder(const TArray<FString> SelectedFolders)
{

	// Don't allow asset deletion during PIE
	if (GIsEditor)
	{
		UEditorEngine* Editor = GEditor;
		FWorldContext* PIEWorldContext = GEditor->GetPIEWorldContext();
		if (PIEWorldContext)
		{
			FNotificationInfo Notification(LOCTEXT("CannotDeleteAssetInPIE", "Assets cannot be deleted while in PIE."));
			Notification.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Notification);
			return;
		}
	}
	/*
	const TArray<FContentBrowserItem> SelectedFiles = AssetPicker->GetAssetView()->GetSelectedFileItems();

	// Batch these by their data sources
	TMap<UContentBrowserDataSource*, TArray<FContentBrowserItemData>> SourcesAndItems;
	for (const FContentBrowserItem& SelectedItem : SelectedFiles)
	{
		FContentBrowserItem::FItemDataArrayView ItemDataArray = SelectedItem.GetInternalItems();
		for (const FContentBrowserItemData& ItemData : ItemDataArray)
		{
			if (UContentBrowserDataSource* ItemDataSource = ItemData.GetOwnerDataSource())
			{
				FText DeleteErrorMsg;
				if (ItemDataSource->CanDeleteItem(ItemData, &DeleteErrorMsg))
				{
					TArray<FContentBrowserItemData>& ItemsForSource = SourcesAndItems.FindOrAdd(ItemDataSource);
					ItemsForSource.Add(ItemData);
				}
				else
				{
					AssetViewUtils::ShowErrorNotifcation(DeleteErrorMsg);
				}
			}
		}
	}

	// Execute the operation now
	for (const auto& SourceAndItemsPair : SourcesAndItems)
	{
		SourceAndItemsPair.Key->BulkDeleteItems(SourceAndItemsPair.Value);
	}
	*/
	// If we had any folders selected, ask the user whether they want to delete them 
	// as it can be slow to build the deletion dialog on an accidental click
	if (SelectedFolders.Num() > 0)
	{
		FText Prompt;
		if (SelectedFolders.Num() == 1)
		{
			//			Prompt = FText::Format(LOCTEXT("FolderDeleteConfirm_Single", "Delete folder '{0}'?"), SelectedFolders[0]);
		}
		else
		{
			//		Prompt = FText::Format(LOCTEXT("FolderDeleteConfirm_Multiple", "Delete {0} folders?"), SelectedFolders.Num());
		}

		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		/*
		// Spawn a confirmation dialog since this is potentially a highly destructive operation
		ContentBrowserModule.Get().DisplayConfirmationPopup(
			Prompt,
			LOCTEXT("FolderDeleteConfirm_Yes", "Delete"),
			LOCTEXT("FolderDeleteConfirm_No", "Cancel"),
			ToSharedRef(),
			FOnClicked::CreateSP(this, &SControlRigBaseListWidget::ExecuteDeleteFolderConfirmed)
		);
		*/
	}

}

FReply SControlRigBaseListWidget::ExecuteDeleteFolderConfirmed()
{
	/*
	TArray< FString > SelectedFolders = AssetPicker->GetAssetView()->GetSelectedFolders();

	if (SelectedFolders.Num() > 0)
	{
		ContentBrowserUtils::DeleteFolders(SelectedFolders);
	}
	else
	{
		const TArray<FString>& SelectedPaths = PathPicker->GetPaths();

		if (SelectedPaths.Num() > 0)
		{
			if (ContentBrowserUtils::DeleteFolders(SelectedPaths))
			{
				// Since the contents of the asset view have just been deleted, set the selected path to the default "/Game"
				TArray<FString> DefaultSelectedPaths;
				DefaultSelectedPaths.Add(TEXT("/Game"));
				PathPicker->GetPathView()->SetSelectedPaths(DefaultSelectedPaths);

				FSourcesData DefaultSourcesData(FName("/Game"));
				AssetPicker->GetAssetView()->SetSourcesData(DefaultSourcesData);
			}
		}
	}
	*/
	return FReply::Handled();
}



void SControlRigBaseListWidget::ExecutePastePose(UControlRigPoseAsset* PoseAsset)
{
	if (PoseAsset)
	{
		FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
		if (ControlRigEditMode && ControlRigEditMode->GetControlRig(true))
		{
			PoseAsset->PastePose(ControlRigEditMode->GetControlRig(true));
		}
	}
}
bool SControlRigBaseListWidget::CanExecutePastePose(UControlRigPoseAsset* PoseAsset) const
{
	return PoseAsset != nullptr;
}
void SControlRigBaseListWidget::ExecuteSelectControls(UControlRigPoseAsset* PoseAsset)
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode && ControlRigEditMode->GetControlRig(true))
	{
		PoseAsset->SelectControls(ControlRigEditMode->GetControlRig(true));
	}
}

void SControlRigBaseListWidget::ExecutePasteMirrorPose(UControlRigPoseAsset* PoseAsset)
{
	if (PoseAsset)
	{
		FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
		if (ControlRigEditMode && ControlRigEditMode->GetControlRig(true))
		{
			PoseAsset->PastePose(ControlRigEditMode->GetControlRig(true), false, true);
		}
	}
}

bool SControlRigBaseListWidget::CanExecutePasteMirrorPose(UControlRigPoseAsset* PoseAsset) const
{
	return PoseAsset != nullptr;
}

void SControlRigBaseListWidget::CreateCurrentView(UObject* Asset)
{
	PoseView.Reset();
	AnimationView.Reset();
	SelectionSetView.Reset();
	EmptyBox.Reset();
	TSharedRef<SWidget> NewView = SNullWidget::NullWidget;
	switch (CurrentViewType)
	{
	case ESelectedControlAsset::Type::Pose:
		PoseView = CreatePoseView(Asset);
		ViewContainer->SetContent(PoseView.ToSharedRef());
		break;
	case ESelectedControlAsset::Type::Animation:
		AnimationView = CreateAnimationView(Asset);

		ViewContainer->SetContent(AnimationView.ToSharedRef());
		break;
	case ESelectedControlAsset::Type::SelectionSet:
		SelectionSetView = CreateSelectionSetView(Asset);

		ViewContainer->SetContent(SelectionSetView.ToSharedRef());
		break;
	case ESelectedControlAsset::Type::None:
		EmptyBox = SNew(SBox);
		ViewContainer->SetContent(EmptyBox.ToSharedRef());
		break;
	}

}

TSharedRef<class SControlRigPoseView> SControlRigBaseListWidget::CreatePoseView(UObject* InObject)
{
	UControlRigPoseAsset* PoseAsset = Cast<UControlRigPoseAsset>(InObject);
	return SNew(SControlRigPoseView).
		PoseAsset(PoseAsset);
}

TSharedRef<class SControlRigPoseView> SControlRigBaseListWidget::CreateAnimationView(UObject* InObject)
{
	return SNew(SControlRigPoseView);
}

TSharedRef<class SControlRigPoseView> SControlRigBaseListWidget::CreateSelectionSetView(UObject* InObject)
{
	return SNew(SControlRigPoseView);
}

#undef LOCTEXT_NAMESPACE
