// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved. 

#include "BaseDynamicMeshComponent.h"



UBaseDynamicMeshComponent::UBaseDynamicMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}



void UBaseDynamicMeshComponent::SetOverrideRenderMaterial(UMaterialInterface* Material)
{
	OverrideRenderMaterial = Material;
	NotifyMaterialSetUpdated();
}

void UBaseDynamicMeshComponent::ClearOverrideRenderMaterial()
{
	OverrideRenderMaterial = nullptr;
	NotifyMaterialSetUpdated();
}



int32 UBaseDynamicMeshComponent::GetNumMaterials() const
{
	return BaseMaterials.Num();
}

UMaterialInterface* UBaseDynamicMeshComponent::GetMaterial(int32 ElementIndex) const 
{
	return (ElementIndex >= 0 && ElementIndex < BaseMaterials.Num()) ? BaseMaterials[ElementIndex] : nullptr;
}

void UBaseDynamicMeshComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* Material)
{
	check(ElementIndex >= 0);
	if (ElementIndex >= BaseMaterials.Num())
	{
		BaseMaterials.SetNum(ElementIndex + 1, false);
	}
	BaseMaterials[ElementIndex] = Material;
}


void UBaseDynamicMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	UMeshComponent::GetUsedMaterials(OutMaterials, bGetDebugMaterials);
	if (OverrideRenderMaterial != nullptr)
	{
		OutMaterials.Add(OverrideRenderMaterial);
	}
}

