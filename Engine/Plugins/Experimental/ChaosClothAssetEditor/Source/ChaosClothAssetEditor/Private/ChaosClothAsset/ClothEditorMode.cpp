// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditorMode.h"
#include "ChaosClothAsset/ClothEditorCommands.h"
#include "ChaosClothAsset/ClothEditorModeToolkit.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ClothEditorRestSpaceViewportClient.h"
#include "ChaosClothAsset/ClothPatternToDynamicMesh.h"
#include "ChaosClothAsset/ClothEditorPreviewScene.h"
#include "ChaosClothAsset/ClothEditorContextObject.h"
#include "ChaosClothAsset/AddWeightMapNode.h"
#include "ChaosClothAsset/TransferSkinWeightsNode.h"
#include "AssetEditorModeManager.h"
#include "Drawing/MeshElementsVisualizer.h"
#include "EditorViewportClient.h"
#include "EdModeInteractiveToolsContext.h"
#include "Framework/Commands/UICommandList.h"
#include "InteractiveTool.h"
#include "ModelingToolTargetUtil.h"
#include "PreviewScene.h"
#include "TargetInterfaces/AssetBackedTarget.h"
#include "TargetInterfaces/DynamicMeshCommitter.h"
#include "TargetInterfaces/DynamicMeshProvider.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "ToolTargetManager.h"
#include "Toolkits/BaseToolkit.h"
#include "ToolTargets/ToolTarget.h"
#include "ToolTargets/DynamicMeshComponentToolTarget.h"
#include "ChaosClothAsset/ClothComponentToolTarget.h"
#include "Engine/Selection.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "RemeshMeshTool.h"
#include "AttributeEditorTool.h"
#include "MeshAttributePaintTool.h"
#include "ChaosClothAsset/ClothWeightMapPaintTool.h"
#include "ChaosClothAsset/ClothTrainingTool.h"
#include "ChaosClothAsset/ClothTransferSkinWeightsTool.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "DynamicMeshEditor.h"
#include "Settings/EditorStyleSettings.h"
#include "ClothingSystemEditorInterfaceModule.h"
#include "ClothingAssetFactoryInterface.h"
#include "ClothingAssetFactory.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Utils/ClothingMeshUtils.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "BoneWeights.h"
#include "ToolSetupUtil.h"
#include "Dataflow/DataflowComponent.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "ToolTargets/SkeletalMeshComponentToolTarget.h"
#include "Dataflow/DataflowGraphEditor.h"
#include "ContextObjectStore.h"
#include "BaseGizmos/TransformGizmoUtil.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Dataflow/DataflowSNode.h"
#include "Components/SkeletalMeshComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothEditorMode)

#define LOCTEXT_NAMESPACE "UChaosClothAssetEditorMode"

const FEditorModeID UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId = TEXT("EM_ChaosClothAssetEditorMode");


namespace ChaosClothAssetEditorModeHelpers
{
	void RemoveClothWeightMaps(UE::Chaos::ClothAsset::FCollectionClothFacade& ClothFacade, const TArray<FName>& WeightMapNames)
	{
		for (const FName& WeightMapName : WeightMapNames)
		{
			if (ClothFacade.HasWeightMap(WeightMapName))
			{
				ClothFacade.RemoveWeightMap(WeightMapName);
			}
		}
	}

	TArray<FName> GetDynamicMeshWeightMapNames(const UE::Geometry::FDynamicMesh3& DynamicMesh)
	{
		TArray<FName> OutWeightMapNames;

		for (int32 DynamicMeshWeightMapIndex = 0; DynamicMeshWeightMapIndex < DynamicMesh.Attributes()->NumWeightLayers(); ++DynamicMeshWeightMapIndex)
		{
			const UE::Geometry::FDynamicMeshWeightAttribute* const WeightMapAttribute = DynamicMesh.Attributes()->GetWeightLayer(DynamicMeshWeightMapIndex);
			const FName WeightMapName = WeightMapAttribute->GetName();
			OutWeightMapNames.Add(WeightMapName);
		}

		return OutWeightMapNames;
	}
}


UChaosClothAssetEditorMode::UChaosClothAssetEditorMode()
{
	Info = FEditorModeInfo(
		EM_ChaosClothAssetEditorModeId,
		LOCTEXT("ChaosClothAssetEditorModeName", "Cloth"),
		FSlateIcon(),
		false);
}

const FToolTargetTypeRequirements& UChaosClothAssetEditorMode::GetToolTargetRequirements()
{
	static const FToolTargetTypeRequirements ToolTargetRequirements =
		FToolTargetTypeRequirements({
			UMaterialProvider::StaticClass(),
			UDynamicMeshCommitter::StaticClass(),
			UDynamicMeshProvider::StaticClass()
			});
	return ToolTargetRequirements;
}

void UChaosClothAssetEditorMode::Enter()
{
	UBaseCharacterFXEditorMode::Enter();

	// Register gizmo ContextObject for use inside interactive tools
	UE::TransformGizmoUtil::RegisterTransformGizmoContextObject(GetInteractiveToolsContext());
}

void UChaosClothAssetEditorMode::AddToolTargetFactories()
{
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<UDynamicMeshComponentToolTargetFactory>(GetToolManager()));
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<UClothComponentToolTargetFactory>(GetToolManager()));
}


void UChaosClothAssetEditorMode::RegisterClothTool(TSharedPtr<FUICommandInfo> UICommand, FString ToolIdentifier, UInteractiveToolBuilder* Builder, UEditorInteractiveToolsContext* const ToolsContext, EToolsContextScope ToolScope)
{
	if (!Toolkit.IsValid())
	{
		return;
	}

	if (!ToolsContext)
	{
		return;
	}

	if (ToolScope == EToolsContextScope::Default)
	{
		ToolScope = GetDefaultToolScope();
	}
	ensure(ToolScope != EToolsContextScope::Editor);

	ToolsContext->ToolManager->RegisterToolType(ToolIdentifier, Builder);

	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();

	CommandList->MapAction(UICommand,
		FExecuteAction::CreateWeakLambda(ToolsContext, [this, ToolsContext, ToolIdentifier]()
		{
			ActiveToolsContext = ToolsContext;
			ToolsContext->StartTool(ToolIdentifier);
		}),
		FCanExecuteAction::CreateWeakLambda(ToolsContext, [this, ToolIdentifier, ToolsContext]() 
		{
			return ShouldToolStartBeAllowed(ToolIdentifier) &&
			ToolsContext->ToolManager->CanActivateTool(EToolSide::Mouse, ToolIdentifier);
		}),
		FIsActionChecked::CreateUObject(ToolsContext, &UEdModeInteractiveToolsContext::IsToolActive, EToolSide::Mouse, ToolIdentifier),
		EUIActionRepeatMode::RepeatDisabled);

}

void UChaosClothAssetEditorMode::RegisterAddNodeCommand(TSharedPtr<FUICommandInfo> AddNodeCommand, const FName& NewNodeType, TSharedPtr<FUICommandInfo> StartToolCommand)
{
	auto AddNode = [this](const FName& NewNodeType)
	{
		const FName ConnectionType = FManagedArrayCollection::StaticType();
		UEdGraphNode* const CurrentlySelectedNode = GetSingleSelectedNodeWithOutputType(ConnectionType);
		checkf(CurrentlySelectedNode, TEXT("No node with FManagedArrayCollection output is currently selected in the Dataflow graph"));
		verifyf(CreateAndConnectNewNode(NewNodeType, *CurrentlySelectedNode, ConnectionType), TEXT("Failed to create a new node: %s"), *NewNodeType.ToString());
	};

	auto CanAddNode = [this](const FName& NewNodeType) -> bool
	{
		const UEdGraphNode* const CurrentlySelectedNode = GetSingleSelectedNodeWithOutputType(FManagedArrayCollection::StaticType());
		return (CurrentlySelectedNode != nullptr);
	};

	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();

	CommandList->MapAction(AddNodeCommand,
		FExecuteAction::CreateWeakLambda(this, AddNode, NewNodeType),
		FCanExecuteAction::CreateWeakLambda(this, CanAddNode, NewNodeType)
	);

	NodeTypeToToolCommandMap.Add(NewNodeType, StartToolCommand);
}


void UChaosClothAssetEditorMode::RegisterPreviewTools()
{
	using namespace UE::Chaos::ClothAsset;

	UEditorInteractiveToolsContext* const PreviewSceneToolsContext = PreviewScene->GetClothPreviewEditorModeManager()->GetInteractiveToolsContext();

	const FChaosClothAssetEditorCommands& CommandInfos = FChaosClothAssetEditorCommands::Get();
	RegisterClothTool(CommandInfos.BeginClothTrainingTool, FChaosClothAssetEditorCommands::BeginClothTrainingToolIdentifier, NewObject<UClothTrainingToolBuilder>(), PreviewSceneToolsContext);
}

void UChaosClothAssetEditorMode::RegisterTools()
{
	using namespace UE::Chaos::ClothAsset;

	const FChaosClothAssetEditorCommands& CommandInfos = FChaosClothAssetEditorCommands::Get();

	UEditorInteractiveToolsContext* const ConstructionViewportToolsContext = GetInteractiveToolsContext();

	RegisterClothTool(CommandInfos.BeginAttributeEditorTool, FChaosClothAssetEditorCommands::BeginAttributeEditorToolIdentifier, NewObject<UAttributeEditorToolBuilder>(), ConstructionViewportToolsContext);

	RegisterClothTool(CommandInfos.BeginWeightMapPaintTool, FChaosClothAssetEditorCommands::BeginWeightMapPaintToolIdentifier, NewObject<UClothEditorWeightMapPaintToolBuilder>(), ConstructionViewportToolsContext);
	RegisterAddNodeCommand(CommandInfos.AddWeightMapNode, FChaosClothAssetAddWeightMapNode::StaticType(), CommandInfos.BeginWeightMapPaintTool);

	RegisterClothTool(CommandInfos.BeginTransferSkinWeightsTool, FChaosClothAssetEditorCommands::BeginTransferSkinWeightsToolIdentifier, NewObject<UClothTransferSkinWeightsToolBuilder>(), ConstructionViewportToolsContext);
	RegisterAddNodeCommand(CommandInfos.AddTransferSkinWeightsNode, FChaosClothAssetTransferSkinWeightsNode::StaticType(), CommandInfos.BeginTransferSkinWeightsTool);
}

bool UChaosClothAssetEditorMode::ShouldToolStartBeAllowed(const FString& ToolIdentifier) const
{
	// Allow switching away from tool if no changes have been made in the tool yet (which we infer from the CanAccept status)
	if (GetInteractiveToolsContext()->CanAcceptActiveTool())
	{
		return false;
	}
	
	if (PreviewScene && PreviewScene->GetClothPreviewEditorModeManager() && PreviewScene->GetClothPreviewEditorModeManager()->GetInteractiveToolsContext())
	{
		if (PreviewScene->GetClothPreviewEditorModeManager()->GetInteractiveToolsContext()->HasActiveTool())
		{
			return false;
		}
	}

	return Super::ShouldToolStartBeAllowed(ToolIdentifier);
}

void UChaosClothAssetEditorMode::CreateToolkit()
{
	Toolkit = MakeShared<UE::Chaos::ClothAsset::FChaosClothAssetEditorModeToolkit>();
}

void UChaosClothAssetEditorMode::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	using namespace UE::Chaos::ClothAsset;

	FChaosClothAssetEditorCommands::UpdateToolCommandBinding(Tool, ToolCommandList, false);

	bCanChangeConstructionViewMode = false;
}

void UChaosClothAssetEditorMode::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	UE::Chaos::ClothAsset::FChaosClothAssetEditorCommands::UpdateToolCommandBinding(Tool, ToolCommandList, true);

	bCanChangeConstructionViewMode = true;

	ReinitializeDynamicMeshComponents();

	if (TSharedPtr<SDataflowGraphEditor> GraphEditor = DataflowGraphEditor.Pin())
	{
		GraphEditor->SetEnabled(true);
	}
}

void UChaosClothAssetEditorMode::PostUndo()
{
	ReinitializeDynamicMeshComponents();
}

void UChaosClothAssetEditorMode::BindCommands()
{
	using namespace UE::Chaos::ClothAsset;
	const FChaosClothAssetEditorCommands& CommandInfos = FChaosClothAssetEditorCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();

	// Hook up to Enter/Esc key presses
	CommandList->MapAction(
		CommandInfos.AcceptOrCompleteActiveTool,
		FExecuteAction::CreateLambda([this]() { AcceptActiveToolActionOrTool(); }),
		FCanExecuteAction::CreateLambda([this]() {
			return GetInteractiveToolsContext()->CanAcceptActiveTool() || GetInteractiveToolsContext()->CanCompleteActiveTool();
		}),
		FGetActionCheckState(),
		FIsActionButtonVisible(),
		EUIActionRepeatMode::RepeatDisabled);

	CommandList->MapAction(
		CommandInfos.CancelOrCompleteActiveTool,
		FExecuteAction::CreateLambda([this]() { CancelActiveToolActionOrTool(); }),
		FCanExecuteAction::CreateLambda([this]() {
			return GetInteractiveToolsContext()->CanCompleteActiveTool() || GetInteractiveToolsContext()->CanCancelActiveTool();
		}),
		FGetActionCheckState(),
		FIsActionButtonVisible(),
		EUIActionRepeatMode::RepeatDisabled);
}


void UChaosClothAssetEditorMode::Exit()
{
	UActorComponent::MarkRenderStateDirtyEvent.RemoveAll(this);

	if (DynamicMeshComponent)
	{
		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->SelectionOverrideDelegate.Unbind();
	}
	DynamicMeshComponent = nullptr;
	DynamicMeshComponentParentActor = nullptr;

	if (WireframeToTick)
	{
		WireframeToTick->Disconnect();
	}
	WireframeToTick = nullptr;

	if (DataflowComponent)
	{
		DataflowComponent->UnregisterComponent();
		DataflowComponent->DestroyComponent();
	}

	PropertyObjectsToTick.Empty();
	PreviewScene = nullptr;

	Super::Exit();
}

void UChaosClothAssetEditorMode::SetPreviewScene(UE::Chaos::ClothAsset::FChaosClothPreviewScene* InPreviewScene)
{
	using namespace UE::Chaos::ClothAsset;

	PreviewScene = InPreviewScene;


	UEditorInteractiveToolsContext* const PreviewToolsContext = PreviewScene->GetClothPreviewEditorModeManager()->GetInteractiveToolsContext();
	UInteractiveToolManager* const PreviewToolManager = PreviewToolsContext->ToolManager;
	PreviewToolsContext->TargetManager->AddTargetFactory(NewObject<UClothComponentToolTargetFactory>(PreviewToolManager));
	PreviewToolsContext->TargetManager->AddTargetFactory(NewObject<USkeletalMeshComponentToolTargetFactory>(PreviewToolManager));

	PreviewToolManager->OnToolStarted.AddUObject(this, &UChaosClothAssetEditorMode::OnToolStarted);
	PreviewToolManager->OnToolEnded.AddUObject(this, &UChaosClothAssetEditorMode::OnToolEnded);

	check(Toolkit.IsValid());
	
	// FBaseToolkit's OnToolStarted and OnToolEnded are protected, so we use the subclass to get at them
	FChaosClothAssetEditorModeToolkit* const ClothModeToolkit = static_cast<FChaosClothAssetEditorModeToolkit*>(Toolkit.Get());	

	PreviewToolManager->OnToolStarted.AddSP(ClothModeToolkit, &FChaosClothAssetEditorModeToolkit::OnToolStarted);
	PreviewToolManager->OnToolEnded.AddSP(ClothModeToolkit, &FChaosClothAssetEditorModeToolkit::OnToolEnded);

	// TODO: It would be nice if the PreviewScene could be specified before the UBaseCharacterFXEditorMode::Enter() function is called. Then we could register both sets of tools from there.
	RegisterPreviewTools();
}

void UChaosClothAssetEditorMode::CreateToolTargets(const TArray<TObjectPtr<UObject>>& AssetsIn) 
{
	// TODO: When we have a cloth component tool target, create it here

}


bool UChaosClothAssetEditorMode::IsComponentSelected(const UPrimitiveComponent* InComponent)
{
	if (const FEditorModeTools* const ModeManager = GetModeManager())
	{
		if (const UTypedElementSelectionSet* const TypedElementSelectionSet = ModeManager->GetEditorSelectionSet())
		{
			if (const FTypedElementHandle ComponentElement = UEngineElementsLibrary::AcquireEditorComponentElementHandle(InComponent))
			{
				const bool bElementSelected = TypedElementSelectionSet->IsElementSelected(ComponentElement, FTypedElementIsSelectedOptions());
				return bElementSelected;
			}
		}
	}

	return false;
}


void UChaosClothAssetEditorMode::SetSelectedClothCollection(TSharedPtr<FManagedArrayCollection> Collection)
{
	SelectedClothCollection = Collection;
	ReinitializeDynamicMeshComponents();

	if (bFirstClothCollection && Collection)
	{
		// refocus the viewport if this is the first time a cloth collection has been set
		RefocusRestSpaceViewportClient();
		bFirstClothCollection = false;
	}
}

TSharedPtr<FManagedArrayCollection> UChaosClothAssetEditorMode::GetClothCollection()
{
	return SelectedClothCollection;

	// TODO: If no cloth collection node is selected, show the ClothAsset's collection. In this case, also ensure that any interactive tools are disabled. (UE-181574)
}


void UChaosClothAssetEditorMode::ReinitializeDynamicMeshComponents()
{
	using namespace UE::Chaos::ClothAsset;

	auto SetUpDynamicMeshComponentMaterial = [this](const UE::Chaos::ClothAsset::FCollectionClothConstFacade& ClothFacade, UDynamicMeshComponent& MeshComponent)
	{
		switch (ConstructionViewMode)
		{
			case EClothPatternVertexType::Sim2D:
			{
				UMaterialInterface* const Material = ToolSetupUtil::GetCustomTwoSidedDepthOffsetMaterial(GetToolManager(), FLinearColor{ 0.6, 0.6, 0.6 }, 0.0);
				MeshComponent.SetMaterial(0, Material);
			}
			break;
			case EClothPatternVertexType::Sim3D:
			{
				UMaterialInterface* const Material = ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager());
				MeshComponent.SetMaterial(0, Material);
			}
			break;
			case EClothPatternVertexType::Render:
			{
				const TArrayView<const FString> MaterialPaths = ClothFacade.GetRenderMaterialPathName();
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialPaths.Num(); ++MaterialIndex)
				{
					const FString& Path = MaterialPaths[MaterialIndex];
					UMaterialInterface* const Material = LoadObject<UMaterialInterface>(nullptr, *Path);
					MeshComponent.SetMaterial(MaterialIndex, Material);
				}

				// Fix up any triangles without valid material IDs
				int32 DefaultMaterialID = INDEX_NONE;
				for (const int32 TriID : MeshComponent.GetMesh()->TriangleIndicesItr())
				{
					const int32 MaterialID = MeshComponent.GetMesh()->Attributes()->GetMaterialID()->GetValue(TriID);
					if (!MeshComponent.GetMaterial(MaterialID))
					{
						if (DefaultMaterialID == INDEX_NONE)
						{
							DefaultMaterialID = MeshComponent.GetNumMaterials();
							MeshComponent.SetMaterial(DefaultMaterialID, UMaterial::GetDefaultMaterial(MD_Surface));
						}
						MeshComponent.GetMesh()->Attributes()->GetMaterialID()->SetValue(TriID, DefaultMaterialID);
					}
				}

			}
			break;
		}
	};
	
	// Clean up existing DynamicMeshComponent
	// Save indices of selected mesh components

	USelection* SelectedComponents = GetModeManager()->GetSelectedComponents();

	if (DynamicMeshComponent)
	{
		// TODO: Check for any outstanding changes on the dynamic mesh component?
		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->SelectionOverrideDelegate.Unbind();

		if (SelectedComponents->IsSelected(DynamicMeshComponent))
		{
			SelectedComponents->Deselect(DynamicMeshComponent);
			DynamicMeshComponent->PushSelectionToProxy();
		}
	}

	if (WireframeToTick)
	{
		WireframeToTick->Disconnect();
	}

	PropertyObjectsToTick.Empty();	// TODO: We only want to empty the wireframe display properties. Is anything else using this array?
	DynamicMeshComponent = nullptr;
	DynamicMeshComponentParentActor = nullptr;
	WireframeToTick = nullptr;

	TSharedPtr<FManagedArrayCollection> Collection = GetClothCollection();
	if (!Collection)
	{
		return;
	}

	const UE::Chaos::ClothAsset::FCollectionClothConstFacade ClothFacade(Collection);

	UE::Geometry::FDynamicMesh3 LodMesh;
	LodMesh.EnableAttributes();
	FClothPatternToDynamicMesh Converter;
	Converter.Convert(Collection, INDEX_NONE, ConstructionViewMode, LodMesh);

	if (ConstructionViewMode == EClothPatternVertexType::Sim2D)
	{
		// Use per-triangle normals for the 2D view
		UE::Geometry::FMeshNormals::InitializeMeshToPerTriangleNormals(&LodMesh);
	}

	// We only need an actor to allow use of HHitProxy for selection
	const FRotator Rotation(0.0f, 0.0f, 0.0f);
	const FActorSpawnParameters SpawnInfo;
	DynamicMeshComponentParentActor = this->GetWorld()->SpawnActor<AActor>(FVector::ZeroVector, Rotation, SpawnInfo);

	DynamicMeshComponent = NewObject<UDynamicMeshComponent>(DynamicMeshComponentParentActor);
	DynamicMeshComponent->SetMesh(MoveTemp(LodMesh));

	SetUpDynamicMeshComponentMaterial(ClothFacade, *DynamicMeshComponent);

	DynamicMeshComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateUObject(this, &UChaosClothAssetEditorMode::IsComponentSelected);
	DynamicMeshComponent->RegisterComponentWithWorld(this->GetWorld());

	// Set up the wireframe display of the rest space mesh.
	WireframeToTick = NewObject<UMeshElementsVisualizer>(this);
	WireframeToTick->CreateInWorld(GetWorld(), FTransform::Identity);

	WireframeToTick->Settings->DepthBias = 2.0;
	WireframeToTick->Settings->bAdjustDepthBiasUsingMeshSize = false;
	WireframeToTick->Settings->bShowWireframe = true;
	WireframeToTick->Settings->bShowBorders = true;
	WireframeToTick->Settings->bShowUVSeams = false;
	WireframeToTick->Settings->bShowNormalSeams = false;

	// These are not exposed at the visualizer level yet
	// TODO: Should they be?
	WireframeToTick->WireframeComponent->BoundaryEdgeThickness = 2;

	WireframeToTick->SetMeshAccessFunction([this](UMeshElementsVisualizer::ProcessDynamicMeshFunc ProcessFunc)
	{
		ProcessFunc(*DynamicMeshComponent->GetMesh());
	});

	DynamicMeshComponent->OnMeshChanged.Add(
		FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
		{
			WireframeToTick->NotifyMeshChanged();
		}));

	// The settings object and wireframe are not part of a tool, so they won't get ticked like they
	// are supposed to (to enable property watching), unless we add this here.
	PropertyObjectsToTick.Add(WireframeToTick->Settings);

	// Some interactive tools will hide the input DynamicMeshComponent and create their own temporary PreviewMesh for visualization. If this
	// occurs, we should also hide the corresponding WireframeDisplay (and un-hide it when the tool finishes).
	UActorComponent::MarkRenderStateDirtyEvent.AddWeakLambda(this, [this](UActorComponent& ActorComponent)
	{
		if (!WireframeToTick || !DynamicMeshComponent)
		{
			return;
		}
		const bool bRestSpaceMeshVisible = DynamicMeshComponent->GetVisibleFlag();
		WireframeToTick->Settings->bVisible = bRestSpaceMeshVisible && bConstructionViewWireframe;
	});

	const bool bRestSpaceMeshVisible = DynamicMeshComponent->GetVisibleFlag();
	WireframeToTick->Settings->bVisible = bRestSpaceMeshVisible && bConstructionViewWireframe;

	SelectedComponents->DeselectAll();
	if (ConstructionViewMode != EClothPatternVertexType::Render)
	{
		SelectedComponents->Select(DynamicMeshComponent);
		DynamicMeshComponent->PushSelectionToProxy();
	}

	// Update the context object with the ConstructionViewMode and Collection used to build the DynamicMeshComponents, so 
	// tools know how to use the components.
	UEditorInteractiveToolsContext* const RestSpaceToolsContext = GetInteractiveToolsContext();
	UClothEditorContextObject* EditorContextObject = RestSpaceToolsContext->ContextObjectStore->FindContext<UClothEditorContextObject>();
	if (ensure(EditorContextObject))
	{
		EditorContextObject->SetClothCollection(ConstructionViewMode, Collection);
	}
}

void UChaosClothAssetEditorMode::RefocusRestSpaceViewportClient()
{
	TSharedPtr<UE::Chaos::ClothAsset::FChaosClothEditorRestSpaceViewportClient, ESPMode::ThreadSafe> PinnedVC = RestSpaceViewportClient.Pin();
	if (PinnedVC.IsValid())
	{
		// This will happen in FocusViewportOnBox anyways; do it now to get a consistent end result
		PinnedVC->ToggleOrbitCamera(false);

		const FBox SceneBounds = SceneBoundingBox();
		const bool bPattern2DMode = (ConstructionViewMode == UE::Chaos::ClothAsset::EClothPatternVertexType::Sim2D);
		if (bPattern2DMode)
		{
			// 2D pattern
			PinnedVC->SetInitialViewTransform(ELevelViewportType::LVT_Perspective, FVector(0, 0, -100), FRotator(90, -90, 0), DEFAULT_ORTHOZOOM);
		}
		else
		{
			// 3D rest space
			PinnedVC->SetInitialViewTransform(ELevelViewportType::LVT_Perspective, FVector(0, 150, 200), FRotator(0, 0, 0), DEFAULT_ORTHOZOOM);
		}

		constexpr bool bInstant = true;
		PinnedVC->FocusViewportOnBox(SceneBounds, bInstant);

		// Recompute near/far clip planes
		PinnedVC->SetConstructionViewMode(ConstructionViewMode);
	}
}

void UChaosClothAssetEditorMode::InitializeTargets(const TArray<TObjectPtr<UObject>>& AssetsIn)
{
	// InitializeContexts needs to have been called first so that we have the 3d preview world ready.
	check(PreviewScene);

	UBaseCharacterFXEditorMode::InitializeTargets(AssetsIn);

	DataflowComponent = NewObject<UDataflowComponent>();
	DataflowComponent->RegisterComponentWithWorld(PreviewScene->GetWorld());
}

void UChaosClothAssetEditorMode::SoftResetSimulation()
{
	bShouldResetSimulation = true;
	bShouldClearTeleportFlag = false;
	bHardReset = false;
}

void UChaosClothAssetEditorMode::HardResetSimulation()
{
	bShouldResetSimulation = true;
	bShouldClearTeleportFlag = false;
	bHardReset = true;
}

void UChaosClothAssetEditorMode::SuspendSimulation()
{
	if (PreviewScene && PreviewScene->GetClothComponent())
	{
		PreviewScene->GetClothComponent()->SuspendSimulation();
	}
}

void UChaosClothAssetEditorMode::ResumeSimulation()
{
	if (PreviewScene && PreviewScene->GetClothComponent())
	{
		PreviewScene->GetClothComponent()->ResumeSimulation();
	}
}

bool UChaosClothAssetEditorMode::IsSimulationSuspended() const
{
	if (PreviewScene && PreviewScene->GetClothComponent())
	{
		return PreviewScene->GetClothComponent()->IsSimulationSuspended();
	}

	return false;
}

void UChaosClothAssetEditorMode::SetEnableSimulation(bool bEnable)
{
	if (PreviewScene && PreviewScene->GetClothComponent())
	{
		PreviewScene->GetClothComponent()->SetEnableSimulation(bEnable);
	}
}

bool UChaosClothAssetEditorMode::IsSimulationEnabled() const
{
	if (PreviewScene && PreviewScene->GetClothComponent())
	{
		return PreviewScene->GetClothComponent()->IsSimulationEnabled();
	}

	return false;
}

UDataflowComponent* UChaosClothAssetEditorMode::GetDataflowComponent() const
{
	return DataflowComponent;
}

void UChaosClothAssetEditorMode::ModeTick(float DeltaTime)
{
	Super::ModeTick(DeltaTime);
	

	if (TSharedPtr<SDataflowGraphEditor> GraphEditor = DataflowGraphEditor.Pin())
	{
		// For now don't allow selection change once the tool has uncommitted changes
		// TODO: We might want to auto-accept unsaved changes and allow switching between nodes
		if (GetInteractiveToolsContext()->CanAcceptActiveTool())
		{
			GraphEditor->SetEnabled(false);
		}
		else
		{
			GraphEditor->SetEnabled(true);
		}
	}


	for (TObjectPtr<UInteractiveToolPropertySet>& Propset : PropertyObjectsToTick)
	{
		if (Propset)
		{
			if (Propset->IsPropertySetEnabled())
			{
				Propset->CheckAndUpdateWatched();
			}
			else
			{
				Propset->SilentUpdateWatched();
			}
		}
	}

	if (WireframeToTick)
	{
		WireframeToTick->OnTick(DeltaTime);
	}


	if (bShouldClearTeleportFlag)
	{
		PreviewScene->GetClothComponent()->ResetTeleportMode();
		bShouldClearTeleportFlag = false;
	}

	if (bShouldResetSimulation)
	{
		if (bHardReset)
		{
			const FComponentReregisterContext Context(PreviewScene->GetClothComponent());
		}
		else
		{
			PreviewScene->GetClothComponent()->ForceNextUpdateTeleportAndReset();
		}

		bShouldResetSimulation = false;
		bShouldClearTeleportFlag = true;		// clear the flag next tick
	}

	if (PreviewScene->GetWorld())
	{
		PreviewScene->GetWorld()->Tick(ELevelTick::LEVELTICK_All, DeltaTime);
	}
}

void UChaosClothAssetEditorMode::RestSpaceViewportResized(FViewport* RestspaceViewport, uint32 /*Unused*/)
{
	// We'd like to call RefocusRestSpaceViewportClient() when the viewport is first created, however in Ortho mode the
	// viewport needs to have non-zero size for FocusViewportOnBox() to work properly. So we wait until the viewport is resized here.
	if (bShouldFocusRestSpaceView && RestspaceViewport && RestspaceViewport->GetSizeXY().X > 0 && RestspaceViewport->GetSizeXY().Y > 0)
	{
		RefocusRestSpaceViewportClient();
		bShouldFocusRestSpaceView = false;
	}
}

FBox UChaosClothAssetEditorMode::SceneBoundingBox() const
{
	FBoxSphereBounds TotalBounds(ForceInitToZero);
	
	if (DynamicMeshComponent)
	{
		TotalBounds = DynamicMeshComponent->Bounds;
	}

	return TotalBounds.GetBox();
}

FBox UChaosClothAssetEditorMode::SelectionBoundingBox() const
{
	const USelection* const SelectedComponents = GetModeManager()->GetSelectedComponents();

	if (DynamicMeshComponent && SelectedComponents->IsSelected(DynamicMeshComponent))
	{
		return DynamicMeshComponent->Bounds.GetBox();
	}
	
	// Nothing selected, return the whole scene
	return SceneBoundingBox();
}


FBox UChaosClothAssetEditorMode::PreviewBoundingBox() const
{
	FBox Bounds(ForceInit);

	if (const UChaosClothComponent* const Cloth = PreviewScene->GetClothComponent())
	{
		Bounds += Cloth->Bounds.GetBox();
	}

	if (const USkeletalMeshComponent* const SkeletalMesh = PreviewScene->GetSkeletalMeshComponent())
	{
		Bounds += SkeletalMesh->Bounds.GetBox();
	}

	return Bounds;
}

void UChaosClothAssetEditorMode::SetConstructionViewMode(UE::Chaos::ClothAsset::EClothPatternVertexType InMode)
{
	const bool bSwitching2D3D = (ConstructionViewMode == UE::Chaos::ClothAsset::EClothPatternVertexType::Sim2D) != (InMode == UE::Chaos::ClothAsset::EClothPatternVertexType::Sim2D);

	ConstructionViewMode = InMode;
	ReinitializeDynamicMeshComponents();

	TSharedPtr<UE::Chaos::ClothAsset::FChaosClothEditorRestSpaceViewportClient> VC = RestSpaceViewportClient.Pin();
	if (VC.IsValid())
	{
		VC->SetConstructionViewMode(ConstructionViewMode);
	}

	if (bSwitching2D3D)
	{
		RefocusRestSpaceViewportClient();
	}
}

UE::Chaos::ClothAsset::EClothPatternVertexType UChaosClothAssetEditorMode::GetConstructionViewMode() const
{
	return ConstructionViewMode;
}

bool UChaosClothAssetEditorMode::CanChangeConstructionViewMode() const
{
	return bCanChangeConstructionViewMode;
}

void UChaosClothAssetEditorMode::ToggleConstructionViewWireframe()
{
	bConstructionViewWireframe = !bConstructionViewWireframe;
	ReinitializeDynamicMeshComponents();
}

void UChaosClothAssetEditorMode::SetRestSpaceViewportClient(TWeakPtr<UE::Chaos::ClothAsset::FChaosClothEditorRestSpaceViewportClient, ESPMode::ThreadSafe> InViewportClient)
{
	RestSpaceViewportClient = InViewportClient;

	TSharedPtr<UE::Chaos::ClothAsset::FChaosClothEditorRestSpaceViewportClient> VC = RestSpaceViewportClient.Pin();
	if (VC.IsValid())
	{
		VC->SetConstructionViewMode(ConstructionViewMode);
		VC->SetToolCommandList(ToolCommandList);

		if (VC->Viewport)
		{
			VC->Viewport->ViewportResizedEvent.AddUObject(this, &UChaosClothAssetEditorMode::RestSpaceViewportResized);
		}
	}
}


void UChaosClothAssetEditorMode::InitializeContextObject()
{
	UEditorInteractiveToolsContext* const RestSpaceToolsContext = GetInteractiveToolsContext();

	UClothEditorContextObject* EditorContextObject = RestSpaceToolsContext->ContextObjectStore->FindContext<UClothEditorContextObject>();
	if (!EditorContextObject)
	{
		EditorContextObject = NewObject<UClothEditorContextObject>();
		RestSpaceToolsContext->ContextObjectStore->AddContextObject(EditorContextObject);
	}

	EditorContextObject->Init(DataflowGraphEditor, ConstructionViewMode, SelectedClothCollection);

	check(EditorContextObject);

}

void UChaosClothAssetEditorMode::DeleteContextObject()
{
	UEditorInteractiveToolsContext* const RestSpaceToolsContext = GetInteractiveToolsContext();
	if (UClothEditorContextObject* ClothEditorContextObject = RestSpaceToolsContext->ContextObjectStore->FindContext<UClothEditorContextObject>())
	{
		RestSpaceToolsContext->ContextObjectStore->RemoveContextObject(ClothEditorContextObject);
	}
}

void UChaosClothAssetEditorMode::SetDataflowGraphEditor(TSharedPtr<SDataflowGraphEditor> InGraphEditor)
{
	if (InGraphEditor)
	{
		DataflowGraphEditor = InGraphEditor;
		InitializeContextObject();
	}
	else
	{
		DeleteContextObject();
	}
}

void UChaosClothAssetEditorMode::OnDataflowNodeSelectionChanged(const TSet<UObject*>& NewSelection)
{
	using namespace UE::Chaos::ClothAsset;

	// Start the tool associated with the selected node, if any

	const TSharedRef<FUICommandList> CommandList = Toolkit->GetToolkitCommands();
	const FChaosClothAssetEditorCommands& CommandInfos = FChaosClothAssetEditorCommands::Get();

	bool bNewToolStarted = false;
	if (NewSelection.Num() == 1)
	{
		if (const UDataflowEdNode* const Node = Cast<UDataflowEdNode>(*NewSelection.CreateConstIterator()))
		{
			if (const TSharedPtr<const FDataflowNode> DataflowNode = Node->GetDataflowNode())
			{
				const FName DataflowNodeName = DataflowNode->GetType();
				if (NodeTypeToToolCommandMap.Contains(DataflowNodeName))
				{
					CommandList->TryExecuteAction(NodeTypeToToolCommandMap[DataflowNodeName].ToSharedRef());
					bNewToolStarted = true;
				}
			}
		}
	}

	UEditorInteractiveToolsContext* const ToolsContext = GetInteractiveToolsContext();
	checkf(ToolsContext, TEXT("No valid ToolsContext found for UChaosClothAssetEditorMode"));
	if (!bNewToolStarted && ToolsContext->HasActiveTool())
	{
		// The user has clicked away from the selected node, end the tool
		// TODO: If we wanted we could probably auto-accept the pending changes in the active tool
		ToolsContext->EndTool(EToolShutdownType::Completed);
	}

}


void UChaosClothAssetEditorMode::OnDataflowNodeDeleted(const TSet<UObject*>& DeletedNodes)
{
	UEditorInteractiveToolsContext* const ToolsContext = GetInteractiveToolsContext();
	checkf(ToolsContext, TEXT("No valid ToolsContext found for UChaosClothAssetEditorMode"));
	const bool bCanCancel = ToolsContext->CanCancelActiveTool();
	ToolsContext->EndTool(bCanCancel ? EToolShutdownType::Cancel : EToolShutdownType::Completed);
}

UEdGraphNode* UChaosClothAssetEditorMode::GetSingleSelectedNodeWithOutputType(const FName& SelectedNodeOutputTypeName) const
{
	const TSharedPtr<const SDataflowGraphEditor> PinnedDataflowGraphEditor = DataflowGraphEditor.Pin();
	if (!PinnedDataflowGraphEditor)
	{
		return nullptr;
	}

	UEdGraphNode* const SelectedNode = PinnedDataflowGraphEditor->GetSingleSelectedNode();
	if (!SelectedNode)
	{
		return nullptr;
	}

	const UDataflowEdNode* const SelectedDataflowEdNode = CastChecked<UDataflowEdNode>(SelectedNode);
	const TSharedPtr<const FDataflowNode> SelectedDataflowNode = SelectedDataflowEdNode->GetDataflowNode();

	if (!SelectedDataflowNode)
	{
		// This can happen when the user deletes a node. Seems like the Dataflow FGraph is updated with the removed node before the graph editor can update.
		return nullptr;
	}

	for (const FDataflowOutput* const Output : SelectedDataflowNode->GetOutputs())
	{
		if (Output->GetType() == SelectedNodeOutputTypeName)
		{
			return SelectedNode;
		}
	}

	return nullptr;
}

UEdGraphNode* UChaosClothAssetEditorMode::CreateNewNode(const FName& NewNodeTypeName)
{
	const TSharedPtr<const SDataflowGraphEditor> PinnedDataflowGraphEditor = DataflowGraphEditor.Pin();
	if (!PinnedDataflowGraphEditor)
	{
		return nullptr;
	}

	const TSharedPtr<FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode> NodeAction =
		FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::CreateAction(DataflowGraph, NewNodeTypeName);
	constexpr UEdGraphPin* FromPin = nullptr;
	constexpr bool bSelectNewNode = true;
	UEdGraphNode* const NewEdNode = NodeAction->PerformAction(DataflowGraph, FromPin, PinnedDataflowGraphEditor->GetPasteLocation(), bSelectNewNode);

	return NewEdNode;
}


UEdGraphNode* UChaosClothAssetEditorMode::CreateAndConnectNewNode(const FName& NewNodeTypeName, UEdGraphNode& UpstreamNode, const FName& ConnectionTypeName)
{
	// First find the specified output of the upstream node, plus any pins it's connected to

	UEdGraphPin* UpstreamNodeOutputPin = nullptr;
	TArray<UEdGraphPin*> ExistingNodeInputPins;

	const UDataflowEdNode* const UpstreamDataflowEdNode = CastChecked<UDataflowEdNode>(&UpstreamNode);
	const TSharedPtr<const FDataflowNode> UpstreamDataflowNode = UpstreamDataflowEdNode->GetDataflowNode();

	for (const FDataflowOutput* const Output : UpstreamDataflowNode->GetOutputs())
	{
		if (Output->GetType() == ConnectionTypeName)
		{
			UpstreamNodeOutputPin = UpstreamDataflowEdNode->FindPin(*Output->GetName().ToString(), EGPD_Output);
			ExistingNodeInputPins = UpstreamNodeOutputPin->LinkedTo;
			break;
		}
	}

	// Add the new node 

	UEdGraphNode* const NewEdNode = CreateNewNode(NewNodeTypeName);
	checkf(NewEdNode, TEXT("Failed to create a new node in the DataflowGraph"));

	UDataflowEdNode* const NewDataflowEdNode = CastChecked<UDataflowEdNode>(NewEdNode);
	const TSharedPtr<FDataflowNode> NewDataflowNode = NewDataflowEdNode->GetDataflowNode();

	// Re-wire the graph

	if (UpstreamNodeOutputPin)
	{
		UEdGraphPin* NewNodeInputPin = nullptr;
		for (const FDataflowInput* const NewNodeInput : NewDataflowNode->GetInputs())
		{
			if (NewNodeInput->GetType() == ConnectionTypeName)
			{
				NewNodeInputPin = NewDataflowEdNode->FindPin(*NewNodeInput->GetName().ToString(), EGPD_Input);
			}
		}

		UEdGraphPin* NewNodeOutputPin = nullptr;
		for (const FDataflowOutput* const NewNodeOutput : NewDataflowNode->GetOutputs())
		{
			if (NewNodeOutput->GetType() == ConnectionTypeName)
			{
				NewNodeOutputPin = NewDataflowEdNode->FindPin(*NewNodeOutput->GetName().ToString(), EGPD_Output);
				break;
			}
		}

		check(NewNodeInputPin);
		check(NewNodeOutputPin);

		DataflowGraph->GetSchema()->TryCreateConnection(UpstreamNodeOutputPin, NewNodeInputPin);

		for (UEdGraphPin* DownstreamInputPin : ExistingNodeInputPins)
		{
			DataflowGraph->GetSchema()->TryCreateConnection(NewNodeOutputPin, DownstreamInputPin);
		}
	}

	DataflowGraph->NotifyGraphChanged();

	return NewEdNode;
}

#undef LOCTEXT_NAMESPACE

