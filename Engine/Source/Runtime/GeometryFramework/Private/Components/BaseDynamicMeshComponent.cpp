// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Components/BaseDynamicMeshComponent.h"
#include "Components/BaseDynamicMeshSceneProxy.h"

using namespace UE::Geometry;

UBaseDynamicMeshComponent::UBaseDynamicMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}



#if WITH_EDITOR
void UBaseDynamicMeshComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.GetPropertyName();
	if ( (PropName == GET_MEMBER_NAME_CHECKED(UBaseDynamicMeshComponent, bEnableRaytracing))  )
	{
		OnRaytracingStateChanged();
	}
}
#endif


void UBaseDynamicMeshComponent::SetShadowsEnabled(bool bEnabled)
{
	// finish any drawing so that we can be certain our SceneProxy is no longer in use before we rebuild it below
	FlushRenderingCommands();

	SetCastShadow(bEnabled);
	//bCastDynamicShadow = bEnabled;

	// apparently SceneProxy has to be fully rebuilt to change shadow state

	// this marks the SceneProxy for rebuild, but not immediately, and possibly allows bad things to happen
	// before the end of the frame
	//MarkRenderStateDirty();

	// force immediate rebuild of the SceneProxy
	if (IsRegistered())
	{
		ReregisterComponent();
	}
}


void UBaseDynamicMeshComponent::SetOverrideRenderMaterial(UMaterialInterface* Material)
{
	if (OverrideRenderMaterial != Material)
	{
		OverrideRenderMaterial = Material;
		NotifyMaterialSetUpdated();
	}
}

void UBaseDynamicMeshComponent::ClearOverrideRenderMaterial()
{
	if (OverrideRenderMaterial != nullptr)
	{
		OverrideRenderMaterial = nullptr;
		NotifyMaterialSetUpdated();
	}
}





void UBaseDynamicMeshComponent::SetSecondaryRenderMaterial(UMaterialInterface* Material)
{
	if (SecondaryRenderMaterial != Material)
	{
		SecondaryRenderMaterial = Material;
		NotifyMaterialSetUpdated();
	}
}

void UBaseDynamicMeshComponent::ClearSecondaryRenderMaterial()
{
	if (SecondaryRenderMaterial != nullptr)
	{
		SecondaryRenderMaterial = nullptr;
		NotifyMaterialSetUpdated();
	}
}



void UBaseDynamicMeshComponent::SetSecondaryBuffersVisibility(bool bSecondaryVisibility)
{
	bDrawSecondaryBuffers = bSecondaryVisibility;
}

bool UBaseDynamicMeshComponent::GetSecondaryBuffersVisibility() const
{
	return bDrawSecondaryBuffers;
}


void UBaseDynamicMeshComponent::SetEnableRaytracing(bool bSetEnabled)
{
	if (bEnableRaytracing != bSetEnabled)
	{
		bEnableRaytracing = bSetEnabled;
		OnRaytracingStateChanged();
	}
}

bool UBaseDynamicMeshComponent::GetEnableRaytracing() const
{
	return bEnableRaytracing;
}

void UBaseDynamicMeshComponent::OnRaytracingStateChanged()
{
	// finish any drawing so that we can be certain our SceneProxy is no longer in use before we rebuild it below
	FlushRenderingCommands();

	// force immediate rebuild of the SceneProxy
	if (IsRegistered())
	{
		ReregisterComponent();
	}
}


int32 UBaseDynamicMeshComponent::GetNumMaterials() const
{
	return BaseMaterials.Num();
}

UMaterialInterface* UBaseDynamicMeshComponent::GetMaterial(int32 ElementIndex) const 
{
	return (ElementIndex >= 0 && ElementIndex < BaseMaterials.Num()) ? BaseMaterials[ElementIndex] : nullptr;
}

FMaterialRelevance UBaseDynamicMeshComponent::GetMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const
{
	FMaterialRelevance Result = UMeshComponent::GetMaterialRelevance(InFeatureLevel);
	if (OverrideRenderMaterial)
	{
		Result |= OverrideRenderMaterial->GetRelevance_Concurrent(InFeatureLevel);
	}
	if (SecondaryRenderMaterial)
	{
		Result |= SecondaryRenderMaterial->GetRelevance_Concurrent(InFeatureLevel);
	}
	return Result;
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


void UBaseDynamicMeshComponent::SetNumMaterials(int32 NumMaterials)
{
	if (BaseMaterials.Num() > NumMaterials)
	{
		// discard extra materials
		BaseMaterials.SetNum(NumMaterials);
	}
	else
	{
		while (NumMaterials < BaseMaterials.Num())
		{
			SetMaterial(NumMaterials, nullptr);
			NumMaterials++;
		}
	}
}

void UBaseDynamicMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	UMeshComponent::GetUsedMaterials(OutMaterials, bGetDebugMaterials);
	if (OverrideRenderMaterial != nullptr)
	{
		OutMaterials.Add(OverrideRenderMaterial);
	}
	if (SecondaryRenderMaterial != nullptr)
	{
		OutMaterials.Add(SecondaryRenderMaterial);
	}
}

