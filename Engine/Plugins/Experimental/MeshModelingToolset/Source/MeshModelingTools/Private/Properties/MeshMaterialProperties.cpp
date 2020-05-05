// Copyright Epic Games, Inc. All Rights Reserved.

#include "Properties/MeshMaterialProperties.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"

#include "SimpleDynamicMeshComponent.h"

#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "UMeshMaterialProperites"

UNewMeshMaterialProperties::UNewMeshMaterialProperties()
{
	Material = CreateDefaultSubobject<UMaterialInterface>(TEXT("MATERIAL"));
}

void UExistingMeshMaterialProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	Super::RestoreProperties(RestoreToTool);
	Setup();
}

void UExistingMeshMaterialProperties::Setup()
{
	UMaterial* CheckerMaterialBase = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/CheckerMaterial"));
	if (CheckerMaterialBase != nullptr)
	{
		CheckerMaterial = UMaterialInstanceDynamic::Create(CheckerMaterialBase, NULL);
		if (CheckerMaterial != nullptr)
		{
			CheckerMaterial->SetScalarParameterValue("Density", CheckerDensity);
		}
	}
}

void UExistingMeshMaterialProperties::UpdateMaterials()
{
	if (CheckerMaterial != nullptr)
	{
		CheckerMaterial->SetScalarParameterValue("Density", CheckerDensity);
	}
}


UMaterialInterface* UExistingMeshMaterialProperties::GetActiveOverrideMaterial() const
{
	if (MaterialMode == ESetMeshMaterialMode::Checkerboard && CheckerMaterial != nullptr)
	{
		return CheckerMaterial;
	}
	if (MaterialMode == ESetMeshMaterialMode::Override && OverrideMaterial != nullptr)
	{
		return OverrideMaterial;
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
