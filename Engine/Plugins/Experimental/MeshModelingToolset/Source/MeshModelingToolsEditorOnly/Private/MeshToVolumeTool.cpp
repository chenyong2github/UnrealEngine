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
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"

#include "Engine/Classes/Engine/BlockingVolume.h"
#include "Engine/Classes/Components/BrushComponent.h"
#include "Engine/Classes/Engine/Polys.h"
#include "Model.h"
#include "BSPOps.h"		// in UnrealEd

#define LOCTEXT_NAMESPACE "UMeshToVolumeTool"


/*
 * ToolBuilder
 */

const FToolTargetTypeRequirements& UMeshToVolumeToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements(nullptr, TArray<const UClass*>({
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass() })
		);
	return TypeRequirements;
}

bool UMeshToVolumeToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	// We don't want to allow this tool to run on selected volumes
	return ToolBuilderUtil::CountSelectedActorsOfType<AVolume>(SceneState) == 0 
		&& SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 1;
}

UInteractiveTool* UMeshToVolumeToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMeshToVolumeTool* NewTool = NewObject<UMeshToVolumeTool>(SceneState.ToolManager);

	UToolTarget* Target = SceneState.TargetManager->BuildFirstSelectedTargetable(SceneState, GetTargetRequirements());
	check(Target);
	NewTool->SetTarget(Target);

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

	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	check(TargetComponent);

	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->bBuildSpatialDataStructure = false;
	PreviewMesh->CreateInWorld(TargetComponent->GetOwnerActor()->GetWorld(), FTransform::Identity);
	PreviewMesh->SetTransform(TargetComponent->GetWorldTransform());

	FComponentMaterialSet MaterialSet;
	TargetComponent->GetMaterialSet(MaterialSet);
	PreviewMesh->SetMaterials(MaterialSet.Materials);

	PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::ExternallyCalculated);

	// TODO: We really just need a dynamic mesh, not a mesh description (since we're converting the mesh
	// description to a dynamic mesh), but it requires refactoring PreviewMesh to accept a dynamic mesh. 
	// Once we do, however, the requirement for this tool will change to IDynamicMeshProvider, and we'll
	// use that instead.
	PreviewMesh->InitializeMesh(Cast<IMeshDescriptionProvider>(Target)->GetMeshDescription());

	InputMesh.Copy(*PreviewMesh->GetMesh());

	VolumeEdgesSet = NewObject<ULineSetComponent>(PreviewMesh->GetRootComponent());
	VolumeEdgesSet->SetupAttachment(PreviewMesh->GetRootComponent());
	VolumeEdgesSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager()));
	VolumeEdgesSet->RegisterComponent();

	// hide input StaticMeshComponent
	TargetComponent->SetOwnerVisibility(false);

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

	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	TargetComponent->SetOwnerVisibility(true);

	UWorld* TargetWorld = TargetComponent->GetOwnerActor()->GetWorld();


	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshToVolumeToolTransactionName", "Create Volume"));

		FTransform TargetTransform = TargetComponent->GetWorldTransform();

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
		Actors.Add(TargetComponent->GetOwnerActor());
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
