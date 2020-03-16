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


void UNewMeshMaterialProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UNewMeshMaterialProperties* PropertyCache = GetPropertyCache<UNewMeshMaterialProperties>();
	PropertyCache->Material = this->Material;
	PropertyCache->UVScale = this->UVScale;
	PropertyCache->bWorldSpaceUVScale = this->bWorldSpaceUVScale;
	// not bWireframe
}

void UNewMeshMaterialProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UNewMeshMaterialProperties* PropertyCache = GetPropertyCache<UNewMeshMaterialProperties>();
	this->Material = PropertyCache->Material;
	this->UVScale = PropertyCache->UVScale;
	this->bWorldSpaceUVScale = PropertyCache->bWorldSpaceUVScale;
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

void UExistingMeshMaterialProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UExistingMeshMaterialProperties* PropertyCache = GetPropertyCache<UExistingMeshMaterialProperties>();
	PropertyCache->MaterialMode = this->MaterialMode;
	PropertyCache->CheckerDensity = this->CheckerDensity;
	PropertyCache->OverrideMaterial = this->OverrideMaterial;
}

void UExistingMeshMaterialProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UExistingMeshMaterialProperties* PropertyCache = GetPropertyCache<UExistingMeshMaterialProperties>();
	this->MaterialMode = PropertyCache->MaterialMode;
	this->CheckerDensity = PropertyCache->CheckerDensity;
	this->OverrideMaterial = PropertyCache->OverrideMaterial;
	this->Setup();
}

void UMeshEditingViewProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UMeshEditingViewProperties* PropertyCache = GetPropertyCache<UMeshEditingViewProperties>();
	PropertyCache->bShowWireframe = this->bShowWireframe;
	PropertyCache->MaterialMode = this->MaterialMode;
	PropertyCache->bFlatShading = this->bFlatShading;
	PropertyCache->Color = this->Color;
	PropertyCache->Image = this->Image;
}

void UMeshEditingViewProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UMeshEditingViewProperties* PropertyCache = GetPropertyCache<UMeshEditingViewProperties>();
	this->bShowWireframe = PropertyCache->bShowWireframe;
	this->MaterialMode = PropertyCache->MaterialMode;
	this->bFlatShading = PropertyCache->bFlatShading;
	this->Color = PropertyCache->Color;
	this->Image = PropertyCache->Image;
}


#undef LOCTEXT_NAMESPACE
