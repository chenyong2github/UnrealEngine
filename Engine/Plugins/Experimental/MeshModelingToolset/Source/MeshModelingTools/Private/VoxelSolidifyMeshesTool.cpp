// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelSolidifyMeshesTool.h"
#include "CompositionOps/VoxelSolidifyMeshesOp.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "Selection/ToolSelectionUtil.h"

#include "DynamicMesh3.h"
#include "MeshTransforms.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "InteractiveGizmoManager.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "CompositionOps/VoxelSolidifyMeshesOp.h"


#define LOCTEXT_NAMESPACE "UVoxelSolidifyMeshesTool"



void UVoxelSolidifyMeshesTool::SetupProperties()
{
	Super::SetupProperties();
	SolidifyProperties = NewObject<UVoxelSolidifyMeshesToolProperties>(this);
	SolidifyProperties->RestoreProperties(this);
	AddToolPropertySource(SolidifyProperties);

	SetToolDisplayName(LOCTEXT("VoxelSolidifyMeshesToolName", "Wrap Meshes Tool"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("VoxelSolidifyMeshesToolDescription", "Create a new closed/solid shell mesh that wraps the input meshes. Holes will automatically be filled, controlled by the Winding Threshold. UVs, sharp edges, and small/thin features will be lost. Increase Voxel Count to enhance accuracy."),
		EToolMessageLevel::UserNotification);
}


void UVoxelSolidifyMeshesTool::SaveProperties()
{
	Super::SaveProperties();
	SolidifyProperties->SaveProperties(this);
}


TUniquePtr<FDynamicMeshOperator> UVoxelSolidifyMeshesTool::MakeNewOperator()
{
	TUniquePtr<FVoxelSolidifyMeshesOp> Op = MakeUnique<FVoxelSolidifyMeshesOp>();

	Op->Transforms.SetNum(ComponentTargets.Num());
	Op->Meshes.SetNum(ComponentTargets.Num());
	for (int Idx = 0; Idx < ComponentTargets.Num(); Idx++)
	{
		Op->Meshes[Idx] = OriginalDynamicMeshes[Idx];
		FTransform UseTransform = TransformProxies[Idx]->GetTransform();
		UseTransform.MultiplyScale3D(TransformInitialScales[Idx]);
		Op->Transforms[Idx] = UseTransform;
	}

	Op->bSolidAtBoundaries = SolidifyProperties->bSolidAtBoundaries;
	Op->WindingThreshold = SolidifyProperties->WindingThreshold;
	Op->bMakeOffsetSurfaces = SolidifyProperties->bMakeOffsetSurfaces;
	Op->OffsetThickness = SolidifyProperties->OffsetThickness;
	Op->SurfaceSearchSteps = SolidifyProperties->SurfaceSearchSteps;
	Op->ExtendBounds = SolidifyProperties->ExtendBounds;
	VoxProperties->SetPropertiesOnOp(*Op);
	
	return Op;
}



FString UVoxelSolidifyMeshesTool::GetCreatedAssetName() const
{
	return TEXT("Solid");
}

FText UVoxelSolidifyMeshesTool::GetActionName() const
{
	return LOCTEXT("VoxelSolidifyMeshes", "Voxel Shell");
}









#undef LOCTEXT_NAMESPACE
