// Copyright Epic Games, Inc. All Rights Reserved.

#include "VolumeToMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ConversionUtils/VolumeToDynamicMesh.h"
#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "Util/ColorConstants.h"
#include "ToolSetupUtil.h"
#include "Selection/ToolSelectionUtil.h"
#include "AssetGenerationUtil.h"

#include "Operations/MergeCoincidentMeshEdges.h"
#include "CompGeom/PolygonTriangulation.h"
#include "MeshBoundaryLoops.h"
#include "Operations/MinimalHoleFiller.h"

#include "Model.h"

#define LOCTEXT_NAMESPACE "UVolumeToMeshTool"


/*
 * ToolBuilder
 */

bool UVolumeToMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountSelectedActorsOfType<AVolume>(SceneState) == 1;
}

UInteractiveTool* UVolumeToMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UVolumeToMeshTool* NewTool = NewObject<UVolumeToMeshTool>(SceneState.ToolManager);

	NewTool->SetWorld(SceneState.World);
	check(AssetAPI);
	NewTool->SetAssetAPI(AssetAPI);

	AVolume* Volume = ToolBuilderUtil::FindFirstActorOfType<AVolume>(SceneState);
	check(Volume != nullptr);
	NewTool->SetSelection(Volume);

	return NewTool;
}




/*
 * Tool
 */
UVolumeToMeshTool::UVolumeToMeshTool()
{
	SetToolDisplayName(LOCTEXT("VolumeToMeshToolName", "Volume to Mesh"));
}


void UVolumeToMeshTool::SetSelection(AVolume* Volume)
{
	TargetVolume = Volume;
}


void UVolumeToMeshTool::Setup()
{
	UInteractiveTool::Setup();

	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->bBuildSpatialDataStructure = false;
	PreviewMesh->CreateInWorld(TargetVolume->GetWorld(), FTransform::Identity);
	PreviewMesh->SetTransform(TargetVolume->GetActorTransform());

	PreviewMesh->SetMaterial( ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager()) );

	PreviewMesh->SetOverrideRenderMaterial(ToolSetupUtil::GetSelectionMaterial(GetToolManager()));
	PreviewMesh->SetTriangleColorFunction([this](const FDynamicMesh3* Mesh, int TriangleID)
	{
		return LinearColors::SelectFColor(Mesh->GetTriangleGroup(TriangleID));
	});

	VolumeEdgesSet = NewObject<ULineSetComponent>(PreviewMesh->GetRootComponent());
	VolumeEdgesSet->SetupAttachment(PreviewMesh->GetRootComponent());
	VolumeEdgesSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager()));
	VolumeEdgesSet->RegisterComponent();


	Settings = NewObject<UVolumeToMeshToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	Settings->WatchProperty(Settings->bWeldEdges, [this](bool) { bResultValid = false; });
	Settings->WatchProperty(Settings->bAutoRepair, [this](bool) { bResultValid = false; });
	Settings->WatchProperty(Settings->bOptimizeMesh, [this](bool) { bResultValid = false; });
	Settings->WatchProperty(Settings->bShowWireframe, [this](bool) { bResultValid = false; });

	bResultValid = false;

	GetToolManager()->DisplayMessage( 
		LOCTEXT("OnStartTool", "Convert a Volume to a Static Mesh"),
		EToolMessageLevel::UserNotification);
}



void UVolumeToMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);

	FTransform3d Transform(PreviewMesh->GetTransform());
	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	if (ShutdownType == EToolShutdownType::Accept )
	{
		UMaterialInterface* UseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);

		FString NewName = TargetVolume.IsValid() ?
			FString::Printf(TEXT("%sMesh"), *TargetVolume->GetName()) : TEXT("Volume Mesh");

		GetToolManager()->BeginUndoTransaction(LOCTEXT("CreateMeshVolume", "Volume To Mesh"));

		AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
			AssetAPI, TargetWorld,
			&CurrentMesh, Transform, NewName, UseMaterial);
		if (NewActor != nullptr)
		{
			ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
		}

		GetToolManager()->EndUndoTransaction();
	}


}


void UVolumeToMeshTool::OnTick(float DeltaTime)
{
	if (bResultValid == false)
	{
		RecalculateMesh();
	}
}

void UVolumeToMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}


bool UVolumeToMeshTool::HasAccept() const
{
	return true;
}

bool UVolumeToMeshTool::CanAccept() const
{
	return true;
}



void UVolumeToMeshTool::UpdateLineSet()
{
	VolumeEdgesSet->Clear();

	FColor BoundaryEdgeColor = LinearColors::VideoRed3b();
	float BoundaryEdgeThickness = 1.0;
	float BoundaryEdgeDepthBias = 2.0f;

	FColor WireEdgeColor = LinearColors::Gray3b();
	float WireEdgeThickness = 0.1;
	float WireEdgeDepthBias = 1.0f;

	if (Settings->bShowWireframe)
	{
		VolumeEdgesSet->ReserveLines(CurrentMesh.EdgeCount());

		for (int32 eid : CurrentMesh.EdgeIndicesItr())
		{
			FVector3d A, B;
			CurrentMesh.GetEdgeV(eid, A, B);
			if (CurrentMesh.IsBoundaryEdge(eid))
			{
				VolumeEdgesSet->AddLine((FVector)A, (FVector)B,
					BoundaryEdgeColor, BoundaryEdgeThickness, BoundaryEdgeDepthBias);
			}
			else
			{
				VolumeEdgesSet->AddLine((FVector)A, (FVector)B,
					WireEdgeColor, WireEdgeThickness, WireEdgeDepthBias);
			}
		}
	}
}

void UVolumeToMeshTool::RecalculateMesh()
{
	if (TargetVolume.IsValid())
	{
		UE::Conversion::FVolumeToMeshOptions Options;
		Options.bMergeVertices = Settings->bWeldEdges;
		Options.bAutoRepairMesh = Settings->bAutoRepair;
		Options.bOptimizeMesh = Settings->bOptimizeMesh;

		CurrentMesh = FDynamicMesh3(EMeshComponents::FaceGroups);
		UE::Conversion::VolumeToDynamicMesh(TargetVolume.Get(), CurrentMesh, Options);
		FMeshNormals::InitializeMeshToPerTriangleNormals(&CurrentMesh);
		PreviewMesh->UpdatePreview(&CurrentMesh);
	}

	UpdateLineSet();

	bResultValid = true;
}




#undef LOCTEXT_NAMESPACE
