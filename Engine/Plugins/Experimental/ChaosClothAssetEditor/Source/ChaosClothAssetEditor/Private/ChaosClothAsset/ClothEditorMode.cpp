// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditorMode.h"
#include "ChaosClothAsset/ClothEditorCommands.h"
#include "ChaosClothAsset/ClothEditorModeToolkit.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "ChaosClothAsset/ClothAdapter.h"
#include "ChaosClothAsset/ClothEditorRestSpaceViewportClient.h"
#include "ChaosClothAsset/ClothPatternToDynamicMesh.h"
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
#include "ToolTargets/StaticMeshComponentToolTarget.h"
#include "ToolTargets/VolumeComponentToolTarget.h"
#include "ToolTargets/DynamicMeshComponentToolTarget.h"
#include "ToolTargets/SkeletalMeshComponentToolTarget.h"
#include "Engine/Selection.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "RemeshMeshTool.h"
#include "AttributeEditorTool.h"
#include "MeshAttributePaintTool.h"
#include "ChaosClothAsset/ClothWeightMapPaintTool.h"
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

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothEditorMode)

#define LOCTEXT_NAMESPACE "UChaosClothAssetEditorMode"

const FEditorModeID UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId = TEXT("EM_ChaosClothAssetEditorMode");

namespace ChaosClothAssetEditorModeLocals
{
	void LogAndToastWarning(const FText& Error)
	{
		FNotificationInfo Info(Error);
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);

		// TODO: use a non-temp category
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Error.ToString());
	}

	FBoxSphereBounds RenderMeshBounds(const UE::Chaos::ClothAsset::FClothPatternConstAdapter& ClothPatternAdapter)
	{
		const TSharedPtr<const UE::Chaos::ClothAsset::FClothCollection> ClothCollection = ClothPatternAdapter.GetClothCollection();
		const int32 PatternElementIndex = ClothPatternAdapter.GetElementIndex();

		const int32 RenderVerticesStart = ClothCollection->RenderVerticesStart[PatternElementIndex];
		const int32 RenderVerticesEnd = ClothCollection->RenderVerticesEnd[PatternElementIndex];

		const FVector3f* RawArray = &ClothCollection->RenderPosition[0] + RenderVerticesStart;
		const int32 NumPoints = RenderVerticesEnd - RenderVerticesStart;
		const FBoxSphereBounds3f Bounds(RawArray, NumPoints);

		return FBoxSphereBounds(Bounds);
	}

}		// namespace ChaosClothAssetEditorModeLocals

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

UChaosClothAssetEditorMode::UChaosClothAssetEditorMode()
{
	Info = FEditorModeInfo(
		EM_ChaosClothAssetEditorModeId,
		LOCTEXT("ChaosClothAssetEditorModeName", "Cloth"),
		FSlateIcon(),
		false);
}

void UChaosClothAssetEditorMode::Enter()
{
	UBaseCharacterFXEditorMode::Enter();

	SelectionModifiedEventHandle = USelection::SelectionChangedEvent.AddLambda([this](UObject*)
	{
		if (!GetModeManager() || !GetModeManager()->GetSelectedComponents())
		{
			return;
		}

		constexpr FColor UnselectedColor = FColor(128, 128, 128);
		const FColor SelectedColor = GetDefault<UEditorStyleSettings>()->SelectionColor.ToFColor(true);

		const USelection* const SelectedComponents = GetModeManager()->GetSelectedComponents();

		check(DynamicMeshComponents.Num() == WireframesToTick.Num());

		for (int MeshIndex = 0; MeshIndex < WireframesToTick.Num(); ++MeshIndex)
		{
			const TObjectPtr<UMeshElementsVisualizer> WireframeDisplay = WireframesToTick[MeshIndex];
			if (SelectedComponents->IsSelected(DynamicMeshComponents[MeshIndex]))
			{
				WireframeDisplay->Settings->WireframeColor = SelectedColor;
			}
			else
			{
				WireframeDisplay->Settings->WireframeColor = UnselectedColor;
			}
		}
	});

}

void UChaosClothAssetEditorMode::AddToolTargetFactories()
{
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<UStaticMeshComponentToolTargetFactory>(GetToolManager()));
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<UVolumeComponentToolTargetFactory>(GetToolManager()));
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<UDynamicMeshComponentToolTargetFactory>(GetToolManager()));
}

void UChaosClothAssetEditorMode::RegisterTools()
{
	const FChaosClothAssetEditorCommands& CommandInfos = FChaosClothAssetEditorCommands::Get();

	// Note that the identifiers below need to match the command names so that the tool icons can 
	// be easily retrieved from the active tool name in ChaosClothAssetEditorModeToolkit::OnToolStarted. Otherwise
	// we would need to keep some other mapping from tool identifier to tool icon.

	RegisterTool(CommandInfos.BeginRemeshTool, FChaosClothAssetEditorCommands::BeginRemeshToolIdentifier, NewObject<URemeshMeshToolBuilder>());
	RegisterTool(CommandInfos.BeginAttributeEditorTool, FChaosClothAssetEditorCommands::BeginAttributeEditorToolIdentifier, NewObject<UAttributeEditorToolBuilder>());
	RegisterTool(CommandInfos.BeginWeightMapPaintTool, FChaosClothAssetEditorCommands::BeginWeightMapPaintToolIdentifier, NewObject<UClothEditorWeightMapPaintToolBuilder>());
}

bool UChaosClothAssetEditorMode::ShouldToolStartBeAllowed(const FString& ToolIdentifier) const
{
	// For now we've decided to disallow switch-away on accept/cancel tools in the Cloth editor.
	if (GetInteractiveToolsContext()->ActiveToolHasAccept())
	{
		return false;
	}
	
	return Super::ShouldToolStartBeAllowed(ToolIdentifier);
}

void UChaosClothAssetEditorMode::CreateToolkit()
{
	Toolkit = MakeShared<FChaosClothAssetEditorModeToolkit>();
}

void UChaosClothAssetEditorMode::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	FChaosClothAssetEditorCommands::UpdateToolCommandBinding(Tool, ToolCommandList, false);

	bCanTogglePattern2DMode = false;
}

void UChaosClothAssetEditorMode::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	FChaosClothAssetEditorCommands::UpdateToolCommandBinding(Tool, ToolCommandList, true);

	bCanTogglePattern2DMode = true;

	UpdateSimulationMeshes();
	ReinitializeDynamicMeshComponents();
}

void UChaosClothAssetEditorMode::PostUndo()
{
	ReinitializeDynamicMeshComponents();
}

void UChaosClothAssetEditorMode::BindCommands()
{
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
	USelection::SelectionChangedEvent.Remove(SelectionModifiedEventHandle);

	for (TObjectPtr<UMeshElementsVisualizer> WireframeDisplay : WireframesToTick)
	{
		WireframeDisplay->Disconnect();
	}
	WireframesToTick.Reset();
	DynamicMeshComponents.Reset();
	PropertyObjectsToTick.Empty();
	PreviewWorld = nullptr;

	Super::Exit();
}

void UChaosClothAssetEditorMode::InitializeContexts(FEditorViewportClient& LivePreviewViewportClient, FAssetEditorModeManager& LivePreviewModeManager)
{
	PreviewWorld = LivePreviewModeManager.GetPreviewScene()->GetWorld();

	// UContextObjectStore* ContextStore = GetInteractiveToolsContext()->ToolManager->GetContextObjectStore();

	// TODO: If we are going to support tools that operate on the 3D sim mesh, we'll need something like this to pass into those tools:
	//UClothToolLivePreviewAPI* LivePreviewAPI = NewObject<UClothToolLivePreviewAPI>();
	//LivePreviewAPI->Initialize(
	//	LivePreviewWorld,
	//	LivePreviewModeManager.GetInteractiveToolsContext()->InputRouter,
	//	[this, LivePreviewViewportClientPtr = &LivePreviewViewportClient](FViewCameraState& CameraStateOut) {
	//		GetCameraState(*LivePreviewViewportClientPtr, CameraStateOut); 
	//	});
	//ContextStore->AddContextObject(LivePreviewAPI);

	// TODO: If we add buttons to the viewport that the tools will need to interact with (e.g. selection of triangles, edges, etc.), we will need something like this:
	//ContextStore->AddContextObject(&ViewportButtonsAPI);
}

void UChaosClothAssetEditorMode::GetRestSpaceMesh(UToolTarget* ToolTarget, UE::Geometry::FDynamicMesh3& RestSpaceMesh)
{
	// TODO: Get the actual rest-space mesh. NOTE: Currently full 3D mesh (not 2D projection)
	RestSpaceMesh = UE::ToolTarget::GetDynamicMeshCopy(ToolTarget);
}

namespace ChaosClothAssetEditorModeHelpers
{
	TArray<FName> GetClothAssetWeightMapNames(const UE::Chaos::ClothAsset::FClothAdapter& ClothAdapter)
	{
		const TSharedPtr<const UE::Chaos::ClothAsset::FClothCollection> ClothCollection = ClothAdapter.FClothConstAdapter::GetClothCollection();

		TArray<FName> OutWeightMapNames;
		const TArray<FName> SimVerticesAttributeNames = ClothCollection->AttributeNames(UE::Chaos::ClothAsset::FClothCollection::SimVerticesGroup);
		for (const FName& AttributeName : SimVerticesAttributeNames)
		{
			if (ClothCollection->FindAttributeTyped<float>(AttributeName, UE::Chaos::ClothAsset::FClothCollection::SimVerticesGroup))
			{
				OutWeightMapNames.Add(AttributeName);
			}
		}

		return OutWeightMapNames;
	}

	void RemoveClothWeightMaps(UE::Chaos::ClothAsset::FClothAdapter& ClothAdapter, const TArray<FName>& WeightMapNames)
	{
		const TSharedPtr<const UE::Chaos::ClothAsset::FClothCollection> ClothCollection = ClothAdapter.GetClothCollection();
		for (const FName& RemovedMap : WeightMapNames)
		{
			if (ClothCollection->FindAttributeTyped<float>(RemovedMap, UE::Chaos::ClothAsset::FClothCollection::SimVerticesGroup))
			{
				ClothAdapter.RemoveWeightMap(RemovedMap);
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


void UChaosClothAssetEditorMode::UpdateSimulationMeshes()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("ChaosClothAssetEditorApplyChangesTransaction", "Cloth Editor Apply Changes"));

	UChaosClothAsset* ChaosClothAsset = ClothComponent->GetClothAsset();
	ChaosClothAsset->Modify();

	UE::Chaos::ClothAsset::FClothAdapter ClothAdapter(ChaosClothAsset->GetClothCollection());

	check(DynamicMeshSourceInfos.Num() == DynamicMeshComponents.Num());

	// Search for weight map names that are in the original cloth asset, but are not in *all* dynamic mesh components. These are weight maps
	// that have been removed by one of the mesh tools, and so should be removed from the asset.
	const TSet<FName> ClothAssetWeightMapNames = TSet<FName>(ChaosClothAssetEditorModeHelpers::GetClothAssetWeightMapNames(ClothAdapter));
	TSet<FName> CommonDynamicMeshWeightMapNames;
	bool bFirstIteration = true;

	for (int DynamicMeshIndex = 0; DynamicMeshIndex < DynamicMeshComponents.Num(); ++DynamicMeshIndex)
	{
		const int32 LodIndex = DynamicMeshSourceInfos[DynamicMeshIndex].LodIndex;
		const int32 PatternIndex = DynamicMeshSourceInfos[DynamicMeshIndex].PatternIndex;

		UE::Chaos::ClothAsset::FClothLodAdapter ClothLodAdapter = ClothAdapter.GetLod(LodIndex);
		UE::Chaos::ClothAsset::FClothPatternAdapter ClothPatternAdapter = ClothLodAdapter.GetPattern(PatternIndex);

		const UDynamicMeshComponent& DynamicMeshComponent = *DynamicMeshComponents[DynamicMeshIndex];
		const UE::Geometry::FDynamicMesh3* DynamicMesh = DynamicMeshComponent.GetMesh();

		//
		// TODO: Set vertices and triangles on the pattern, e.g.:
		// void Initialize(const TArray<FVector2f>& Positions, const TArray<FVector3f>& RestPositions, const TArray<uint32>& Indices)
		//

		//
		// Weight maps
		//

		if (!DynamicMesh->HasAttributes() || DynamicMesh->Attributes()->NumWeightLayers() == 0)
		{
			continue;
		}
		 
		const TSet<FName> DynamicMeshWeightMapNames(ChaosClothAssetEditorModeHelpers::GetDynamicMeshWeightMapNames(*DynamicMesh));
		if (bFirstIteration)
		{
			CommonDynamicMeshWeightMapNames = DynamicMeshWeightMapNames;
			bFirstIteration = false;
		}
		else
		{
			CommonDynamicMeshWeightMapNames = CommonDynamicMeshWeightMapNames.Intersect(DynamicMeshWeightMapNames);
		}

		const int NumInputWeightElements = DynamicMesh->MaxVertexID();

		for (int32 DynamicMeshWeightMapIndex = 0; DynamicMeshWeightMapIndex < DynamicMesh->Attributes()->NumWeightLayers(); ++DynamicMeshWeightMapIndex)
		{
			const UE::Geometry::FDynamicMeshWeightAttribute* WeightMapAttribute = DynamicMesh->Attributes()->GetWeightLayer(DynamicMeshWeightMapIndex);
			const FName WeightMapName = WeightMapAttribute->GetName();

			ClothAdapter.AddWeightMap(WeightMapName);		// Does nothing if weight map already exists

			TArrayView<float> PatternWeights = ClothPatternAdapter.GetWeightMap(WeightMapName);
			for (int VertexID = 0; VertexID < NumInputWeightElements; ++VertexID)
			{
				float Val;
				WeightMapAttribute->GetValue(VertexID, &Val);
				PatternWeights[VertexID] = Val;
			}
		}
	}

	const TSet<FName> WeightMapsToRemove = ClothAssetWeightMapNames.Difference(CommonDynamicMeshWeightMapNames);
	ChaosClothAssetEditorModeHelpers::RemoveClothWeightMaps(ClothAdapter, WeightMapsToRemove.Array());

	GetToolManager()->EndUndoTransaction();
}

void UChaosClothAssetEditorMode::CreateToolTargets(const TArray<TObjectPtr<UObject>>& AssetsIn) 
{
	// TODO: When we have a cloth component tool target, create it here

}


void UChaosClothAssetEditorMode::ReinitializeDynamicMeshComponents()
{
	// Clean up any existing DynamicMeshComponents
	// Save indices of selected mesh components
	TArray<int32> PreviouslySelectedDynamicMeshComponents;
	const int32 TotalNumberExistingDynamicMeshComponents = DynamicMeshComponents.Num();

	USelection* SelectedComponents = GetModeManager()->GetSelectedComponents();
	for (int32 DynamicMeshComponentIndex = 0; DynamicMeshComponentIndex < DynamicMeshComponents.Num(); ++DynamicMeshComponentIndex)
	{
		// TODO: Check for any outstanding changes on the dynamic mesh component?

		TObjectPtr<UDynamicMeshComponent> DynamicMeshComp = DynamicMeshComponents[DynamicMeshComponentIndex];
		DynamicMeshComp->UnregisterComponent();

		if (SelectedComponents->IsSelected(DynamicMeshComp))
		{
			PreviouslySelectedDynamicMeshComponents.Add(DynamicMeshComponentIndex);
			SelectedComponents->Deselect(DynamicMeshComp);
		}
	}

	for (const TObjectPtr<UMeshElementsVisualizer> Wireframe : WireframesToTick)
	{
		Wireframe->Disconnect();
	}

	DynamicMeshComponents.Empty();
	DynamicMeshComponentParentActors.Empty();
	PropertyObjectsToTick.Empty();	// TODO: We only want to empty the wireframe display properties. Is anything else using this array?
	WireframesToTick.Empty();
	DynamicMeshSourceInfos.Empty();

	if (!ClothComponent)
	{
		return;
	}

	const UChaosClothAsset* ChaosClothAsset = ClothComponent->GetClothAsset();

	if (!ChaosClothAsset)
	{
		return;
	}

	const UE::Chaos::ClothAsset::FClothConstAdapter ClothAdapter(ChaosClothAsset->GetClothCollection());

	for (int32 LodIndex = 0; LodIndex < ClothAdapter.GetNumLods(); ++LodIndex)
	{
		const UE::Chaos::ClothAsset::FClothLodConstAdapter ClothLodAdapter = ClothAdapter.GetLod(LodIndex);

		if (bCombineAllPatterns)
		{
			// TODO: Enable a mode where all patterns are edited as one dynamic mesh component. Will require some fun bookkeeping.
			check(0);
		}
		else
		{
			const int32 NumComponents = ClothLodAdapter.GetNumPatterns();
			DynamicMeshComponents.SetNum(NumComponents);
			DynamicMeshComponentParentActors.SetNum(NumComponents);

			for (int32 PatternIndex = 0; PatternIndex < ClothLodAdapter.GetNumPatterns(); ++PatternIndex)
			{
				UE::Geometry::FDynamicMesh3 PatternMesh;
				FClothPatternToDynamicMesh Converter;
				Converter.Convert(ChaosClothAsset, LodIndex, PatternIndex, bPattern2DMode, PatternMesh);

				// We only need an actor to allow use of HHitProxy for selection
				const FRotator Rotation(0.0f, 0.0f, 0.0f);
				const FActorSpawnParameters SpawnInfo;
				const TObjectPtr<AActor> ParentActor = this->GetWorld()->SpawnActor<AActor>(FVector::ZeroVector, Rotation, SpawnInfo);
				DynamicMeshComponentParentActors[PatternIndex] = ParentActor;

				TObjectPtr<UDynamicMeshComponent> PatternMeshComponent = NewObject<UDynamicMeshComponent>(ParentActor);
				PatternMeshComponent->SetMesh(MoveTemp(PatternMesh));
				PatternMeshComponent->RegisterComponentWithWorld(this->GetWorld());
				DynamicMeshComponents[PatternIndex] = PatternMeshComponent;
				DynamicMeshSourceInfos.Add(FDynamicMeshSourceInfo{ LodIndex, PatternIndex });
			}
		}

		// If we found any patterns, use this LOD
		// TODO: Give the user the ability to specify which LOD to display
		if (ClothLodAdapter.GetNumPatterns() > 0)
		{
			break;
		}
	}

	for (TObjectPtr<UDynamicMeshComponent> RestSpaceMeshComponent : DynamicMeshComponents)
	{
		// Set up the wireframe display of the rest space mesh.
		TObjectPtr<UMeshElementsVisualizer> WireframeDisplay = NewObject<UMeshElementsVisualizer>(this);
		WireframeDisplay->CreateInWorld(GetWorld(), FTransform::Identity);

		WireframeDisplay->Settings->DepthBias = 2.0;
		WireframeDisplay->Settings->bAdjustDepthBiasUsingMeshSize = false;
		WireframeDisplay->Settings->bShowWireframe = true;
		WireframeDisplay->Settings->bShowBorders = true;
		WireframeDisplay->Settings->bShowUVSeams = false;
		WireframeDisplay->Settings->bShowNormalSeams = false;

		// These are not exposed at the visualizer level yet
		// TODO: Should they be?
		WireframeDisplay->WireframeComponent->BoundaryEdgeThickness = 2;

		WireframeDisplay->SetMeshAccessFunction([RestSpaceMeshComponent](UMeshElementsVisualizer::ProcessDynamicMeshFunc ProcessFunc)
		{
			ProcessFunc(*RestSpaceMeshComponent->GetMesh());
		});

		RestSpaceMeshComponent->OnMeshChanged.Add(
			FSimpleMulticastDelegate::FDelegate::CreateLambda([WireframeDisplay]()
		{
			WireframeDisplay->NotifyMeshChanged();
		}));

		// The settings object and wireframe are not part of a tool, so they won't get ticked like they
		// are supposed to (to enable property watching), unless we add this here.
		PropertyObjectsToTick.Add(WireframeDisplay->Settings);
		int32 WireframeIndex = WireframesToTick.Add(WireframeDisplay);

		RestSpaceMeshComponent->MarkRenderStateDirtyEvent.AddLambda( [WireframeIndex, this](UActorComponent& ActorComponent)
		{
			if (WireframeIndex >= WireframesToTick.Num() || WireframeIndex >= DynamicMeshComponents.Num())
			{
				return;
			}

			TObjectPtr<UMeshElementsVisualizer> WireframeDisplay = WireframesToTick[WireframeIndex];
			TObjectPtr<UDynamicMeshComponent> RestSpaceMesh = DynamicMeshComponents[WireframeIndex];

			if (!WireframeDisplay || !RestSpaceMesh)
			{
				return;
			}

			bool bRestSpaceMeshVisible = RestSpaceMesh->GetVisibleFlag();
			WireframeDisplay->SetAllVisible(bRestSpaceMeshVisible);
			WireframeDisplay->Settings->bVisible = bRestSpaceMeshVisible;
		});
	}

	const bool bNumberDynamicMeshesUnchanged = (TotalNumberExistingDynamicMeshComponents == DynamicMeshComponents.Num());
	if (bNumberDynamicMeshesUnchanged && PreviouslySelectedDynamicMeshComponents.Num() > 0)
	{
		SelectedComponents->Modify();
		SelectedComponents->BeginBatchSelectOperation();
		for (int32 DynamicMeshComponentIndex : PreviouslySelectedDynamicMeshComponents)
		{
			if (DynamicMeshComponentIndex < DynamicMeshComponents.Num())
			{
				SelectedComponents->Select(DynamicMeshComponents[DynamicMeshComponentIndex]);
			}
		}
		SelectedComponents->EndBatchSelectOperation();
	}

}

void UChaosClothAssetEditorMode::RefocusRestSpaceViewportClient()
{
	TSharedPtr<FEditorViewportClient, ESPMode::ThreadSafe> PinnedVC = RestSpaceViewportClient.Pin();
	if (PinnedVC.IsValid())
	{
		// This will happen in FocusViewportOnBox anyways; do it now to get a consistent end result
		PinnedVC->ToggleOrbitCamera(false);

		const FBox SceneBounds = SceneBoundingBox();
		if (IsPattern2DModeActive())
		{
			// 2D pattern
			PinnedVC->SetInitialViewTransform(ELevelViewportType::LVT_Perspective, FVector(0, 150, 100), FRotator(-90, 90, 0), DEFAULT_ORTHOZOOM);
		}
		else
		{
			// 3D rest space
			PinnedVC->SetInitialViewTransform(ELevelViewportType::LVT_Perspective, FVector(0, 150, 200), FRotator(0, 0, 0), DEFAULT_ORTHOZOOM);
		}

		constexpr bool bInstant = true;
		PinnedVC->FocusViewportOnBox(SceneBounds, bInstant);
	}
}

void UChaosClothAssetEditorMode::InitializeTargets(const TArray<TObjectPtr<UObject>>& AssetsIn)
{
	// InitializeContexts needs to have been called first so that we have the 3d preview world ready.
	check(PreviewWorld);

	OriginalObjectsToEdit = AssetsIn;

	// TODO: If we have multiple objects to initialize, position them relative to each other somehow
	Transforms.Init(FTransform::Identity, AssetsIn.Num());

	CreateToolTargets(AssetsIn);

	// NOTE: This ensure will fire until we are able to build a tool target associated with ClothComponent
	//if (!ensure(AssetsIn.Num() == ToolTargets.Num()))
	//{
	//	return;
	//}

	for (TObjectPtr<UObject> Asset : AssetsIn)
	{
		if (UChaosClothAsset* ChaosClothAsset = Cast<UChaosClothAsset>(Asset))
		{
			ClothComponent = NewObject<UChaosClothComponent>();
			ClothComponent->SetClothAsset(ChaosClothAsset);
			ClothComponent->RegisterComponentWithWorld(PreviewWorld);

			break;
		}
	}

	ReinitializeDynamicMeshComponents();
	RefocusRestSpaceViewportClient();

	// Prep things for layer/channel selection
	AssetNames.Reset(ToolTargets.Num());
	for (const TObjectPtr<UToolTarget>& ToolTarget : ToolTargets)
	{
		AssetNames.Add(UE::ToolTarget::GetHumanReadableName(ToolTarget));
	}
}

bool UChaosClothAssetEditorMode::HaveUnappliedChanges()
{
	return ModifiedAssetIDs.Num() > 0;
}

void UChaosClothAssetEditorMode::GetAssetsWithUnappliedChanges(TArray<TObjectPtr<UObject>> UnappliedAssetsOut)
{
	for (int32 AssetID : ModifiedAssetIDs)
	{
		// The asset ID corresponds to the index into OriginalObjectsToEdit
		UnappliedAssetsOut.Add(OriginalObjectsToEdit[AssetID]);
	}
}

void UChaosClothAssetEditorMode::ApplyChanges()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("ChaosClothAssetEditorApplyChangesTransaction", "Cloth Editor Apply Changes"));
	GetToolManager()->EndUndoTransaction();
}

void UChaosClothAssetEditorMode::ModeTick(float DeltaTime)
{
	Super::ModeTick(DeltaTime);
	
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

	for (TWeakObjectPtr<UMeshElementsVisualizer> WireframeDisplay : WireframesToTick)
	{
		if (WireframeDisplay.IsValid())
		{
			WireframeDisplay->OnTick(DeltaTime);
		}
	}

	if (PreviewWorld)
	{
		PreviewWorld->Tick(ELevelTick::LEVELTICK_All, DeltaTime);
	}
}


FBox UChaosClothAssetEditorMode::SceneBoundingBox() const
{
	FBoxSphereBounds TotalBounds(ForceInitToZero);
	
	bool bFoundMeshBounds = false;
	for (const TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent : DynamicMeshComponents)
	{
		const FBoxSphereBounds& CurrentBounds = DynamicMeshComponent->Bounds;

		if (!bFoundMeshBounds)
		{
			TotalBounds = CurrentBounds;
			bFoundMeshBounds = true;
		}
		else
		{
			TotalBounds = TotalBounds + CurrentBounds;
		}
	}

	return TotalBounds.GetBox();
}

FBox UChaosClothAssetEditorMode::SelectionBoundingBox() const
{
	FBoxSphereBounds TotalBounds;

	const USelection* const SelectedComponents = GetModeManager()->GetSelectedComponents();

	bool bFoundSelected = false;
	for (const TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent : DynamicMeshComponents)
	{
		if (SelectedComponents->IsSelected(DynamicMeshComponent))
		{
			const FBoxSphereBounds& CurrentBounds = DynamicMeshComponent->Bounds;

			if (!bFoundSelected)
			{
				TotalBounds = CurrentBounds;
				bFoundSelected = true;
			}
			else
			{
				TotalBounds = TotalBounds + CurrentBounds;
			}
			
		}
	}

	if (bFoundSelected)
	{
		return TotalBounds.GetBox();
	}
	else
	{
		// Nothing selected, return the whole scene
		return SceneBoundingBox();
	}

}


FBox UChaosClothAssetEditorMode::PreviewBoundingBox() const
{
	FBoxSphereBounds TotalBounds(ForceInit);

	bool bAnyValid = false;

	if (ClothComponent && ClothComponent->GetClothAsset())
	{
		const UChaosClothAsset* ChaosClothAsset = ClothComponent->GetClothAsset();
		const UE::Chaos::ClothAsset::FClothConstAdapter ClothAdapter(ChaosClothAsset->GetClothCollection());

		for (int32 LodIndex = 0; LodIndex < ClothAdapter.GetNumLods(); ++LodIndex)
		{
			const UE::Chaos::ClothAsset::FClothLodConstAdapter ClothLodAdapter = ClothAdapter.GetLod(LodIndex);

			for (int32 PatternIndex = 0; PatternIndex < ClothLodAdapter.GetNumPatterns(); ++PatternIndex)
			{
				const UE::Chaos::ClothAsset::FClothPatternConstAdapter ClothPatternAdapter = ClothLodAdapter.GetPattern(PatternIndex);
				FBoxSphereBounds BoxSphere = ChaosClothAssetEditorModeLocals::RenderMeshBounds(ClothPatternAdapter);
				TotalBounds = TotalBounds + BoxSphere;
				bAnyValid = true;
			}
		}
	}

	if (bAnyValid)
	{
		return TotalBounds.GetBox();
	}
	else
	{
		return FBox(ForceInitToZero);
	}
}

bool UChaosClothAssetEditorMode::IsPattern2DModeActive() const
{
	return bPattern2DMode;
}

void UChaosClothAssetEditorMode::TogglePatternMode()
{
	bPattern2DMode = !bPattern2DMode;
	ReinitializeDynamicMeshComponents();

	TSharedPtr<FChaosClothEditorRestSpaceViewportClient> VC = RestSpaceViewportClient.Pin();
	if (VC.IsValid())
	{
		VC->Set2DMode(bPattern2DMode);
	}

	RefocusRestSpaceViewportClient();
}

bool UChaosClothAssetEditorMode::CanTogglePatternMode() const
{
	return bCanTogglePattern2DMode;
}


void UChaosClothAssetEditorMode::SetRestSpaceViewportClient(TWeakPtr<FChaosClothEditorRestSpaceViewportClient, ESPMode::ThreadSafe> InViewportClient)
{
	RestSpaceViewportClient = InViewportClient;

	TSharedPtr<FChaosClothEditorRestSpaceViewportClient> VC = RestSpaceViewportClient.Pin();
	if (VC.IsValid())
	{
		VC->Set2DMode(bPattern2DMode);
		VC->SetToolCommandList(ToolCommandList);
	}
}

#undef LOCTEXT_NAMESPACE

