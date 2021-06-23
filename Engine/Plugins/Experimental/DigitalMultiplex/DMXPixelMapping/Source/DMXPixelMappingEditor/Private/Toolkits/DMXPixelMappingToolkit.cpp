// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/DMXPixelMappingToolkit.h"

#include "DMXEditorUtils.h"
#include "DMXPixelMapping.h"
#include "DMXPixelMappingEditorCommands.h"
#include "DMXPixelMappingEditorModule.h"
#include "DMXPixelMappingEditorUtils.h"
#include "DMXPixelMappingToolbar.h"
#include "K2Node_PixelMappingBaseComponent.h"
#include "DMXPixelMappingComponentWidget.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingOutputComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "ViewModels/DMXPixelMappingPaletteViewModel.h"
#include "Views/SDMXPixelMappingPaletteView.h"
#include "Views/SDMXPixelMappingHierarchyView.h"
#include "Views/SDMXPixelMappingDesignerView.h"
#include "Views/SDMXPixelMappingPreviewView.h"
#include "Views/SDMXPixelMappingDetailsView.h"

#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "DMXUtils.h"
#include "DMXPixelMappingComponentWidget.h"

#include "ScopedTransaction.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Commands/GenericCommands.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Widgets/SWidget.h"
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
	UDMXPixelMappingBaseComponent::GetOnComponentAdded().RemoveAll(this);
	UDMXPixelMappingBaseComponent::GetOnComponentRemoved().RemoveAll(this);

	Toolbar.Reset();
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
	if (!ensure(DMXPixelMapping))
	{
		return;
	}

	UDMXPixelMappingRootComponent* RootComponent =  DMXPixelMapping->RootComponent;
	if (!ensure(RootComponent))
	{
		return;
	}
	
	// render selected component
	if (!bIsPlayingDMX)
	{
		for (const FDMXPixelMappingComponentReference& SelectedComponentRef : SelectedComponents)
		{
			UDMXPixelMappingBaseComponent* SelectedComponent = SelectedComponentRef.Component.Get();
			if (IsValid(SelectedComponent))
			{
				// User select root component
				if (Cast<UDMXPixelMappingRootComponent>(SelectedComponent))
				{
					break;
				}

				// Try to get renderer component from selected component
				UDMXPixelMappingRendererComponent* RendererComponent = SelectedComponent->GetRendererComponent();
				if (!ensureMsgf(RendererComponent, TEXT("Component %s resides in pixelmapping but has no valid renderer."), *SelectedComponent->GetUserFriendlyName()))
				{
					break;
				}
				
				// Render
				RendererComponent->Render();

				// Render preview
				RendererComponent->RenderEditorPreviewTexture();

				// Render only once for all selected components
				break;
			}
		}
	}

	if (bIsPlayingDMX)
	{
		if (bTogglePlayDMXAll) // Send to all
		{
			// Render all components
			RootComponent->RenderAndSendDMX();

			for (FDMXPixelMappingComponentReference& SelectedComponentRef : SelectedComponents)
			{
				if (UDMXPixelMappingBaseComponent* SelectedComponent = SelectedComponentRef.Component.Get())
				{
					// Try to get renderer component from selected component
					UDMXPixelMappingRendererComponent* RendererComponent = SelectedComponent->GetRendererComponent();
					if (RendererComponent)
					{
						RendererComponent->RenderEditorPreviewTexture();
					}

					// Render only once for all selected components
					break;
				}
			}
		}
		else // Send to selected component
		{
			bool bRenderedOnce = false;

			for (FDMXPixelMappingComponentReference& SelectedComponentRef : SelectedComponents)
			{
				if (UDMXPixelMappingBaseComponent* SelectedComponent = SelectedComponentRef.Component.Get())
				{
					if (!bRenderedOnce)
					{
						// Try to get renderer component from selected component
						UDMXPixelMappingRendererComponent* RendererComponent = SelectedComponent->GetRendererComponent();
						if (RendererComponent)
						{
							RendererComponent->Render();

							RendererComponent->RenderEditorPreviewTexture();
						}

						bRenderedOnce = true;
					}

					SelectedComponent->SendDMX();
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
					RootComponent->ResetDMX();
				}
				else // Send to selected component
				{
					for (FDMXPixelMappingComponentReference& SelectedComponentRef : SelectedComponents)
					{
						if (UDMXPixelMappingBaseComponent* SelectedComponent = SelectedComponentRef.Component.Get())
						{
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

	// Update component colors
	UpdateComponentWidgetColors();
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

void FDMXPixelMappingToolkit::HandleAddComponents()
{
	// DEPRECATED 4.27
	UpdateBlueprintNodes(DMXPixelMapping);
	OnComponentsAddedOrDeletedDelegate_DEPRECATED.Broadcast();
}

void FDMXPixelMappingToolkit::HandleRemoveComponents()
{
	// DEPRECATED 4.27
	FDMXEditorUtils::ClearAllDMXPortBuffers();
	FDMXEditorUtils::ClearFixturePatchCachedData();

	DesignerView->UpdateOutput(true);

	UpdateBlueprintNodes(DMXPixelMapping);
	OnComponentsAddedOrDeletedDelegate_DEPRECATED.Broadcast();

	// Remove removed components from selection
	TSet<FDMXPixelMappingComponentReference> NewSelection;
	for (const FDMXPixelMappingComponentReference& ComponentReference : SelectedComponents)
	{
		
		if (UDMXPixelMappingBaseComponent* Component = ComponentReference.GetComponent())
		{
			if (IsValid(Component->GetParent()) || Component->GetClass() == UDMXPixelMappingRootComponent::StaticClass())
			{
				NewSelection.Add(ComponentReference);
			}
		}
	}
	if (NewSelection.Num() != SelectedComponents.Num())
	{
		SelectedComponents = NewSelection;

		OnSelectedComponentsChangedDelegate.Broadcast();
	}
}

template <typename ComponentType>
TArray<ComponentType> FDMXPixelMappingToolkit::MakeComponentArray(const TSet<FDMXPixelMappingComponentReference>& Components) const
{
	TArray<ComponentType> Result;
	for (const FDMXPixelMappingComponentReference& Component : Components)
	{
		if (ComponentType* CastedComponent = Cast<ComponentType>(Component))
		{
			Result.Add(CastedComponent);
		}
	}
}

void FDMXPixelMappingToolkit::SelectComponents(const TSet<FDMXPixelMappingComponentReference>& InSelectedComponents)
{
	SelectedComponents.Empty();

	SetActiveRenderComponent(nullptr);
	ActiveOutputComponents.Empty();

	SelectedComponents.Append(InSelectedComponents);

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

	OnSelectedComponentsChangedDelegate.Broadcast();
}

void FDMXPixelMappingToolkit::UpdateComponentWidgetColors()
{
	if (DMXPixelMapping)
	{
		// Reset all to default color
		DMXPixelMapping->ForEachComponentOfClass<UDMXPixelMappingOutputComponent>([this](UDMXPixelMappingOutputComponent* OutputComponent)
			{
				if (TSharedPtr<FDMXPixelMappingComponentWidget> ComponentWidget = OutputComponent->GetComponentWidget())
				{
					ComponentWidget->SetColor(OutputComponent->GetEditorColor());
				}
			});

		TArray<UDMXPixelMappingBaseComponent*> SelectedComponentsArray;
		SelectedComponentsArray.Reserve(GetSelectedComponents().Num());
		for (const FDMXPixelMappingComponentReference& ComponentReference : GetSelectedComponents())
		{
			if (ComponentReference.GetComponent())
			{
				SelectedComponentsArray.Add(ComponentReference.GetComponent());
			}
		}

		UDMXPixelMappingBaseComponent** SelectedFixtureGroupPtr = SelectedComponentsArray.FindByPredicate([&SelectedComponentsArray](UDMXPixelMappingBaseComponent* Component)
			{
				if (Component->GetClass() == UDMXPixelMappingFixtureGroupComponent::StaticClass())
				{
					return true;
				}
				return false;
			});

		if (SelectedFixtureGroupPtr)
		{
			if (UDMXPixelMappingOutputComponent* FixtureGroupComponent = Cast<UDMXPixelMappingOutputComponent>(*SelectedFixtureGroupPtr))
			{
				if (TSharedPtr<FDMXPixelMappingComponentWidget> ComponentWidget = FixtureGroupComponent->GetComponentWidget())
				{
					ComponentWidget->SetColor(FLinearColor::Green);
				}

				constexpr bool bApplyColorChildsRecursive = true;
				FixtureGroupComponent->ForEachChildOfClass<UDMXPixelMappingOutputComponent>([FixtureGroupComponent](UDMXPixelMappingOutputComponent* HighlightedComponent)
					{	
						const bool bIsOverParent = HighlightedComponent->IsOverParent();
						if (TSharedPtr<FDMXPixelMappingComponentWidget> ComponentWidget = HighlightedComponent->GetComponentWidget())
						{
							if (bIsOverParent)
							{							
								// Give childs of the group some greenish color when they're over their parent
								ComponentWidget->SetColor(FLinearColor(0.05f, 0.5f, 0.05f));
							}
							else
							{
								ComponentWidget->SetColor(FLinearColor::Red);
							}
						}
					}, bApplyColorChildsRecursive);
			}
		}
		else
		{
			// Set a selected color to selected components and their childs
			for (UDMXPixelMappingBaseComponent* Component : SelectedComponentsArray)
			{
				if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(Component))
				{							
					// Pick a color depending on where the widget resides and what is selected
					FLinearColor Color = [OutputComponent]()
					{
						const bool bIsOverParent = OutputComponent->IsOverParent();
						if (bIsOverParent)
						{
							return FLinearColor::Green;
						}
						else
						{
							return FLinearColor::Red;
						}
					}();

					if (TSharedPtr<FDMXPixelMappingComponentWidget> ComponentWidget = OutputComponent->GetComponentWidget())
					{
						ComponentWidget->SetColor(Color);
					}

					constexpr bool bApplyColorChildsRecursive = true;
					OutputComponent->ForEachChildOfClass<UDMXPixelMappingOutputComponent>([&Color](UDMXPixelMappingOutputComponent* HighlightedComponent)
						{

							if (TSharedPtr<FDMXPixelMappingComponentWidget> ComponentWidget = HighlightedComponent->GetComponentWidget())
							{
								ComponentWidget->SetColor(Color);
							}
						}, bApplyColorChildsRecursive);
				}
			}
		}
	}
}

bool FDMXPixelMappingToolkit::IsComponentSelected(UDMXPixelMappingBaseComponent* Component) const
{
	for (const FDMXPixelMappingComponentReference& ComponentReference : SelectedComponents)
	{
		if (Component && Component == ComponentReference.GetComponent())
		{
			return true;
		}
	}
	return false;
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

	FDMXEditorUtils::ClearFixturePatchCachedData();
	FDMXEditorUtils::ClearAllDMXPortBuffers();
}

void FDMXPixelMappingToolkit::ExecutebTogglePlayDMXAll()
{
	bTogglePlayDMXAll ^= 1;
}

void FDMXPixelMappingToolkit::UpdateBlueprintNodes(UDMXPixelMapping* InDMXPixelMapping)
{
	if (InDMXPixelMapping != nullptr)
	{
		for (TObjectIterator<UK2Node_PixelMappingBaseComponent> It(RF_Transient | RF_ClassDefaultObject, /** bIncludeDerivedClasses */ true, /** InternalExcludeFlags */ EInternalObjectFlags::PendingKill); It; ++It)
		{
			It->OnPixelMappingChanged(InDMXPixelMapping);
		}
	}
}

void FDMXPixelMappingToolkit::OnSaveThumbnailImage()
{
	if (ActiveRendererComponent.IsValid())
	{
		DMXPixelMapping->ThumbnailImage = ActiveRendererComponent->GetPreviewRenderTarget();
	}
}

TArray<UDMXPixelMappingBaseComponent*> FDMXPixelMappingToolkit::CreateComponentsFromTemplates(UDMXPixelMappingRootComponent* RootComponent, UDMXPixelMappingBaseComponent* Target, const TArray<TSharedPtr<FDMXPixelMappingComponentTemplate>>& Templates)
{
	TArray<UDMXPixelMappingBaseComponent*> NewComponents;
	if (ensureMsgf(RootComponent && Target, TEXT("Tried to create components from template but RootComponent or Target were invalid.")))
	{
		for (const TSharedPtr<FDMXPixelMappingComponentTemplate>& Template : Templates)
		{
			if (UDMXPixelMappingBaseComponent* NewComponent = Template->CreateComponent<UDMXPixelMappingBaseComponent>(RootComponent))
			{
				NewComponents.Add(NewComponent);

				Target->Modify();
				NewComponent->Modify();

				Target->AddChild(NewComponent);				
			}
		}
	}

	return NewComponents;
}

void FDMXPixelMappingToolkit::OnComponentAdded(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* InComponent)
{
	UpdateBlueprintNodes(DMXPixelMapping);
}

void FDMXPixelMappingToolkit::OnComponentRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* InComponent)
{
	UpdateBlueprintNodes(DMXPixelMapping);
}

void FDMXPixelMappingToolkit::InitializeInternal(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, const FGuid& MessageLogGuid)
{
	// Make sure we loaded all UObjects
	DMXPixelMapping->CreateOrLoadObjects();

	// Bind to component changes
	UDMXPixelMappingBaseComponent::GetOnComponentAdded().AddRaw(this, &FDMXPixelMappingToolkit::OnComponentAdded);
	UDMXPixelMappingBaseComponent::GetOnComponentRemoved().AddRaw(this, &FDMXPixelMappingToolkit::OnComponentRemoved);

	// Create commands
	DesignerCommandList = MakeShareable(new FUICommandList);
	DesignerCommandList->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &FDMXPixelMappingToolkit::DeleteSelectedComponents)
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

void FDMXPixelMappingToolkit::DeleteSelectedComponents()
{
	const FScopedTransaction DeleteSelectedComponentsTransaction =
		FScopedTransaction(FText::Format(LOCTEXT("DeleteSelectedComponents", "PixelMapping: Delete {0}|plural(one=Component, other=Components)"), SelectedComponents.Num()));

	// Select parent as selected.
	TSet<FDMXPixelMappingComponentReference> ParentComponentReferences;

	for (const FDMXPixelMappingComponentReference& ComponentReference : SelectedComponents)
	{
		if (UDMXPixelMappingBaseComponent* Component = ComponentReference.GetComponent())
		{
			if (UDMXPixelMappingRendererComponent* RendererComponent = Cast<UDMXPixelMappingRendererComponent>(Component))
			{
				SetActiveRenderComponent(nullptr);
			}

			if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(Component))
			{
				ActiveOutputComponents.Remove(OutputComponent);
			}

			if (UDMXPixelMappingBaseComponent* ParentComponent = Component->GetParent())
			{
				ParentComponentReferences.Add(GetReferenceFromComponent(ParentComponent));

				Component->SetFlags(RF_Transactional);
				Component->Modify();
				ParentComponent->Modify();

				ParentComponent->RemoveChild(ComponentReference.GetComponent());
			}
		}
	}

	// Should be called at the end of the function after Broadcast the changes
	SelectComponents(ParentComponentReferences);
}

void FDMXPixelMappingToolkit::OnComponentRenamed(UDMXPixelMappingBaseComponent* InComponent)
{
	UpdateBlueprintNodes(GetDMXPixelMapping());
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
		FDMXPixelMappingEditorCommands::Get().SaveThumbnailImage,
		FExecuteAction::CreateSP(this, &FDMXPixelMappingToolkit::OnSaveThumbnailImage));

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
