// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelMorphologyMeshesTool.h"

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

#include "AssetGenerationUtil.h"

#include "CompositionOps/VoxelMorphologyMeshesOp.h"


#define LOCTEXT_NAMESPACE "UVoxelMorphologyMeshesTool"



void UVoxelMorphologyMeshesTool::SetupProperties()
{
	Super::SetupProperties();

	MorphologyProperties = NewObject<UVoxelMorphologyMeshesToolProperties>(this);
	MorphologyProperties->RestoreProperties(this);
	AddToolPropertySource(MorphologyProperties);

	SetToolDisplayName(LOCTEXT("VoxelMorphologyMeshesToolName", "Mesh Morphology Tool"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Apply Morphological operations to the input meshes to create a new Mesh, using voxelization techniques. UVs, sharp edges, and small/thin features will be lost. Increase Voxel Count to enhance accuracy."),
		EToolMessageLevel::UserNotification);

}


void UVoxelMorphologyMeshesTool::SaveProperties()
{
	Super::SaveProperties();

	VoxProperties->SaveProperties(this);
}


TUniquePtr<FDynamicMeshOperator> UVoxelMorphologyMeshesTool::MakeNewOperator()
{
	TUniquePtr<FVoxelMorphologyMeshesOp> Op = MakeUnique<FVoxelMorphologyMeshesOp>();

	Op->Transforms.SetNum(ComponentTargets.Num());
	Op->Meshes.SetNum(ComponentTargets.Num());
	for (int Idx = 0; Idx < ComponentTargets.Num(); Idx++)
	{
		Op->Meshes[Idx] = OriginalDynamicMeshes[Idx];
		FTransform UseTransform = TransformProxies[Idx]->GetTransform();
		UseTransform.MultiplyScale3D(TransformInitialScales[Idx]);
		Op->Transforms[Idx] = UseTransform;
	}

	VoxProperties->SetPropertiesOnOp(*Op);
	
	Op->bSolidifyInput = MorphologyProperties->bSolidifyInput;
	Op->OffsetSolidifySurface = MorphologyProperties->OffsetSolidifySurface;
	Op->bRemoveInternalsAfterSolidify = MorphologyProperties->bRemoveInternalsAfterSolidify;
	Op->Distance = MorphologyProperties->Distance;
	Op->Operation = MorphologyProperties->Operation;

	return Op;
}


FString UVoxelMorphologyMeshesTool::GetCreatedAssetName() const
{
	return TEXT("Morphology");
}

FText UVoxelMorphologyMeshesTool::GetActionName() const
{
	return LOCTEXT("VoxelMorphologyMeshes", "Voxel Morphology");
}


#undef LOCTEXT_NAMESPACE
