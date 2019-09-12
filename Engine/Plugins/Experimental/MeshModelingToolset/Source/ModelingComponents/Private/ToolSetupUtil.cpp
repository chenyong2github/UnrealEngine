// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "ToolSetupUtil.h"
#include "InteractiveTool.h"
#include "InteractiveToolManager.h"
#include "Materials/Material.h"



UMaterialInterface* ToolSetupUtil::GetDefaultMaterial(UInteractiveToolManager* ToolManager, UMaterialInterface* SourceMaterial)
{
	if (SourceMaterial == nullptr)
	{
		return ToolManager->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	}
	return SourceMaterial;
}



UMaterialInterface* ToolSetupUtil::GetDefaultWorkingMaterial(UInteractiveToolManager* ToolManager)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/InProgressMaterial"));
	if (Material == nullptr)
	{
		return ToolManager->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	}
	return Material;
}