// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/DMXPixelMappingToolkit.h"

#include "DMXPixelMappingEditorModule.h"
#include "DMXPixelMapping.h"
#include "DMXPixelMappingEditorUtils.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "K2Node_PixelMappingBaseComponent.h"
#include "Views/SDMXPixelMappingPaletteView.h"
#include "Views/SDMXPixelMappingHierarchyView.h"
#include "Views/SDMXPixelMappingDesignerView.h"
#include "Views/SDMXPixelMappingPreviewView.h"
#include "Views/SDMXPixelMappingDetailsView.h"
#include "ViewModels/DMXPixelMappingPaletteViewModel.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingOutputComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"
#include "DMXPixelMappingToolbar.h"
#include "DMXPixelMappingEditorCommands.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "DMXUtils.h"

#include "Framework/Commands/GenericCommands.h"
#include "Widgets/SWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Widgets/Docking/SDockTab.h"


#define LOCTEXT_NAMESPACE "DMXPixelMappingToolkit"

const FName FDMXPixelMappingToolkit::PaletteViewTabID(TEXT("DMXPixelMappingEditor_PaletteViewTabID"));
const FName FDMXPixelMappingToolkit::HierarchyViewTabID(TEXT("DMXPixelMappingEditor_HierarchyViewTabID"));

const FName FDMXPixelMappingToolkit::DesignerViewTabID(TEXT("DMXPixelMappingEditor_DesignerViewTabID"));
const FName FDMXPixelMappingToolkit::PreviewViewTabID(TEXT("DMXPixelMappingEditor_PreviewViewTabID"));

const FName FDMXPixelMappingToolkit::DetailsViewTabID(TEXT("DMXPixelMappingEditor_DetailsViewTabID"));

const uint8 FDMXPixelMappingToolkit::RequestStopSendingMaxTicks = 5;

FDMXPixelMappingToolkit::FDMXPixelMappingToolkit()
	: DMXPixelMapping(nullptr)
	, bIsPlayingDMX(false)
	, bTogglePlayDMXAll(true)
	, bRequestStopSendingDMX(false)
{}

FDMXPixelMappingToolkit::~FDMXPixelMappingToolkit()
{
	if (DMXPixelMapping != nullptr)
	{
		DMXPixelMapping->OnEditorRebuildChildrenComponentsDelegate.Unbind();
	}

	if (Toolbar.IsValid())
	{
		Toolbar.Reset();
	}
}

void FDMXPixelMappingToolkit::InitPixelMappingEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UDMXPixelMapping* InDMXPixelMapping)
{
	check(InDMXPixelMapping);
	InDMXPixelMapping->DestroyInvalidComponents();

	DMXPixelMapping = InDMXPixelMapping;

	InitializeInternal(Mode, InitToolkitHost, FGuid::NewGuid());
}

void FDMXPixelMappingToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_TextureEditor", "DMX Pixel Mapping Editor"));
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(PaletteViewTabID, FOnSpawnTab::CreateSP(this, &FDMXPixelMappingToolkit::SpawnTab_PaletteView))
		.SetDisplayName(LOCTEXT("Tab_PaletteView", "Palette"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(HierarchyViewTabID, FOnSpawnTab::CreateSP(this, &FDMXPixelMappingToolkit::SpawnTab_HierarchyView))
		.SetDisplayName(LOCTEXT("Tab_HierarchyView", "Hierarchy"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(DesignerViewTabID, FOnSpawnTab::CreateSP(this, &FDMXPixelMappingToolkit::SpawnTab_DesignerView))
		.SetDisplayName(LOCTEXT("Tab_DesignerView", "Designer"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(PreviewViewTabID, FOnSpawnTab::CreateSP(this, &FDMXPixelMappingToolkit::SpawnTab_PreviewView))
		.SetDisplayName(LOCTEXT("Tab_PreviewView", "Preview"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(DetailsViewTabID, FOnSpawnTab::CreateSP(this, &FDMXPixelMappingToolkit::SpawnTab_DetailsView))
		.SetDisplayName(LOCTEXT("Tab_DetailsView", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

void FDMXPixelMappingToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(PaletteViewTabID);
	InTabManager->UnregisterTabSpawner(HierarchyViewTabID);
	InTabManager->UnregisterTabSpawner(DesignerViewTabID);
	InTabManager->UnregisterTabSpawner(PreviewViewTabID);
	InTabManager->UnregisterTabSpawner(DetailsViewTabID);
}

FText FDMXPixelMappingToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "DMX Pixel Mapping");
}

FName FDMXPixelMappingToolkit::GetToolkitFName() const
{
	return FName("DMX Pixel Mapping");
}

FString FDMXPixelMappingToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "DMX Pixel Mapping ").ToString();
}

void FDMXPixelMappingToolkit::Tick(float DeltaTime)
{
	// render selected component
	if (!bIsPlayingDMX)
	{
		for (FDMXPixelMappingComponentReference& SelectedComponentRef : SelectedComponents)
		{
			if (SelectedComponentRef.Component.IsValid())
			{
				UDMXPixelMappingBaseComponent* SelectedComponent = SelectedComponentRef.Component.Get();
				if (UDMXPixelMappingRendererComponent* RendererComponent = SelectedComponent->GetFirstParentByClass<UDMXPixelMappingRendererComponent>(SelectedComponent))
				{
					RendererComponent->RendererInputTexture();
				}

				SelectedComponent->Render();

				if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(SelectedComponent))
				{
					OutputComponent->RenderEditorPreviewTexture();
				}
			}
		}
	}

	if (bIsPlayingDMX)
	{
		if (bTogglePlayDMXAll) // Send to all
		{
			if (DMXPixelMapping != nullptr &&
				DMXPixelMapping->RootComponent != nullptr)
			{
				DMXPixelMapping->RootComponent->RenderAndSendDMX();

				for (FDMXPixelMappingComponentReference& SelectedComponentRef : SelectedComponents)
				{
					if (SelectedComponentRef.Component.IsValid())
					{
						UDMXPixelMappingBaseComponent* SelectedComponent = SelectedComponentRef.Component.Get();
						if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(SelectedComponent))
						{
							OutputComponent->RenderEditorPreviewTexture();
						}
					}
				}
			}
		}
		else // Send to selected component
		{
			for (FDMXPixelMappingComponentReference& SelectedComponentRef : SelectedComponents)
			{
				if (SelectedComponentRef.Component.IsValid())
				{
					UDMXPixelMappingBaseComponent* SelectedComponent = SelectedComponentRef.Component.Get();
					if (UDMXPixelMappingRendererComponent* RendererComponent = SelectedComponent->GetFirstParentByClass<UDMXPixelMappingRendererComponent>(SelectedComponent))
					{
						RendererComponent->RendererInputTexture();
					}

					SelectedComponent->RenderAndSendDMX();

					if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(SelectedComponent))
					{
						OutputComponent->RenderEditorPreviewTexture();
					}
				}
			}
		}
	}
	else
	{
		if (bRequestStopSendingDMX)
		{
			if (RequestStopSendingTicks < RequestStopSendingMaxTicks)
			{
				if (bTogglePlayDMXAll) // Send to all
				{
					if (DMXPixelMapping != nullptr &&
						DMXPixelMapping->RootComponent != nullptr
						)
					{
						DMXPixelMapping->RootComponent->ResetDMX();
					}
				}
				else // Send to selected component
				{
					for (FDMXPixelMappingComponentReference& SelectedComponentRef : SelectedComponents)
					{
						if (SelectedComponentRef.Component.IsValid())
						{
							UDMXPixelMappingBaseComponent* SelectedComponent = SelectedComponentRef.Component.Get();
							SelectedComponent->ResetDMX();
						}
					}
				}

				RequestStopSendingTicks++;
			}
			else
			{
				RequestStopSendingTicks = 0;
				bRequestStopSendingDMX = false;
			}
		}
	}
}

TStatId FDMXPixelMappingToolkit::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FDMXPixelMappingToolkit, STATGROUP_Tickables);
}

FDMXPixelMappingComponentReference FDMXPixelMappingToolkit::GetReferenceFromComponent(UDMXPixelMappingBaseComponent* InComponent)
{
	return FDMXPixelMappingComponentReference(SharedThis(this), InComponent);
}

void FDMXPixelMappingToolkit::SetActiveRenderComponent(UDMXPixelMappingRendererComponent* InComponent)
{
	ActiveRendererComponent = InComponent;
}

void FDMXPixelMappingToolkit::HandleAddComponent(bool bIsSuccess)
{
	OnComponenetAddedOrDeletedDelegate.Broadcast(bIsSuccess);
}

void FDMXPixelMappingToolkit::SelectComponents(const TSet<FDMXPixelMappingComponentReference>& InSelectedComponents)
{
	SelectedComponents.Empty();

	SetActiveRenderComponent(nullptr);
	ActiveOutputComponents.Empty();

	SelectedComponents.Append(InSelectedComponents);

	// toggle highlight selection
	DMXPixelMapping->ForEachComponentOfClass<UDMXPixelMappingOutputComponent>([this](UDMXPixelMappingOutputComponent* InComponent)
	{
		InComponent->ToggleHighlightSelection(true);
	});

	for (const FDMXPixelMappingComponentReference& ComponentReference : SelectedComponents)
	{
		if (UDMXPixelMappingRendererComponent* RendererComponent = Cast<UDMXPixelMappingRendererComponent>(ComponentReference.GetComponent()))
		{
			SetActiveRenderComponent(RendererComponent);
		}
		else
		{
			if (UDMXPixelMappingRendererComponent* RendererComponentParent = ComponentReference.GetComponent()->GetFirstParentByClass<UDMXPixelMappingRendererComponent>(ComponentReference.GetComponent()))
			{
				SetActiveRenderComponent(RendererComponentParent);
			}
		}

		if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(ComponentReference.GetComponent()))
		{
			ActiveOutputComponents.Add(OutputComponent);

		}
	}

	if (SelectedComponents.Num() > 0)
	{
		// Force update design view
		DesignerView->UpdateOutput(true);
	}

	for (TWeakObjectPtr<UDMXPixelMappingOutputComponent> ComponentToHighlight : ActiveOutputComponents)
	{
		// highlight active component
		ComponentToHighlight->ToggleHighlightSelection(true);
	}

	OnSelectedComponenetChangedDelegate.Broadcast();
}

void FDMXPixelMappingToolkit::AddRenderer()
{
	if (DMXPixelMapping != nullptr)
	{
		{
			// Just use root component for now
			UDMXPixelMappingRendererComponent* RendererComponent = FDMXPixelMappingEditorUtils::AddRenderer(DMXPixelMapping);
			SetActiveRenderComponent(RendererComponent);
		}
	}

	BroadcastPostChange(DMXPixelMapping);

	HandleAddComponent(true);
}

void FDMXPixelMappingToolkit::ClearRenderers()
{
	if (DMXPixelMapping != nullptr)
	{
		if (DMXPixelMapping->RootComponent != nullptr)
		{
			DMXPixelMapping->RootComponent->ClearChildren();
		}
	}

	SetActiveRenderComponent(nullptr);
	ActiveOutputComponents.Empty();

	HandleAddComponent(true);
}

void FDMXPixelMappingToolkit::PlayDMX()
{
	bIsPlayingDMX = true;
}

void FDMXPixelMappingToolkit::StopPlayingDMX()
{
	bIsPlayingDMX = false;

	RequestStopSendingTicks = 0;
	bRequestStopSendingDMX = true;
}

void FDMXPixelMappingToolkit::ExecutebTogglePlayDMXAll()
{
	bTogglePlayDMXAll ^= 1;
}

void FDMXPixelMappingToolkit::OnSaveThumbnailImage()
{
	if (GetActiveOutputComponents().Num() > 0)
	{
		if (UDMXPixelMappingOutputComponent* OutputComponent = GetActiveOutputComponents()[0])
		{
			DMXPixelMapping->ThumbnailImage = OutputComponent->GetOutputTexture();
		}
	}
}

void FDMXPixelMappingToolkit::BroadcastPostChange(UDMXPixelMapping* InDMXPixelMapping)
{
	if (InDMXPixelMapping != nullptr)
	{
		for (TObjectIterator<UK2Node_PixelMappingBaseComponent> It(RF_Transient | RF_ClassDefaultObject, /** bIncludeDerivedClasses */ true, /** InternalExcludeFlags */ EInternalObjectFlags::PendingKill); It; ++It)
		{
			It->OnPixelMappingChanged(InDMXPixelMapping);
		}
	}
}

void FDMXPixelMappingToolkit::DeleteMatrixPixels(UDMXPixelMappingMatrixComponent* InMatrixComponent)
{
	if (InMatrixComponent != nullptr)
	{
		TSet<FDMXPixelMappingComponentReference> ComponentReference;
		InMatrixComponent->ForEachComponentOfClass<UDMXPixelMappingMatrixCellComponent>([this, &ComponentReference](UDMXPixelMappingMatrixCellComponent* InComponent)
		{
			ComponentReference.Add(GetReferenceFromComponent(InComponent));
		}, false);

		FDMXPixelMappingEditorUtils::DeleteComponents(SharedThis(this), GetDMXPixelMapping(), ComponentReference);
		GetOnComponenetAddedOrDeletedDelegate().Broadcast(true);

		BroadcastPostChange(GetDMXPixelMapping());
	}
}

void FDMXPixelMappingToolkit::CreateMatrixPixels(UDMXPixelMappingMatrixComponent* InMatrixComponent)
{
	bool bAtLeastOnePixelAdded = false;

	if (InMatrixComponent != nullptr)
	{
		UDMXLibrary* DMXLibrary = InMatrixComponent->FixturePatchMatrixRef.DMXLibrary;
		UDMXEntityFixturePatch* FixturePatch = InMatrixComponent->FixturePatchMatrixRef.GetFixturePatch();

		if (DMXLibrary != nullptr && FixturePatch != nullptr)
		{
			if (UDMXEntityFixtureType* ParentFixtureType = FixturePatch->ParentFixtureTypeTemplate)
			{
				if(FixturePatch->CanReadActiveMode())
				{
					int32 ActiveMode = FixturePatch->ActiveMode;

					const FDMXFixtureMode& FixtureMode = ParentFixtureType->Modes[ActiveMode];
					const FDMXFixtureMatrix& FixtureMatrixConfig = FixtureMode.FixtureMatrixConfig;

					// If there are any pixel functions
					int32 NumChannels = FixtureMatrixConfig.XCells * FixtureMatrixConfig.YCells;
					if (NumChannels > 0 && ParentFixtureType->bFixtureMatrixEnabled)
					{
						InMatrixComponent->SetNumCells(FIntPoint(FixtureMatrixConfig.XCells, FixtureMatrixConfig.YCells));

						TArray<int32> AllChannels;
						int32 MaxChannels = NumChannels + 1;
						for (int32 CellID = 1; CellID < MaxChannels; CellID++)
						{
							AllChannels.Add(CellID);
						}

						TArray<int32> OrderedChannels;
						FDMXUtils::PixelMappingDistributionSort(FixtureMode.FixtureMatrixConfig.PixelMappingDistribution, FixtureMatrixConfig.XCells, FixtureMatrixConfig.YCells, AllChannels, OrderedChannels);
						TArray<UDMXPixelMappingMatrixCellComponent*> Components;
						check(AllChannels.Num() == OrderedChannels.Num());
						int32 XYIndex = 0;
						for (int32 IndexX = 0; IndexX < FixtureMatrixConfig.XCells; IndexX++)
						{
							for (int32 IndexY = 0; IndexY < FixtureMatrixConfig.YCells; IndexY++)
							{
								// Create or delete all matrix pixels
								TSharedPtr<FDMXPixelMappingComponentTemplate> ComponentTemplate = MakeShared<FDMXPixelMappingComponentTemplate>(UDMXPixelMappingMatrixCellComponent::StaticClass());
								UDMXPixelMappingMatrixCellComponent* Component = Cast<UDMXPixelMappingMatrixCellComponent>(ComponentTemplate->Create(DMXPixelMapping->GetRootComponent()));
								const FName UniqueName = MakeUniqueObjectName(Component->GetOuter(), Component->GetClass(), FName(FixturePatch->GetDisplayName()));
								const FString NewNameStr = UniqueName.ToString();
								Component->Rename(*NewNameStr);
								Components.Add(Component);
								Component->CellID = OrderedChannels[XYIndex];
								InMatrixComponent->SetChildSizeAndPosition(Component, FIntPoint(IndexX, IndexY));
								XYIndex++;
								bAtLeastOnePixelAdded = true;
							}
						}

						// Adds matrix child in right order
						for (int32 CellID = 0; CellID < OrderedChannels.Num(); CellID++)
						{
							int32 ComponentIndex = OrderedChannels.IndexOfByKey<int32>(CellID + 1);
							InMatrixComponent->AddChild(Components[ComponentIndex]);
							Components[ComponentIndex]->PostParentAssigned();
						}

						const bool bForceUpdate = true;
						DesignerView->UpdateOutput(bForceUpdate);

						// Set distribution
						InMatrixComponent->Distribution = FixtureMode.FixtureMatrixConfig.PixelMappingDistribution;
					}
				}
				else
				{
					InMatrixComponent->SetNumCells(FIntPoint(0, 0));

					const bool bForceUpdate = true;
					DesignerView->UpdateOutput(bForceUpdate);
				}
			}
		}
	}

	if (bAtLeastOnePixelAdded)
	{
		GetOnComponenetAddedOrDeletedDelegate().Broadcast(true);
	}
}

void FDMXPixelMappingToolkit::OnDMXPixelMappingDeleteChildrenComponents(UDMXPixelMappingBaseComponent* InParentComponent)
{
	if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(InParentComponent))
	{
		DeleteMatrixPixels(MatrixComponent);
		CreateMatrixPixels(MatrixComponent);
	}
}

void FDMXPixelMappingToolkit::InitializeInternal(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, const FGuid& MessageLogGuid)
{
	// Make sure we loaded all UObjects
	DMXPixelMapping->CreateOrLoadObjects();

	// Create delegates
	DMXPixelMapping->OnEditorRebuildChildrenComponentsDelegate.BindRaw(this, &FDMXPixelMappingToolkit::OnDMXPixelMappingDeleteChildrenComponents);

	// Create commands
	DesignerCommandList = MakeShareable(new FUICommandList);
	DesignerCommandList->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &FDMXPixelMappingToolkit::DeleteSelectedComponents_Internal),
		FCanExecuteAction::CreateSP(this, &FDMXPixelMappingToolkit::CanDeleteSelectedComponents_Internal)
	);

	CreateInternalViewModels();
	CreateInternalViews();

	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_PixelMapping_Layout_v5")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
				->SetHideTabWell(true)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.25f)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(PaletteViewTabID, ETabState::OpenedTab)
						->SetSizeCoefficient(0.5f)
					)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(HierarchyViewTabID, ETabState::OpenedTab)
						->SetSizeCoefficient(0.5f)
					)
				)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.5f)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(DesignerViewTabID, ETabState::OpenedTab)
						->SetSizeCoefficient(0.6f)
					)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(PreviewViewTabID, ETabState::OpenedTab)
						->SetSizeCoefficient(0.4f)
					)
				)
				->Split
				(
					FTabManager::NewStack()
					->AddTab(DetailsViewTabID, ETabState::OpenedTab)
					->SetSizeCoefficient(0.25f)
				)
			)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, FDMXPixelMappingEditorModule::DMXPixelMappingEditorAppIdentifier,
		StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, DMXPixelMapping);

	SetupCommands();
	ExtendToolbar();
	RegenerateMenusAndToolbars();
}

TSharedRef<SDockTab> FDMXPixelMappingToolkit::SpawnTab_PaletteView(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PaletteViewTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("PaletteViewTabID", "Palette"))
		[
			PaletteView.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FDMXPixelMappingToolkit::SpawnTab_HierarchyView(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == HierarchyViewTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("HierarchyViewTabID", "Hierarchy"))
		[
			HierarchyView.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FDMXPixelMappingToolkit::SpawnTab_DesignerView(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == DesignerViewTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("DesignerViewTabID", "Designer"))
		[
			DesignerView.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FDMXPixelMappingToolkit::SpawnTab_PreviewView(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PreviewViewTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("PreviewViewTabID", "Preview"))
		[
			PreviewView.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FDMXPixelMappingToolkit::SpawnTab_DetailsView(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == DetailsViewTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("DetailsViewTabID", "Details"))
		[
			DetailsView.ToSharedRef()
		];

	return SpawnedTab;
}

void FDMXPixelMappingToolkit::CreateInternalViewModels()
{
	TSharedPtr<FDMXPixelMappingToolkit> ThisPtr(SharedThis(this));

	PaletteViewModel = MakeShared<FDMXPixelMappingPaletteViewModel>(ThisPtr);
}

void FDMXPixelMappingToolkit::CreateInternalViews()
{
	CreateOrGetView_PaletteView();
	CreateOrGetView_HierarchyView();
	CreateOrGetView_DesignerView();
	CreateOrGetView_PreviewView();
	CreateOrGetView_DetailsView();
}

void FDMXPixelMappingToolkit::DeleteSelectedComponents(const TSet<FDMXPixelMappingComponentReference>& InComponents)
{
	// Select parent as selected.
	TSet<FDMXPixelMappingComponentReference> ParentComponentReferences;

	for (const FDMXPixelMappingComponentReference& ComponentReference : InComponents)
	{
		if (UDMXPixelMappingRendererComponent* RendererComponent = Cast<UDMXPixelMappingRendererComponent>(ComponentReference.GetComponent()))
		{
			SetActiveRenderComponent(nullptr);
		}

		if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(ComponentReference.GetComponent()))
		{
			ActiveOutputComponents.Remove(OutputComponent);
		}


		if (UDMXPixelMappingBaseComponent* ParentComponent = ComponentReference.GetComponent()->Parent)
		{
			ParentComponentReferences.Add(GetReferenceFromComponent(ParentComponent));
		}
	}

	FDMXPixelMappingEditorUtils::DeleteComponents(SharedThis(this), GetDMXPixelMapping(), InComponents);
	OnComponenetAddedOrDeletedDelegate.Broadcast(true);

	BroadcastPostChange(GetDMXPixelMapping());

	// Should be called at the end of the function after Broadcast the changes
	SelectComponents(ParentComponentReferences);
}

bool FDMXPixelMappingToolkit::CanDeleteSelectedComponents(const TSet<FDMXPixelMappingComponentReference>& InComponents)
{
	return InComponents.Num() > 0;
}

void FDMXPixelMappingToolkit::DeleteSelectedComponents_Internal()
{
	DeleteSelectedComponents(GetSelectedComponents());
}

bool FDMXPixelMappingToolkit::CanDeleteSelectedComponents_Internal()
{
	return CanDeleteSelectedComponents(GetSelectedComponents());
}

void FDMXPixelMappingToolkit::OnComponentRenamed(UDMXPixelMappingBaseComponent* InComponent)
{
	BroadcastPostChange(GetDMXPixelMapping());
}

TSharedRef<SWidget> FDMXPixelMappingToolkit::CreateOrGetView_PaletteView()
{
	if (!PaletteView.IsValid())
	{
		PaletteView = SNew(SDMXPixelMappingPaletteView, SharedThis(this));
	}

	return PaletteView.ToSharedRef();
}

TSharedRef<SWidget> FDMXPixelMappingToolkit::CreateOrGetView_HierarchyView()
{
	if (!HierarchyView.IsValid())
	{
		HierarchyView = SNew(SDMXPixelMappingHierarchyView, SharedThis(this));
	}

	return HierarchyView.ToSharedRef();
}

TSharedRef<SWidget> FDMXPixelMappingToolkit::CreateOrGetView_DesignerView()
{
	if (!DesignerView.IsValid())
	{
		DesignerView = SNew(SDMXPixelMappingDesignerView, SharedThis(this));
	}

	return DesignerView.ToSharedRef();
}

TSharedRef<SWidget> FDMXPixelMappingToolkit::CreateOrGetView_PreviewView()
{
	if (!PreviewView.IsValid())
	{
		PreviewView = SNew(SDMXPixelMappingPreviewView, SharedThis(this));
	}

	return PreviewView.ToSharedRef();
}

TSharedRef<SWidget> FDMXPixelMappingToolkit::CreateOrGetView_DetailsView()
{
	if (!DetailsView.IsValid())
	{
		DetailsView = SNew(SDMXPixelMappingDetailsView, SharedThis(this));
	}

	return DetailsView.ToSharedRef();
}

void FDMXPixelMappingToolkit::SetupCommands()
{
	GetToolkitCommands()->MapAction(
		FDMXPixelMappingEditorCommands::Get().AddMapping,
		FExecuteAction::CreateRaw(this, &FDMXPixelMappingToolkit::AddRenderer));

	GetToolkitCommands()->MapAction(
		FDMXPixelMappingEditorCommands::Get().ClearMappings,
		FExecuteAction::CreateRaw(this, &FDMXPixelMappingToolkit::ClearRenderers));

	GetToolkitCommands()->MapAction(
		FDMXPixelMappingEditorCommands::Get().SaveThumbnailImage,
		FExecuteAction::CreateSP(this, &FDMXPixelMappingToolkit::OnSaveThumbnailImage));

	GetToolkitCommands()->MapAction(
		FDMXPixelMappingEditorCommands::Get().PlayDMX,
		FExecuteAction::CreateRaw(this, &FDMXPixelMappingToolkit::PlayDMX),
		FCanExecuteAction::CreateLambda([this] { return !bIsPlayingDMX; }),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateLambda([this] { return !bIsPlayingDMX; }));

	GetToolkitCommands()->MapAction(
		FDMXPixelMappingEditorCommands::Get().PlayDMX,
		FExecuteAction::CreateRaw(this, &FDMXPixelMappingToolkit::PlayDMX),
		FCanExecuteAction::CreateLambda([this] { return !bIsPlayingDMX; }),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateLambda([this] { return !bIsPlayingDMX; }));

	GetToolkitCommands()->MapAction(
		FDMXPixelMappingEditorCommands::Get().StopPlayingDMX,
		FExecuteAction::CreateRaw(this, &FDMXPixelMappingToolkit::StopPlayingDMX),
		FCanExecuteAction::CreateLambda([this] { return bIsPlayingDMX; }),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateLambda([this] { return bIsPlayingDMX; }));

	GetToolkitCommands()->MapAction(
		FDMXPixelMappingEditorCommands::Get().bTogglePlayDMXAll,
		FExecuteAction::CreateRaw(this, &FDMXPixelMappingToolkit::ExecutebTogglePlayDMXAll),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this]() -> bool { return bTogglePlayDMXAll; }));
}

void FDMXPixelMappingToolkit::ExtendToolbar()
{
	FDMXPixelMappingEditorModule& DMXPixelMappingEditorModule = FModuleManager::LoadModuleChecked<FDMXPixelMappingEditorModule>("DMXPixelMappingEditor");
	Toolbar = MakeShared<FDMXPixelMappingToolbar>(SharedThis(this));

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	Toolbar->BuildToolbar(ToolbarExtender);
	AddToolbarExtender(ToolbarExtender);

	// Let other part of the plugin extend DMX Pixel Maping Editor toolbar
	AddMenuExtender(DMXPixelMappingEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
	AddToolbarExtender(DMXPixelMappingEditorModule.GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

#undef LOCTEXT_NAMESPACE
