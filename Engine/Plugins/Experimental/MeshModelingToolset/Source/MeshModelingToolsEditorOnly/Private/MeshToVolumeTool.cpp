// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshToVolumeTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "MeshRegionBoundaryLoops.h"
#include "Util/ColorConstants.h"
#include "ToolSetupUtil.h"
#include "Selections/MeshConnectedComponents.h"
#include "Selection/ToolSelectionUtil.h"

#include "Engine/Classes/Engine/BlockingVolume.h"
#include "Engine/Classes/Components/BrushComponent.h"
#include "Engine/Classes/Engine/Polys.h"
#include "Model.h"
#include "BSPOps.h"		// in UnrealEd

#define LOCTEXT_NAMESPACE "UMeshToVolumeTool"


/*
 * ToolBuilder
 */


bool UMeshToVolumeToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	// We don't want to allow this tool to run on selected volumes
	return ToolBuilderUtil::CountSelectedActorsOfType<AVolume>(SceneState) == 0 
		&& ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UMeshToVolumeToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMeshToVolumeTool* NewTool = NewObject<UMeshToVolumeTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	UPrimitiveComponent* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));

	return NewTool;
}

/*
 * Tool
 */
UMeshToVolumeTool::UMeshToVolumeTool()
{
	SetToolDisplayName(LOCTEXT("MeshToVolumeToolName", "Mesh To Volume"));
}


void UMeshToVolumeTool::Setup()
{
	UInteractiveTool::Setup();

	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->bBuildSpatialDataStructure = false;
	PreviewMesh->CreateInWorld(ComponentTarget->GetOwnerActor()->GetWorld(), FTransform::Identity);
	PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	PreviewMesh->SetMaterials(MaterialSet.Materials);

	PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::ExternallyCalculated);
	PreviewMesh->InitializeMesh(ComponentTarget->GetMesh());

	InputMesh.Copy(*PreviewMesh->GetMesh());

	VolumeEdgesSet = NewObject<ULineSetComponent>(PreviewMesh->GetRootComponent());
	VolumeEdgesSet->SetupAttachment(PreviewMesh->GetRootComponent());
	VolumeEdgesSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager()));
	VolumeEdgesSet->RegisterComponent();

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	Settings = NewObject<UMeshToVolumeToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	Settings->WatchProperty(Settings->ConversionMode,
							[this](EMeshToVolumeMode NewMode)
							{ bVolumeValid = false; });


	HandleSourcesProperties = NewObject<UOnAcceptHandleSourcesProperties>(this);
	HandleSourcesProperties->RestoreProperties(this);
	AddToolPropertySource(HandleSourcesProperties);

	bVolumeValid = false;
	

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Convert a Static Mesh to a Volume, or update an existing Volume"),
		EToolMessageLevel::UserNotification);
}

void UMeshToVolumeTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	HandleSourcesProperties->SaveProperties(this);

	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	ComponentTarget->SetOwnerVisibility(true);


	UWorld* TargetWorld = ComponentTarget->GetOwnerActor()->GetWorld();


	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshToVolumeToolTransactionName", "Create Volume"));

		FTransform TargetTransform = ComponentTarget->GetWorldTransform();

		AVolume* TargetVolume = nullptr;

		FTransform SetTransform = TargetTransform;
		if (Settings->TargetVolume.IsValid() == false)
		{
			FRotator Rotation(0.0f, 0.0f, 0.0f);
			FActorSpawnParameters SpawnInfo;
			FTransform NewActorTransform = FTransform::Identity;
			UClass* VolumeClass = Settings->NewVolumeType.Get();
			if (VolumeClass)
			{
				TargetVolume = (AVolume*)TargetWorld->SpawnActor(VolumeClass, &NewActorTransform, SpawnInfo);
			}
			else
			{
				TargetVolume = TargetWorld->SpawnActor<ABlockingVolume>(FVector::ZeroVector, Rotation, SpawnInfo);
			}
			TargetVolume->BrushType = EBrushType::Brush_Add;
			UModel* Model = NewObject<UModel>(TargetVolume);
			TargetVolume->Brush = Model;
			TargetVolume->GetBrushComponent()->Brush = TargetVolume->Brush;
		}
		else
		{
			TargetVolume = Settings->TargetVolume.Get();
			SetTransform = TargetVolume->GetActorTransform();
			TargetVolume->Modify();
			TargetVolume->GetBrushComponent()->Modify();
		}

		UE::Conversion::DynamicMeshToVolume(InputMesh, Faces, TargetVolume);
		TargetVolume->SetActorTransform(SetTransform);
		TargetVolume->PostEditChange();

		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), TargetVolume);

		TArray<AActor*> Actors;
		Actors.Add(ComponentTarget->GetOwnerActor());
		HandleSourcesProperties->ApplyMethod(Actors, GetToolManager());


		GetToolManager()->EndUndoTransaction();

	}
}

void UMeshToVolumeTool::OnTick(float DeltaTime)
{
	if (bVolumeValid == false)
	{
		RecalculateVolume();
	}
}

void UMeshToVolumeTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}


void UMeshToVolumeTool::UpdateLineSet()
{
	FColor BoundaryEdgeColor(240, 15, 15);
	float BoundaryEdgeThickness = 0.5;
	float BoundaryEdgeDepthBias = 2.0f;

	VolumeEdgesSet->Clear();
	for (const UE::Conversion::FDynamicMeshFace& Face : Faces)
	{
		int32 NumV = Face.BoundaryLoop.Num();
		for (int32 k = 0; k < NumV; ++k)
		{
			VolumeEdgesSet->AddLine(
				(FVector)Face.BoundaryLoop[k], (FVector)Face.BoundaryLoop[(k+1)%NumV],
				BoundaryEdgeColor, BoundaryEdgeThickness, BoundaryEdgeDepthBias);
		}
	}

}

void UMeshToVolumeTool::RecalculateVolume()
{
	if (Settings->ConversionMode == EMeshToVolumeMode::MinimalPolygons)
	{
		UE::Conversion::GetPolygonFaces(InputMesh, Faces);
	}
	else
	{
		UE::Conversion::GetTriangleFaces(InputMesh, Faces);
	}

	UpdateLineSet();
	bVolumeValid = true;
}


#undef LOCTEXT_NAMESPACE
