// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorMode.h"

#include "Algo/AnyOf.h"
#include "ContextObjectStore.h"
#include "Drawing/MeshElementsVisualizer.h"
#include "EdModeInteractiveToolsContext.h" //ToolsContext
#include "InteractiveTool.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "PreviewMesh.h"
#include "TargetInterfaces/AssetBackedTarget.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/DynamicMeshProvider.h"
#include "TargetInterfaces/DynamicMeshCommitter.h"
#include "ToolSetupUtil.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "ToolTargetManager.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "UVEditorCommands.h"
#include "UVSelectTool.h"
#include "UVEditorModeToolkit.h"
#include "UVEditorSubsystem.h"
#include "UVEditorToolUtil.h"
#include "UVToolContextObjects.h"

#define LOCTEXT_NAMESPACE "UUVEditorMode"

using namespace UE::Geometry;

const FEditorModeID UUVEditorMode::EM_UVEditorModeId = TEXT("EM_UVEditorMode");

namespace UVEditorModeLocals
{
	// TODO: This is temporary. We need to be able to configure the layer we look at,
	// and ideally show multiple layers at a time.
	const int32 UVLayerIndex = 0;

	FColor TriangleColor = FColor(50, 194, 219);
	FColor WireframeColor = FColor(50, 100, 219);
	FColor IslandBorderColor = FColor(103, 52, 235);
}

const FToolTargetTypeRequirements& UUVEditorMode::GetToolTargetRequirements()
{
	static const FToolTargetTypeRequirements ToolTargetRequirements =
		FToolTargetTypeRequirements({
			UMaterialProvider::StaticClass(),
			UDynamicMeshCommitter::StaticClass(),
			UDynamicMeshProvider::StaticClass(),
			UAssetBackedTarget::StaticClass()
			});
	return ToolTargetRequirements;
}

UUVEditorMode::UUVEditorMode()
{
	Info = FEditorModeInfo(
		EM_UVEditorModeId,
		LOCTEXT("UVEditorModeName", "UV"),
		FSlateIcon(),
		false);
}

void UUVEditorMode::Enter()
{
	Super::Enter();

	RegisterTools();

	ActivateDefaultTool();
}

void UUVEditorMode::RegisterTools()
{
	const FUVEditorCommands& CommandInfos = FUVEditorCommands::Get();

	// TODO: Other tool registrations go here
	auto UVSelectToolBuilder = NewObject<UUVSelectToolBuilder>();
	UVSelectToolBuilder->Targets = &ToolInputObjects;
	RegisterTool(CommandInfos.BeginSelectTool, TEXT("UVSelectTool"), UVSelectToolBuilder);

	auto UVTransformToolBuilder = NewObject<UUVSelectToolBuilder>();
	UVTransformToolBuilder->Targets = &ToolInputObjects;
	UVTransformToolBuilder->bGizmoEnabled = true;
	RegisterTool(CommandInfos.BeginTransformTool, TEXT("UVTransformTool"), UVTransformToolBuilder);
}

void UUVEditorMode::CreateToolkit()
{
	Toolkit = MakeShared<FUVEditorModeToolkit>();
}

void UUVEditorMode::ActivateDefaultTool()
{
	ToolsContext->StartTool(TEXT("UVSelectTool"));
}


void UUVEditorMode::BindCommands()
{
	// We currently don't have mode-level commands (rather than tool level or asset
	// editor level), but presumably they would go here if we had them.
}

void UUVEditorMode::Exit()
{
	for (TObjectPtr<UUVEditorToolMeshInput> ToolInput : ToolInputObjects)
	{
		ToolInput->Shutdown();
	}
	ToolInputObjects.Reset();

	Super::Exit();
}

void UUVEditorMode::InitializeTargets(TArray<TObjectPtr<UObject>>& AssetsIn, TArray<FTransform>* TransformsIn)
{
	using namespace UVEditorModeLocals;

	OriginalObjectsToEdit = AssetsIn;

	// Build the tool targets that provide us with 3d dynamic meshes
	UUVEditorSubsystem* UVSubsystem = GEditor->GetEditorSubsystem<UUVEditorSubsystem>();
	UVSubsystem->BuildTargets(AssetsIn, GetToolTargetRequirements(), ToolTargets);

	// Get an api for manipulating things in the live preview, which has its own world and input router
	UContextObjectStore* ContextStore = GetInteractiveToolsContext()->ToolManager->GetContextObjectStore();
	UUVToolLivePreviewAPI* LivePreviewAPI = ContextStore->FindContext<UUVToolLivePreviewAPI>();
	check(LivePreviewAPI);
	UWorld* LivePreviewWorld = LivePreviewAPI->GetLivePreviewWorld();

	double ScaleFactor = GetUVMeshScalingFactor();

	// These functions will determine the mapping between UV values and the resulting mesh vertex positions. 
	// If we're looking down on the unwrapped mesh, with the Z axis towards us, we want U's to be right, and
	// V's to be up. In Unreal's left-handed coordinate system, this means that we map U's to world Y
	// and V's to world X.
	// Also, Unreal changes the V coordinates of imported meshes to 1-V internally, and we undo this
	// while displaying the UV's because the users likely expect to see the original UV's (it would
	// be particularly confusing for users working with UDIM assets, where internally stored V's 
	// frequently end up negative).
	// The ScaleFactor just scales the mesh up. Scaling the mesh up makes it easier to zoom in
	// further into the display before getting issues with the camera near plane distance.
	auto UVToVertPosition = [this, ScaleFactor](const FVector2f& UV)
	{
		return FVector3d((1 - UV.Y) * ScaleFactor, UV.X * ScaleFactor, 0);
	};
	auto VertPositionToUV = [this, ScaleFactor](const FVector3d& VertPosition)
	{
		return FVector2D(VertPosition.Y / ScaleFactor, 1 - (VertPosition.X / ScaleFactor));
	};

	// For each of our tool targets, we're going to create a UV tool input object that bundles together
	// the 3d preview and the unwrapped UV layer, with both a reference and a "preview with background op"
	// versions of both
	for (UToolTarget* Target : ToolTargets)
	{
		UUVEditorToolMeshInput* ToolInputObject = NewObject<UUVEditorToolMeshInput>();
		
		if (!ToolInputObject->InitializeMeshes(Target, UVLayerIndex,
			GetWorld(), LivePreviewWorld, ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()),
			UVToVertPosition, VertPositionToUV))
		{
			continue;
		}

		ToolInputObject->UnwrapPreview->PreviewMesh->SetMaterial(
			0, ToolSetupUtil::GetCustomTwoSidedDepthOffsetMaterial(
				GetToolManager(),
				(FLinearColor)TriangleColor,
				0)); //depth offset

		// Initialize our timestamp so we can later detect changes
		MeshChangeStamps.Add(ToolInputObject->UnwrapCanonical->GetShapeTimestamp());

		// Set up the wireframe display of the unwrapped mesh.
		UMeshElementsVisualizer* WireframeDisplay = NewObject<UMeshElementsVisualizer>(this);
		WireframeDisplay->CreateInWorld(GetWorld(), FTransform::Identity);

		WireframeDisplay->Settings->DepthBias = 0.5;
		WireframeDisplay->Settings->bAdjustDepthBiasUsingMeshSize = false;
		WireframeDisplay->Settings->bShowWireframe = true;
		WireframeDisplay->Settings->bShowBorders = true;
		WireframeDisplay->Settings->WireframeColor = WireframeColor;
		WireframeDisplay->Settings->BoundaryEdgeColor = IslandBorderColor;
		WireframeDisplay->Settings->bShowUVSeams = false;
		WireframeDisplay->Settings->bShowNormalSeams = false;
		// These are not exposed at the visualizer level yet
		// TODO: Should they be?
		WireframeDisplay->WireframeComponent->BoundaryEdgeThickness = 2;

		// The wireframe will track the unwrap preview mesh
		WireframeDisplay->SetMeshAccessFunction([ToolInputObject](void) { 
			return ToolInputObject->UnwrapPreview->PreviewMesh->GetMesh(); 
			});

		// The settings object and wireframe are not part of a tool, so they won't get ticked like they
		// are supposed to (to enable property watching), unless we add this here.
		PropertyObjectsToTick.Add(WireframeDisplay->Settings);
		WireframesToTick.Add(WireframeDisplay);

		// The tool input object will hold on to the wireframe for the purposes of updating it and cleaning it up
		ToolInputObject->WireframeDisplay = WireframeDisplay;

		ToolInputObjects.Add(ToolInputObject);
	}
	
	if (TransformsIn && TransformsIn->Num() == AssetsIn.Num())
	{
		for (int i = 0; i < ToolInputObjects.Num(); ++i)
		{
			ToolInputObjects[i]->AppliedPreview->PreviewMesh->SetTransform((*TransformsIn)[i]);
		}
	}
}

void UUVEditorMode::EmitToolIndependentObjectChange(UObject* TargetObject, TUniquePtr<FToolCommandChange> Change, const FText& Description)
{
	GetInteractiveToolsContext()->GetTransactionAPI()->AppendChange(TargetObject, MoveTemp(Change), Description);
}

bool UUVEditorMode::HaveUnappliedChanges()
{
	for (int32 i = 0; i < ToolInputObjects.Num(); ++i)
	{
		if (ToolInputObjects[i]->UnwrapCanonical->GetShapeTimestamp() != MeshChangeStamps[i])
		{
			return true;
		}
	}
	return false;
}

void UUVEditorMode::GetAssetsWithUnappliedChanges(TArray<TObjectPtr<UObject>> UnappliedAssetsOut)
{
	for (int32 i = 0; i < ToolInputObjects.Num(); ++i)
	{
		if (ToolInputObjects[i]->UnwrapCanonical->GetShapeTimestamp() != MeshChangeStamps[i])
		{
			UnappliedAssetsOut.Add(OriginalObjectsToEdit[i]);
		}
	}
}

void UUVEditorMode::ApplyChanges()
{
	using namespace UVEditorModeLocals;

	GetToolManager()->BeginUndoTransaction(LOCTEXT("UVEditorApplyChangesTransaction", "UV Editor Apply Changes"));

	IDynamicMeshCommitter::FDynamicMeshCommitInfo CommitInfo(false);
	CommitInfo.bUVsChanged = true;

	for (int32 i = 0; i < ToolInputObjects.Num(); ++i)
	{
		if (ToolInputObjects[i]->UnwrapCanonical->GetShapeTimestamp() != MeshChangeStamps[i])
		{
			Cast<IDynamicMeshCommitter>(ToolTargets[i])->CommitDynamicMesh(*ToolInputObjects[i]->AppliedCanonical, CommitInfo);
			MeshChangeStamps[i] = ToolInputObjects[i]->UnwrapCanonical->GetShapeTimestamp();
		}
	}

	GetToolManager()->EndUndoTransaction();
}

void UUVEditorMode::ModeTick(float DeltaTime)
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
}

#undef LOCTEXT_NAMESPACE
