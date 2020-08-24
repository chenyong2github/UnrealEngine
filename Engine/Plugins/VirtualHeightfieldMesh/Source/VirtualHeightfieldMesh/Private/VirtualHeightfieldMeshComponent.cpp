// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualHeightfieldMeshComponent.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "Engine/World.h"
#include "HeightfieldMinMaxTexture.h"
#include "VirtualHeightfieldMeshSceneProxy.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureVolume.h"

UVirtualHeightfieldMeshComponent::UVirtualHeightfieldMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bNeverDistanceCull = true;
#if WITH_EDITORONLY_DATA
	bEnableAutoLODGeneration = false;
#endif // WITH_EDITORONLY_DATA

	Mobility = EComponentMobility::Static;
}

ARuntimeVirtualTextureVolume* UVirtualHeightfieldMeshComponent::GetVirtualTextureVolume() const
{
	return VirtualTexture.Get();
}

URuntimeVirtualTexture* UVirtualHeightfieldMeshComponent::GetVirtualTexture() const
{
	URuntimeVirtualTextureComponent* RuntimeVirtualTextureComponent = VirtualTexture.IsValid() ? VirtualTexture.Get()->VirtualTextureComponent : nullptr;
	return RuntimeVirtualTextureComponent ? RuntimeVirtualTextureComponent->GetVirtualTexture() : nullptr;
}

FTransform UVirtualHeightfieldMeshComponent::GetVirtualTextureTransform() const
{
	URuntimeVirtualTextureComponent* RuntimeVirtualTextureComponent = VirtualTexture.IsValid() ? VirtualTexture.Get()->VirtualTextureComponent : nullptr;
	return RuntimeVirtualTextureComponent ? RuntimeVirtualTextureComponent->GetComponentTransform() * RuntimeVirtualTextureComponent->GetTexelSnapTransform() : FTransform::Identity;
}

bool UVirtualHeightfieldMeshComponent::IsVisible() const
{
	return
		Super::IsVisible() &&
		GetVirtualTexture() != nullptr &&
		GetVirtualTexture()->GetMaterialType() == ERuntimeVirtualTextureMaterialType::WorldHeight &&
		UseVirtualTexturing(GetScene() ? GetScene()->GetFeatureLevel() : ERHIFeatureLevel::SM5);
}

FPrimitiveSceneProxy* UVirtualHeightfieldMeshComponent::CreateSceneProxy()
{
	//hack[vhm]: No scene representation when disabled for now.
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VHM.Enable"));
	bool bVHMEnable = CVar != nullptr && CVar->GetValueOnAnyThread() != 0;
	if (!bVHMEnable)
	{
		return nullptr;
	}

	return new FVirtualHeightfieldMeshSceneProxy(this);
}

FBoxSphereBounds UVirtualHeightfieldMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(FBox(FVector(0.f, 0.f, 0.f), FVector(1.f, 1.f, 1.f))).TransformBy(LocalToWorld);
}

void UVirtualHeightfieldMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (Material != nullptr)
	{
		OutMaterials.Add(Material);
	}
}

bool UVirtualHeightfieldMeshComponent::IsMinMaxTextureEnabled() const
{
	URuntimeVirtualTexture* RuntimeVirtualTexture = GetVirtualTexture();
	return RuntimeVirtualTexture != nullptr && RuntimeVirtualTexture->GetMaterialType() == ERuntimeVirtualTextureMaterialType::WorldHeight;
}

#if WITH_EDITOR

void UVirtualHeightfieldMeshComponent::InitializeMinMaxTexture(uint32 InSizeX, uint32 InSizeY, uint32 InNumMips, uint8* InData)
{
	// We need an existing StreamingTexture object to update.
	if (MinMaxTexture != nullptr)
	{
		FHeightfieldMinMaxTextureBuildDesc BuildDesc;
		BuildDesc.SizeX = InSizeX;
		BuildDesc.SizeY = InSizeY;
		BuildDesc.NumMips = InNumMips;
		BuildDesc.Data = InData;

		MinMaxTexture->Modify();
		MinMaxTexture->BuildTexture(BuildDesc);

		MarkRenderStateDirty();
	}
}

#endif
