// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudEditor.h"
#include "LidarPointCloudEditorViewport.h"
#include "LidarPointCloud.h"
#include "LidarPointCloudShared.h"
#include "ILidarPointCloudEditorModule.h"

#include "LidarPointCloudEditorCommands.h"

#include "Misc/ScopedSlowTask.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SSingleObjectDetailsPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Docking/SDockTab.h"
#include "UObject/Package.h"
#include "Framework/Commands/UICommandList.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "AssetRegistryModule.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "LidarPointCloudEditor"

const FName PointCloudEditorAppName = FName(TEXT("LidarPointCloudEditorApp"));

const FName FLidarPointCloudEditor::DetailsTabId(TEXT("Details"));
const FName FLidarPointCloudEditor::ViewportTabId(TEXT("Viewport"));

/////////////////////////////////////////////////////
// SPointCloudPropertiesTabBody

class SPointCloudPropertiesTabBody : public SSingleObjectDetailsPanel
{
public:
	SLATE_BEGIN_ARGS(SPointCloudPropertiesTabBody) {}
	SLATE_END_ARGS()

private:
	// Pointer back to owning sprite editor instance (the keeper of state)
	TWeakPtr<class FLidarPointCloudEditor> PointCloudEditorPtr;
public:
	void Construct(const FArguments& InArgs, TSharedPtr<FLidarPointCloudEditor> InPointCloudEditor)
	{
		PointCloudEditorPtr = InPointCloudEditor;

		SSingleObjectDetailsPanel::Construct(SSingleObjectDetailsPanel::FArguments().HostCommandList(InPointCloudEditor->GetToolkitCommands()).HostTabManager(InPointCloudEditor->GetTabManager()), /*bAutomaticallyObserveViaGetObjectToObserve=*/ true, /*bAllowSearch=*/ true);
	}

	// SSingleObjectDetailsPanel interface
	virtual UObject* GetObjectToObserve() const override
	{
		return PointCloudEditorPtr.Pin()->GetPointCloudBeingEdited();
	}

	virtual TSharedRef<SWidget> PopulateSlot(TSharedRef<SWidget> PropertyEditorWidget) override
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1)
			[
				PropertyEditorWidget
			];
	}
	// End of SSingleObjectDetailsPanel interface
};

//////////////////////////////////////////////////////////////////////////
// FLidarPointCloudEditor

FLidarPointCloudEditor::FLidarPointCloudEditor()
	: PointCloudBeingEdited(nullptr)
	, bEditMode(false)
{
}

FLidarPointCloudEditor::~FLidarPointCloudEditor()
{
	// Unregister from the cloud before closing
	if (PointCloudBeingEdited)
	{
		PointCloudBeingEdited->OnPointCloudRebuilt().RemoveAll(this);
		PointCloudBeingEdited->OnPreSaveCleanup().RemoveAll(this);
	}

	DeselectPoints();
}

void FLidarPointCloudEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_LidarPointCloudEditor", "LiDAR Point Cloud Editor"));
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(FLidarPointCloudEditor::DetailsTabId, FOnSpawnTab::CreateSP(this, &FLidarPointCloudEditor::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTabLabel", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(FLidarPointCloudEditor::ViewportTabId, FOnSpawnTab::CreateSP(this, &FLidarPointCloudEditor::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTabLabel", "Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

void FLidarPointCloudEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(FLidarPointCloudEditor::DetailsTabId);
	InTabManager->UnregisterTabSpawner(FLidarPointCloudEditor::ViewportTabId);
}

FName FLidarPointCloudEditor::GetToolkitFName() const { return FName("LidarPointCloudEditor"); }
FText FLidarPointCloudEditor::GetBaseToolkitName() const { return LOCTEXT("LidarPointCloudEditorAppLabel", "LiDAR Point Cloud Editor"); }
FText FLidarPointCloudEditor::GetToolkitToolTipText() const { return FAssetEditorToolkit::GetToolTipTextForObject(PointCloudBeingEdited); }
FLinearColor FLidarPointCloudEditor::GetWorldCentricTabColorScale() const { return FLinearColor::White; }
FString FLidarPointCloudEditor::GetWorldCentricTabPrefix() const { return TEXT("LidarPointCloudEditor"); }

FText FLidarPointCloudEditor::GetToolkitName() const
{
	const bool bDirtyState = PointCloudBeingEdited->GetOutermost()->IsDirty();

	FFormatNamedArguments Args;
	Args.Add(TEXT("PointCloudName"), FText::FromString(PointCloudBeingEdited->GetName()));
	Args.Add(TEXT("DirtyState"), bDirtyState ? FText::FromString(TEXT("*")) : FText::GetEmpty());
	return FText::Format(LOCTEXT("LidarPointCloudEditorToolkitName", "{PointCloudName}{DirtyState}"), Args);
}

void FLidarPointCloudEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (IsValid(PointCloudBeingEdited))
	{
		Collector.AddReferencedObject(PointCloudBeingEdited);
	}
}

void FLidarPointCloudEditor::InitPointCloudEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, class ULidarPointCloud* InitPointCloud)
{
	FLidarPointCloudEditorCommands::Register();

	PointCloudBeingEdited = InitPointCloud;

	// Register for rebuilding events
	PointCloudBeingEdited->OnPointCloudRebuilt().AddSP(this, &FLidarPointCloudEditor::OnPointCloudRebuilt);
	PointCloudBeingEdited->OnPreSaveCleanup().AddSP(this, &FLidarPointCloudEditor::OnPreSaveCleanup);

	TSharedPtr<FLidarPointCloudEditor> PointCloudEditor = SharedThis(this);

	Viewport = SNew(SLidarPointCloudEditorViewport).PointCloudEditor(SharedThis(this)).ObjectToEdit(PointCloudBeingEdited);

	// Default layout
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_LidarPointCloudEditor_Layout_v2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->SetSizeCoefficient(0.9f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.6f)
					->AddTab(FLidarPointCloudEditor::ViewportTabId, ETabState::OpenedTab)
					->SetHideTabWell(true)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.4f)
					->AddTab(FLidarPointCloudEditor::DetailsTabId, ETabState::OpenedTab)
					->SetHideTabWell(true)
				)
			)
		);

	// Initialize the asset editor
	InitAssetEditor(Mode, InitToolkitHost, PointCloudEditorAppName, StandaloneDefaultLayout, /*bCreateDefaultStandaloneMenu=*/ true, /*bCreateDefaultToolbar=*/ true, InitPointCloud);

	ExtendToolBar();

	BindEditorCommands();

	RegenerateMenusAndToolbars();
}

void FLidarPointCloudEditor::SelectPointsByFrustum(const FConvexVolume& SelectionFrustum, bool bAdditive)
{
	if (bAdditive && SelectedPoints.Num() > 0)
	{
		TArray<FLidarPointCloudPoint*> NewSelectedPoints;
		PointCloudBeingEdited->GetPointsInFrustum(NewSelectedPoints, SelectionFrustum, true);

		// Merge selections and set selection flag for new selection of points
		for (FLidarPointCloudPoint** Data = NewSelectedPoints.GetData(), **DataEnd = Data + NewSelectedPoints.Num(); Data != DataEnd; ++Data)
		{
			if (!(*Data)->bSelected)
			{
				(*Data)->bSelected = true;
				SelectedPoints.Add(*Data);
			}
		}
	}
	else
	{
		// Clear any existing selection
		DeselectPoints();

		PointCloudBeingEdited->GetPointsInFrustum(SelectedPoints, SelectionFrustum, true);

		// Set selection flag for new selection of points
		for (FLidarPointCloudPoint** Data = SelectedPoints.GetData(), **DataEnd = Data + SelectedPoints.Num(); Data != DataEnd; ++Data)
		{
			(*Data)->bSelected = true;
		}
	}	
}

void FLidarPointCloudEditor::DeselectPointsByFrustum(const FConvexVolume& SelectionFrustum)
{
	TArray<FLidarPointCloudPoint*> NewSelectedPoints;
	PointCloudBeingEdited->GetPointsInFrustum(NewSelectedPoints, SelectionFrustum, true);

	// Unset selection flag for selection of points
	for (FLidarPointCloudPoint** Data = NewSelectedPoints.GetData(), **DataEnd = Data + NewSelectedPoints.Num(); Data != DataEnd; ++Data)
	{
		(*Data)->bSelected = false;
	}

	// Remove deselected points from the selection list
	FLidarPointCloudPoint** Data = SelectedPoints.GetData();
	for (int32 i = 0; i < SelectedPoints.Num(); ++Data, ++i)
	{
		if (!(*Data)->bSelected)
		{
			SelectedPoints.RemoveAtSwap(i--, 1, false);
			--Data;
		}
	}

	SelectedPoints.Shrink();
}

void FLidarPointCloudEditor::DeselectPoints()
{
	for (FLidarPointCloudPoint** Data = SelectedPoints.GetData(), **DataEnd = Data + SelectedPoints.Num(); Data != DataEnd; ++Data)
	{
		(*Data)->bSelected = false;
	}

	SelectedPoints.Empty();
}

void FLidarPointCloudEditor::DeletePoints()
{
	if (!ConfirmCollisionChange())
	{
		return;
	}

	// Permanently remove points
	if (FMessageDialog::Open(EAppMsgType::Type::YesNo, FText::FromString("Warning: This operation cannot be reversed!\nAre you sure you want to continue?")) != EAppReturnType::Yes)
	{
		return;
	}

	PointCloudBeingEdited->RemovePoints(SelectedPoints);
	PointCloudBeingEdited->RefreshRendering();
	PointCloudBeingEdited->MarkPackageDirty();
	SelectedPoints.Empty();

	Viewport->RefreshViewport();
}

void FLidarPointCloudEditor::DeleteHiddenPoints()
{
	// Permanently remove points
	if (FMessageDialog::Open(EAppMsgType::Type::YesNo, FText::FromString("Warning: This operation cannot be reversed!\nAre you sure you want to continue?")) != EAppReturnType::Yes)
	{
		return;
	}

	PointCloudBeingEdited->RemoveHiddenPoints();
	DeselectPoints();
	PointCloudBeingEdited->RefreshRendering();
	PointCloudBeingEdited->MarkPackageDirty();

	Viewport->RefreshViewport();
}

void FLidarPointCloudEditor::HidePoints()
{
	if (!ConfirmCollisionChange())
	{
		return;
	}

	// Hide points
	for (FLidarPointCloudPoint** Data = SelectedPoints.GetData(), **DataEnd = Data + SelectedPoints.Num(); Data != DataEnd; ++Data)
	{
		(*Data)->bVisible = false;
		(*Data)->bSelected = false;
	}

	PointCloudBeingEdited->MarkPointVisibilityDirty();

	SelectedPoints.Empty();
}

void FLidarPointCloudEditor::UnhideAll()
{
	if (PointCloudBeingEdited)
	{
		if (!ConfirmCollisionChange())
		{
			return;
		}

		PointCloudBeingEdited->UnhideAll();
	}
}

bool FLidarPointCloudEditor::ConfirmCollisionChange()
{
	if (PointCloudBeingEdited->HasCollisionData())
	{
		if (FMessageDialog::Open(EAppMsgType::Type::YesNo, FText::FromString("Performing this action will invalidate the collision data.\nAre you sure you want to continue?")) != EAppReturnType::Yes)
		{
			return false;
		}

		PointCloudBeingEdited->RemoveCollision();
	}

	return true;
}

TSharedRef<SWidget> FLidarPointCloudEditor::BuildPointCloudStatistics()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(0.5f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding(4.0f)
			[
				SNew(STextBlock).Text_Lambda([this] { return FText::Format(LOCTEXT("PointCount", "Point Count: {0}"), PointCloudBeingEdited ? PointCloudBeingEdited->GetNumPoints() : 0); })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding(4.0f)
			[
				SNew(STextBlock).Text_Lambda([this] { return FText::Format(LOCTEXT("NodeCount", "Node Count: {0}"), PointCloudBeingEdited ? PointCloudBeingEdited->GetNumNodes() : 0); })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding(4.0f)
			[
				SNew(STextBlock).Text_Lambda([this] { return FText::Format(LOCTEXT("Collider", "Collider: {0} poly"), PointCloudBeingEdited && PointCloudBeingEdited->HasCollisionData() ? PointCloudBeingEdited->Octree.GetCollisionData()->Indices.Num() : 0); })
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(0.5f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding(4.0f)
			[
				SNew(STextBlock).Text_Lambda([this]
				{
					FVector BoundingSize = PointCloudBeingEdited ? PointCloudBeingEdited->GetBounds().GetSize() : FVector::ZeroVector;
					return FText::Format(LOCTEXT("PCBounds", "Bounds: {0} x {1} x {2}"), FMath::CeilToInt(BoundingSize.X), FMath::CeilToInt(BoundingSize.Y), FMath::CeilToInt(BoundingSize.Z));
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding(4.0f)
			[
				SNew(STextBlock).Text_Lambda([this] { return FText::Format(LOCTEXT("PCSize", "Data Size: {0} MB"), PointCloudBeingEdited ? PointCloudBeingEdited->GetDataSize() : 0); })
			]
		];
}

TSharedRef<SWidget> FLidarPointCloudEditor::GenerateExtractionMenuContent()
{
	FMenuBuilder MenuBuilder(true, Viewport->GetCommandList());

	MenuBuilder.BeginSection("ExtractSelection", LOCTEXT("ExtractSelection", "Extract Selection"));
	{
		MenuBuilder.AddMenuEntry(FLidarPointCloudEditorCommands::Get().Extract);
		MenuBuilder.AddMenuEntry(FLidarPointCloudEditorCommands::Get().ExtractCopy);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FLidarPointCloudEditor::GenerateCollisionMenuContent()
{
	FMenuBuilder MenuBuilder(true, Viewport->GetCommandList());

	MenuBuilder.BeginSection("Collision", LOCTEXT("Collision", "Collision"));
	{
		MenuBuilder.AddMenuEntry(FLidarPointCloudEditorCommands::Get().BuildCollision);
		MenuBuilder.AddMenuEntry(FLidarPointCloudEditorCommands::Get().RemoveCollision);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FLidarPointCloudEditor::GenerateDeleteMenuContent()
{
	FMenuBuilder MenuBuilder(true, Viewport->GetCommandList());

	MenuBuilder.BeginSection("Delete", LOCTEXT("Delete", "Delete"));
	{
		MenuBuilder.AddMenuEntry(FLidarPointCloudEditorCommands::Get().DeleteSelected);
		MenuBuilder.AddMenuEntry(FLidarPointCloudEditorCommands::Get().DeleteHidden, NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FLidarPointCloudStyle::GetStyleSetName(), "LidarPointCloudEditor.DeleteSelected"));
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FLidarPointCloudEditor::ExtendToolBar()
{
	struct Local
	{
		static void FillToolbar(FToolBarBuilder& ToolbarBuilder, FLidarPointCloudEditor* ThisEditor)
		{
			const FLidarPointCloudEditorCommands* Commands = &FLidarPointCloudEditorCommands::Get();

			ToolbarBuilder.BeginSection("Camera");
			{
				ToolbarBuilder.AddToolBarButton(Commands->ResetCamera, NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "StaticMeshEditor.ResetCamera"));
			}
			ToolbarBuilder.EndSection();

			ToolbarBuilder.BeginSection("LidarPointCloud");
			{
				ToolbarBuilder.AddToolBarButton(Commands->Center, NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "AnimViewportMenu.TranslateMode"));
				ToolbarBuilder.AddToolBarButton(Commands->Merge);
				ToolbarBuilder.AddToolBarButton(Commands->Align);
				ToolbarBuilder.AddComboButton(
					FUIAction(FExecuteAction(), FCanExecuteAction()),
					FOnGetContent::CreateSP(ThisEditor, &FLidarPointCloudEditor::GenerateCollisionMenuContent),
					LOCTEXT("Collision_Label", "Collision"),
					LOCTEXT("Collision_Tooltip", "Collision options"),
					FSlateIcon(FLidarPointCloudStyle::GetStyleSetName(), "LidarPointCloudEditor.BuildCollision")
				);
			}
			ToolbarBuilder.EndSection();

			ToolbarBuilder.BeginSection("LidarPointCloudEdit");
			{
				ToolbarBuilder.AddToolBarButton(Commands->EditMode);
				ToolbarBuilder.AddToolBarButton(Commands->HideSelected);
				ToolbarBuilder.AddToolBarButton(Commands->UnhideAll);
				ToolbarBuilder.AddComboButton(
					FUIAction(FExecuteAction(), FCanExecuteAction()),
					FOnGetContent::CreateSP(ThisEditor, &FLidarPointCloudEditor::GenerateDeleteMenuContent),
					LOCTEXT("Delete_Label", "Delete"),
					LOCTEXT("Delete_Tooltip", "Point deletion options"),
					FSlateIcon(FLidarPointCloudStyle::GetStyleSetName(), "LidarPointCloudEditor.DeleteSelected")
				);
				ToolbarBuilder.AddComboButton(
					FUIAction(FExecuteAction(), FCanExecuteAction::CreateSP(ThisEditor, &FLidarPointCloudEditor::HasSelectedPoints)),
					FOnGetContent::CreateSP(ThisEditor, &FLidarPointCloudEditor::GenerateExtractionMenuContent),
					LOCTEXT("Extract_Label", "Extract"),
					LOCTEXT("Extract_Tooltip", "Selection extraction options"),
					FSlateIcon(FLidarPointCloudStyle::GetStyleSetName(), "LidarPointCloudEditor.Extract")
				);
			}
			ToolbarBuilder.EndSection();
		}
	};

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension("Asset", EExtensionHook::After, Viewport->GetCommandList(), FToolBarExtensionDelegate::CreateStatic(&Local::FillToolbar, this));
	AddToolbarExtender(ToolbarExtender);

	AddToolbarExtender(ILidarPointCloudEditorModule::Get().GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

void FLidarPointCloudEditor::BindEditorCommands()
{
	TSharedPtr<FUICommandList> CommandList = Viewport->GetCommandList();
	
	const FLidarPointCloudEditorCommands& Commands = FLidarPointCloudEditorCommands::Get();

	CommandList->MapAction(Commands.Center, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::ToggleCenter), FCanExecuteAction(), FIsActionChecked::CreateSP(this, &FLidarPointCloudEditor::IsCentered));
	CommandList->MapAction(Commands.BuildCollision, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::BuildCollision));
	CommandList->MapAction(Commands.RemoveCollision, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::RemoveCollision), FCanExecuteAction::CreateSP(this, &FLidarPointCloudEditor::HasCollisionData));
	CommandList->MapAction(Commands.EditMode, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::ToggleEditMode), FCanExecuteAction(), FIsActionChecked::CreateSP(this, &FLidarPointCloudEditor::IsEditMode));
	CommandList->MapAction(Commands.UnhideAll, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::UnhideAll));
	CommandList->MapAction(Commands.HideSelected, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::HidePoints), FCanExecuteAction::CreateSP(this, &FLidarPointCloudEditor::HasSelectedPoints));
	CommandList->MapAction(Commands.DeleteSelected, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::DeletePoints), FCanExecuteAction::CreateSP(this, &FLidarPointCloudEditor::HasSelectedPoints));
	CommandList->MapAction(Commands.DeleteHidden, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::DeleteHiddenPoints));
	CommandList->MapAction(Commands.Extract, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::Extract), FCanExecuteAction::CreateSP(this, &FLidarPointCloudEditor::HasSelectedPoints));
	CommandList->MapAction(Commands.ExtractCopy, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::ExtractCopy), FCanExecuteAction::CreateSP(this, &FLidarPointCloudEditor::HasSelectedPoints));
	CommandList->MapAction(Commands.Merge, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::Merge));
	CommandList->MapAction(Commands.Align, FExecuteAction::CreateSP(this, &FLidarPointCloudEditor::Align));
}

TSharedRef<SDockTab> FLidarPointCloudEditor::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	TSharedPtr<FLidarPointCloudEditor> PointCloudEditorPtr = SharedThis(this);

	// Spawn the tab
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTab_Title", "Details"))
		[
			SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(SBorder)
					[
						BuildPointCloudStatistics()
					]
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(2.0f)
				[
					SNew(SBorder)
					.Padding(4.0f)
					[
						SNew(SPointCloudPropertiesTabBody, PointCloudEditorPtr)
					]
				]
		];
}

TSharedRef<SDockTab> FLidarPointCloudEditor::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == ViewportTabId);

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		.Label(LOCTEXT("ViewportTab_Title", "Viewport"))
		[
			Viewport.ToSharedRef()
		];

	Viewport->SetParentTab(SpawnedTab);

	return SpawnedTab;
}

void FLidarPointCloudEditor::OnPointCloudRebuilt()
{
	// If the cloud asset has been rebuilt, invalidate the selection array
	SelectedPoints.Empty();
}

void FLidarPointCloudEditor::OnPreSaveCleanup()
{
	// If the cloud asset is being saved, deselect all points
	DeselectPoints();
}

void FLidarPointCloudEditor::Extract()
{
	// Skip, if no points are selected
	if (SelectedPoints.Num() == 0)
	{
		return;
	}

	if (!ConfirmCollisionChange())
	{
		return;
	}

	ULidarPointCloud* NewPointCloud = CreateNewAsset();
	if (NewPointCloud)
	{
		NewPointCloud->SetData(SelectedPoints);
		NewPointCloud->OriginalCoordinates = PointCloudBeingEdited->OriginalCoordinates;
		DeletePoints();
	}
}

void FLidarPointCloudEditor::ExtractCopy()
{
	// Skip, if no points are selected
	if (SelectedPoints.Num() == 0)
	{
		return;
	}

	ULidarPointCloud* NewPointCloud = CreateNewAsset();
	if (NewPointCloud)
	{
		NewPointCloud->SetData(SelectedPoints);
		NewPointCloud->OriginalCoordinates = PointCloudBeingEdited->OriginalCoordinates;
		DeselectPoints();
	}
}

void FLidarPointCloudEditor::ToggleCenter()
{
	if (!ConfirmCollisionChange())
	{
		return;
	}

	DeselectPoints();

	if (IsCentered())
	{
		PointCloudBeingEdited->RestoreOriginalCoordinates();
	}
	else
	{
		PointCloudBeingEdited->CenterPoints();
	}

	if (Viewport.IsValid())
	{
		Viewport->ResetCamera();
	}
}

void FLidarPointCloudEditor::ToggleEditMode()
{
	if (!bEditMode)
	{
		// Warn about loading the whole cloud
		if (!PointCloudBeingEdited->IsFullyLoaded())
		{
			if (FMessageDialog::Open(EAppMsgType::Type::YesNo, FText::FromString("The asset needs to be fully loaded into memory to enable editing.\nThis may take a while, depending on the size of the asset.\nAre you sure you want to continue?")) != EAppReturnType::Yes)
			{
				return;
			}
		}

		PointCloudBeingEdited->LoadAllNodes();
	}

	bEditMode = !bEditMode;

	if (!bEditMode)
	{
		DeselectPoints();
		Viewport->RefreshViewport();
	}
}

bool FLidarPointCloudEditor::IsCentered() const
{
	return PointCloudBeingEdited && PointCloudBeingEdited->IsCentered();
}

void FLidarPointCloudEditor::Merge()
{
	TArray<FAssetData> SelectedAssets = SelectAssets(LOCTEXT("SelectMergeSource", "Select Merge Source"));
	if (SelectedAssets.Num())
	{
		TArray<ULidarPointCloud*> Assets;
		TArray<FString> Names({"Initializing", "Self"});

		for (int32 i = 0; i < SelectedAssets.Num(); ++i)
		{
			ULidarPointCloud* Asset = Cast<ULidarPointCloud>(SelectedAssets[i].GetAsset());

			// Skip invalid and self
			if (!Asset || Asset == PointCloudBeingEdited)
			{
				continue;
			}

			Assets.Add(Asset);
			Names.Add(SelectedAssets[i].AssetName.ToString());
		}

		FScopedSlowTask ProgressDialog(SelectedAssets.Num() + 2, LOCTEXT("Merge", "Merging Point Clouds..."));
		ProgressDialog.MakeDialog();
		int32 i = 0;

		PointCloudBeingEdited->Merge(Assets, [&ProgressDialog, &i, &Names](float Progress){
			ProgressDialog.EnterProgressFrame(1.f, FText::FromString(Names[i++]));
		});
	}
}

void FLidarPointCloudEditor::BuildCollision()
{
	if (PointCloudBeingEdited)
	{
		PointCloudBeingEdited->BuildCollision();
	}
}

void FLidarPointCloudEditor::RemoveCollision()
{
	if (PointCloudBeingEdited)
	{
		PointCloudBeingEdited->RemoveCollision();
	}
}

void FLidarPointCloudEditor::Align()
{
	if (!ConfirmCollisionChange())
	{
		return;
	}

	TArray<FAssetData> SelectedAssets = SelectAssets(LOCTEXT("SelectAlignmentSources", "Select Alignment Sources"));
	if (SelectedAssets.Num())
	{
		TArray<ULidarPointCloud*> Assets;
		Assets.Add(PointCloudBeingEdited);

		for (int32 i = 0; i < SelectedAssets.Num(); ++i)
		{
			ULidarPointCloud* Asset = Cast<ULidarPointCloud>(SelectedAssets[i].GetAsset());

			// Skip invalid and self
			if (!Asset || Asset == PointCloudBeingEdited)
			{
				continue;
			}

			Assets.Add(Asset);
		}

		FScopedSlowTask ProgressDialog(1, LOCTEXT("Align", "Aligning Point Clouds..."));
		ProgressDialog.MakeDialog();
		ProgressDialog.EnterProgressFrame(1.f);
		ULidarPointCloud::AlignClouds(Assets);
	}
}

bool FLidarPointCloudEditor::HasCollisionData() const
{
	return PointCloudBeingEdited && PointCloudBeingEdited->HasCollisionData();
}

TArray<FAssetData> FLidarPointCloudEditor::SelectAssets(const FText& Title)
{
	// Initialize SaveAssetDialog config
	FOpenAssetDialogConfig OpenAssetDialogConfig;
	OpenAssetDialogConfig.DialogTitleOverride = Title;
	OpenAssetDialogConfig.DefaultPath = "/Game";
	OpenAssetDialogConfig.bAllowMultipleSelection = true;
	OpenAssetDialogConfig.AssetClassNames.Emplace(*ULidarPointCloud::StaticClass()->GetName());

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	return ContentBrowserModule.Get().CreateModalOpenAssetDialog(OpenAssetDialogConfig);
}

FString FLidarPointCloudEditor::GetSaveAsLocation()
{
	// Initialize SaveAssetDialog config
	FSaveAssetDialogConfig SaveAssetDialogConfig;
	SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SelectExtractDestination", "Select Extract Destination");
	SaveAssetDialogConfig.DefaultPath = "/Game";
	SaveAssetDialogConfig.AssetClassNames.Emplace(*ULidarPointCloud::StaticClass()->GetName());
	SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	return ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
}

ULidarPointCloud* FLidarPointCloudEditor::CreateNewAsset()
{
	ULidarPointCloud* NewPointCloud = nullptr;

	FString SaveObjectPath = GetSaveAsLocation();
	if (!SaveObjectPath.IsEmpty())
	{
		// Attempt to load existing asset first
		NewPointCloud = FindObject<ULidarPointCloud>(nullptr, *SaveObjectPath);

		// Proceed to creating a new asset, if needed
		if (!NewPointCloud)
		{
			const FString PackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
			const FString ObjectName = FPackageName::ObjectPathToObjectName(SaveObjectPath);

			NewPointCloud = NewObject<ULidarPointCloud>(CreatePackage(nullptr, *PackageName), ULidarPointCloud::StaticClass(), FName(*ObjectName), EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);

			FAssetRegistryModule::AssetCreated(NewPointCloud);
			NewPointCloud->MarkPackageDirty();
		}		
	}

	return NewPointCloud;
}

#undef LOCTEXT_NAMESPACE