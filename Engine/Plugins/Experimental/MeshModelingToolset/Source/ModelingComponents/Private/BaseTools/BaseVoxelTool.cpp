// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseTools/BaseVoxelTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "SimpleDynamicMeshComponent.h"
#include "DynamicMesh3.h"

#include "MeshDescriptionToDynamicMesh.h"


#define LOCTEXT_NAMESPACE "UBaseVoxelTool"

using namespace UE::Geometry;

void UBaseVoxelTool::SetupProperties()
{
	Super::SetupProperties();
	VoxProperties = NewObject<UVoxelProperties>(this);
	VoxProperties->RestoreProperties(this);
	AddToolPropertySource(VoxProperties);
}


void UBaseVoxelTool::SaveProperties()
{
	Super::SaveProperties();
	VoxProperties->SaveProperties(this);
}


TArray<UMaterialInterface*> UBaseVoxelTool::GetOutputMaterials() const
{
	TArray<UMaterialInterface*> Materials;
	Materials.Add(LoadObject<UMaterial>(nullptr, TEXT("MATERIAL")));
	return Materials;
}


void UBaseVoxelTool::ConvertInputsAndSetPreviewMaterials(bool bSetPreviewMesh)
{
	OriginalDynamicMeshes.SetNum(Targets.Num());

	for (int ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		OriginalDynamicMeshes[ComponentIdx] = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(TargetMeshProviderInterface(ComponentIdx)->GetMeshDescription(), *OriginalDynamicMeshes[ComponentIdx]);
	}

	//if (bSetPreviewMesh)
	//{
	//	// TODO: create a low quality preview result for initial display?
	//}

	Preview->ConfigureMaterials(
		ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager()),
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);
}


#undef LOCTEXT_NAMESPACE
