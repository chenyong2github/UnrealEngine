// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualHeightfieldMeshComponent.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "Engine/World.h"
#include "HeightfieldMinMaxTexture.h"
#include "VirtualHeightfieldMeshEnable.h"
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

void UVirtualHeightfieldMeshComponent::OnRegister()
{
// 	URuntimeVirtualTextureComponent* RuntimeVirtualTextureComponent = VirtualTexture.IsValid() ? VirtualTexture.Get()->VirtualTextureComponent : nullptr;
// 	if (RuntimeVirtualTextureComponent)
// 	{
// 		// Bind to delegate so that RuntimeVirtualTextureComponent will pull hide flags from this object.
// 		HideFlagDelegateHandle = RuntimeVirtualTextureComponent->GetHidePrimitivesDelegate().AddUObject(this, &UVirtualHeightfieldMeshComponent::GatherHideFlags);
// 		RuntimeVirtualTextureComponent->MarkRenderStateDirty();
// 	}
	Super::OnRegister();
}

void UVirtualHeightfieldMeshComponent::OnUnregister()
{
// 	URuntimeVirtualTextureComponent* RuntimeVirtualTextureComponent = VirtualTexture.IsValid() ? VirtualTexture.Get()->VirtualTextureComponent : nullptr;
// 	if (RuntimeVirtualTextureComponent)
// 	{
// 		RuntimeVirtualTextureComponent->GetHidePrimitivesDelegate().Remove(HideFlagDelegateHandle);
// 		RuntimeVirtualTextureComponent->MarkRenderStateDirty();
// 	}
	Super::OnUnregister();
}

bool UVirtualHeightfieldMeshComponent::IsVisible() const
{
	return
		Super::IsVisible() &&
		GetVirtualTexture() != nullptr &&
		GetVirtualTexture()->GetMaterialType() == ERuntimeVirtualTextureMaterialType::WorldHeight &&
		VirtualHeightfieldMesh::IsEnabled(GetScene() ? GetScene()->GetFeatureLevel() : ERHIFeatureLevel::SM5);
}

FBoxSphereBounds UVirtualHeightfieldMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(FBox(FVector(0.f, 0.f, 0.f), FVector(1.f, 1.f, 1.f))).TransformBy(LocalToWorld);
}

FPrimitiveSceneProxy* UVirtualHeightfieldMeshComponent::CreateSceneProxy()
{
	return new FVirtualHeightfieldMeshSceneProxy(this);
}

void UVirtualHeightfieldMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (Material != nullptr)
	{
		OutMaterials.Add(Material);
	}
}

void UVirtualHeightfieldMeshComponent::GatherHideFlags(bool& InOutHidePrimitivesInEditor, bool& InOutHidePrimitivesInGame) const
{
	const FStaticFeatureLevel FeatureLevel = GetScene() ? GetScene()->GetFeatureLevel() : ERHIFeatureLevel::SM5;
	const bool bIsEnabled = VirtualHeightfieldMesh::IsEnabled(FeatureLevel);
	InOutHidePrimitivesInEditor |= (bIsEnabled && !bHiddenInEditor);
	InOutHidePrimitivesInGame |= bIsEnabled;
}

#if WITH_EDITOR

void UVirtualHeightfieldMeshComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName HideInEditorName = GET_MEMBER_NAME_CHECKED(UVirtualHeightfieldMeshComponent, bHiddenInEditor);

	const FName PropertyName = PropertyChangedEvent.Property->GetFName();
	if (PropertyName == HideInEditorName)
	{
		// Force RuntimeVirtualTextureComponent to poll the HidePrimitives settings.
		URuntimeVirtualTextureComponent* RuntimeVirtualTextureComponent = VirtualTexture.IsValid() ? VirtualTexture.Get()->VirtualTextureComponent : nullptr;
		if (RuntimeVirtualTextureComponent != nullptr)
		{
			RuntimeVirtualTextureComponent->MarkRenderStateDirty();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

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
