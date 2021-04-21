// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorMode.h"

#include "Algo/AnyOf.h"
#include "Drawing/MeshElementsVisualizer.h"
#include "EdModeInteractiveToolsContext.h" //ToolsContext
#include "InteractiveTool.h"
#include "TargetInterfaces/UVUnwrapDynamicMesh.h"
#include "ToolTargetManager.h"
#include "ToolTargets/StaticMeshUVMeshToolTarget.h"
#include "ToolTargets/ToolTarget.h"
#include "PreviewMesh.h"
#include "UVEditorCommands.h"
#include "UVSelectTool.h"
#include "UVEditorModeToolkit.h"
#include "UVEditorSubsystem.h"
#include "ToolSetupUtil.h"
#include "UVToolStateObjects.h"

#define LOCTEXT_NAMESPACE "UUVEditorMode"

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

	StateObjectStore = NewObject<UUVToolStateObjectStore>(this);

	RegisterTools();

	ActivateDefaultTool();

	// Add new target factories here and in UUVEditorSubsystem::Initialize() as they are developed.
	ToolsContext->TargetManager->AddTargetFactory(NewObject<UStaticMeshUVMeshToolTargetFactory>(ToolsContext->TargetManager));
}

void UUVEditorMode::RegisterTools()
{
	const FUVEditorCommands& CommandInfos = FUVEditorCommands::Get();

	// TODO: Other tool registrations go here
	auto UVSelectToolBuilder = NewObject<UUVSelectToolBuilder>();
	UVSelectToolBuilder->DisplayedMeshes = &DisplayedMeshes;
	UVSelectToolBuilder->StateObjectStore = GetStateObjectStore();
	RegisterTool(CommandInfos.BeginSelectTool, TEXT("UVSelectTool"), UVSelectToolBuilder);

	auto UVTransformToolBuilder = NewObject<UUVSelectToolBuilder>();
	UVTransformToolBuilder->DisplayedMeshes = &DisplayedMeshes;
	UVTransformToolBuilder->StateObjectStore = GetStateObjectStore();
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
	StateObjectStore->Clear();
	StateObjectStore = nullptr;

	for (TObjectPtr<UPreviewMesh>& Preview : DisplayedMeshes)
	{
		Preview->Disconnect();
	}
	for (TObjectPtr<UMeshElementsVisualizer>& Wireframe : DisplayedWireframes)
	{
		Wireframe->Disconnect();
	}

	Super::Exit();
}

void UUVEditorMode::InitializeTargets(TArray<TObjectPtr<UObject>>& AssetsIn)
{
	using namespace UVEditorModeLocals;

	OriginalObjectsToEdit = AssetsIn;

	for (const TObjectPtr<UObject>& Object : AssetsIn)
	{
		UToolTarget* ToolTarget = ToolsContext->TargetManager->BuildTarget(Object, UUVEditorSubsystem::UVUnwrapMeshTargetRequirements);
		IUVUnwrapDynamicMesh* UVMesh = Cast<IUVUnwrapDynamicMesh>(ToolTarget);
		check(UVMesh);
		UVUnwrapMeshTargets.Add(ToolTarget);

		// Create in-viewport representations of the unwrapped mesh
		UPreviewMesh* UVPreview = NewObject<UPreviewMesh>();
		UVPreview->CreateInWorld(GetWorld(), FTransform::Identity);
		UVPreview->UpdatePreview(UVMesh->GetMesh(UVLayerIndex).Get());
		
		// We use a two-sided material because the triangles of our unwrapped meshes may have different orientations
		UVPreview->SetMaterial(0, ToolSetupUtil::GetCustomTwoSidedDepthOffsetMaterial(
			GetToolManager(),
			(FLinearColor)TriangleColor,
			0, //depth offset
			0.3)); //opacity

		UVPreview->SetVisible(true);
		DisplayedMeshes.Add(UVPreview);

		// Set up the wireframe display of the unwrapped mesh.
		UMeshElementsVisualizer* WireframeDisplay = NewObject<UMeshElementsVisualizer>(this);
		WireframeDisplay->CreateInWorld(GetWorld(), FTransform::Identity);
		check(WireframeDisplay->Settings);

		WireframeDisplay->Settings->DepthBias = 1;
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

		// The settings object is not part of a tool, so it won't get ticked like its
		// supposed to (to enable property watching), unless we add this here.
		PropertyObjectsToTick.Add(WireframeDisplay->Settings);

		// The wireframe will track the preview mesh
		WireframeDisplay->SetMeshAccessFunction([UVPreview](void) { return UVPreview->GetMesh(); });
		UVPreview->GetOnMeshChanged().AddWeakLambda(this, [WireframeDisplay]() 
		{
			WireframeDisplay->NotifyMeshChanged();
		});
		DisplayedWireframes.Add(WireframeDisplay);

		// Initialize our timestamp so we can later detect changes
		MeshChangeStamps.Add(UVPreview->GetMesh()->GetShapeTimestamp());
	}
}

bool UUVEditorMode::HaveUnappliedChanges()
{
	for (int32 i = 0; i < DisplayedMeshes.Num(); ++i)
	{
		if (DisplayedMeshes[i]->GetMesh()->GetShapeTimestamp() != MeshChangeStamps[i])
		{
			return true;
		}
	}
	return false;
}

void UUVEditorMode::GetAssetsWithUnappliedChanges(TArray<TObjectPtr<UObject>> UnappliedAssetsOut)
{
	for (int32 i = 0; i < DisplayedMeshes.Num(); ++i)
	{
		if (DisplayedMeshes[i]->GetMesh()->GetShapeTimestamp() != MeshChangeStamps[i])
		{
			UnappliedAssetsOut.Add(OriginalObjectsToEdit[i]);
		}
	}
}

void UUVEditorMode::ApplyChanges()
{
	using namespace UVEditorModeLocals;

	for (int32 i = 0; i < DisplayedMeshes.Num(); ++i)
	{
		if (DisplayedMeshes[i]->GetMesh()->GetShapeTimestamp() != MeshChangeStamps[i])
		{
			UVUnwrapMeshTargets[i]->SaveBackToUVs(DisplayedMeshes[i]->GetMesh(), UVLayerIndex);
			MeshChangeStamps[i] = DisplayedMeshes[i]->GetMesh()->GetShapeTimestamp();
		}
	}
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

	for (UMeshElementsVisualizer* WireframeDisplay : DisplayedWireframes)
	{
		WireframeDisplay->OnTick(DeltaTime);
	}
}

#undef LOCTEXT_NAMESPACE
