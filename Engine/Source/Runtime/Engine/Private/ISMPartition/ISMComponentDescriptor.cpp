// Copyright Epic Games, Inc. All Rights Reserved.

#include "ISMPartition/ISMComponentDescriptor.h"

#if WITH_EDITOR

#include "Serialization/ArchiveObjectCrc32.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

#endif

#if WITH_EDITOR

FISMComponentDescriptor FISMComponentDescriptor::CreateFrom(const TSubclassOf<UStaticMeshComponent>& From)
{
	FISMComponentDescriptor ComponentDescriptor;

	ComponentDescriptor.InitFrom(From->GetDefaultObject<UStaticMeshComponent>());
	ComponentDescriptor.ComputeHash();

	return ComponentDescriptor;
}

void FISMComponentDescriptor::InitFrom(UStaticMeshComponent* Template, bool bInitBodyInstance)
{
	bEnableDiscardOnLoad = false;
	ComponentClass = Template->GetClass();
	StaticMesh = Template->GetStaticMesh();
	OverrideMaterials = Template->OverrideMaterials;
	Mobility = Template->Mobility;
	RuntimeVirtualTextures = Template->RuntimeVirtualTextures;
	VirtualTextureRenderPassType = Template->VirtualTextureRenderPassType;
	LightmapType = Template->LightmapType;
	LightingChannels = Template->LightingChannels;
	bHasCustomNavigableGeometry = Template->bHasCustomNavigableGeometry;
	CustomDepthStencilWriteMask = Template->CustomDepthStencilWriteMask;
	VirtualTextureCullMips = Template->VirtualTextureCullMips;
	TranslucencySortPriority = Template->TranslucencySortPriority;
	OverriddenLightMapRes = Template->OverriddenLightMapRes;
	CustomDepthStencilValue = Template->CustomDepthStencilValue;
	bCastShadow = Template->CastShadow;
	bCastStaticShadow = Template->bCastStaticShadow;
	bCastDynamicShadow = Template->bCastDynamicShadow;
	bCastContactShadow = Template->bCastContactShadow;
	bCastShadowAsTwoSided = Template->bCastShadowAsTwoSided;
	bAffectDynamicIndirectLighting = Template->bAffectDynamicIndirectLighting;
	bAffectDistanceFieldLighting = Template->bAffectDistanceFieldLighting;
	bReceivesDecals = Template->bReceivesDecals;
	bOverrideLightMapRes = Template->bOverrideLightMapRes;
	bUseAsOccluder = Template->bUseAsOccluder;
	bRenderCustomDepth = Template->bRenderCustomDepth;
	bIncludeInHLOD = Template->bEnableAutoLODGeneration;

	if (UInstancedStaticMeshComponent* ISMTemplate = Cast<UInstancedStaticMeshComponent>(Template))
	{
		InstanceStartCullDistance = ISMTemplate->InstanceStartCullDistance;
		InstanceEndCullDistance = ISMTemplate->InstanceEndCullDistance;

		// HISM Specific
		if (UHierarchicalInstancedStaticMeshComponent* HISMTemplate = Cast<UHierarchicalInstancedStaticMeshComponent>(Template))
		{
			bEnableDensityScaling = HISMTemplate->bEnableDensityScaling;
		}
	}

	if (bInitBodyInstance)
	{
		BodyInstance.CopyBodyInstancePropertiesFrom(&Template->BodyInstance);
	}
}

bool FISMComponentDescriptor::operator!=(const FISMComponentDescriptor& Other) const
{
	return !(*this == Other);
}

bool FISMComponentDescriptor::operator==(const FISMComponentDescriptor& Other) const
{
	return Hash == Other.Hash && // Check hash first, other checks are in case of Hash collision
	ComponentClass == Other.ComponentClass &&
	StaticMesh == Other.StaticMesh &&
	OverrideMaterials == Other.OverrideMaterials &&
	Mobility == Other.Mobility &&
	RuntimeVirtualTextures == Other.RuntimeVirtualTextures &&
	VirtualTextureRenderPassType == Other.VirtualTextureRenderPassType &&
	LightmapType == Other.LightmapType &&
	GetLightingChannelMaskForStruct(LightingChannels) == GetLightingChannelMaskForStruct(Other.LightingChannels) &&
	bHasCustomNavigableGeometry == Other.bHasCustomNavigableGeometry &&
	CustomDepthStencilWriteMask == Other.CustomDepthStencilWriteMask &&
	InstanceStartCullDistance == Other.InstanceStartCullDistance &&
	InstanceEndCullDistance == Other.InstanceEndCullDistance &&
	VirtualTextureCullMips == Other.VirtualTextureCullMips &&
	TranslucencySortPriority == Other.TranslucencySortPriority &&
	OverriddenLightMapRes == Other.OverriddenLightMapRes &&
	CustomDepthStencilValue == Other.CustomDepthStencilValue &&
	bCastShadow == Other.bCastShadow &&
	bCastStaticShadow == Other.bCastStaticShadow &&
	bCastDynamicShadow == Other.bCastDynamicShadow &&
	bCastContactShadow == Other.bCastContactShadow &&
	bCastShadowAsTwoSided == Other.bCastShadowAsTwoSided &&
	bAffectDynamicIndirectLighting == Other.bAffectDynamicIndirectLighting &&
	bAffectDistanceFieldLighting == Other.bAffectDistanceFieldLighting &&
	bReceivesDecals == Other.bReceivesDecals &&
	bOverrideLightMapRes == Other.bOverrideLightMapRes &&
	bUseAsOccluder == Other.bUseAsOccluder &&
	bRenderCustomDepth == Other.bRenderCustomDepth &&
	bIncludeInHLOD == Other.bIncludeInHLOD &&
	bEnableDiscardOnLoad == Other.bEnableDiscardOnLoad &&
	BodyInstance.GetCollisionEnabled() == Other.BodyInstance.GetCollisionEnabled() && 
	BodyInstance.GetCollisionResponse() == Other.BodyInstance.GetCollisionResponse() &&
	BodyInstance.DoesUseCollisionProfile() == Other.BodyInstance.DoesUseCollisionProfile() &&
	(!BodyInstance.DoesUseCollisionProfile() || (BodyInstance.GetCollisionProfileName() == Other.BodyInstance.GetCollisionProfileName()));
}

uint32 FISMComponentDescriptor::ComputeHash() const
{
	FArchiveObjectCrc32 CrcArchive;
	UISMComponentDescriptorHasher* Hasher = NewObject<UISMComponentDescriptorHasher>();
	Hash = 0; // we don't want the hash to impact the calculation
	Hasher->Descriptor = *this;
	uint32 Crc = CrcArchive.Crc32(Hasher);
	Hash = Crc;

	return Crc;
}

UInstancedStaticMeshComponent* FISMComponentDescriptor::CreateComponent(UObject* Outer, FName Name, EObjectFlags ObjectFlags) const
{
	UInstancedStaticMeshComponent* ISMComponent = NewObject<UInstancedStaticMeshComponent>(Outer, ComponentClass, Name, ObjectFlags);
	
	ISMComponent->SetStaticMesh(StaticMesh);
	ISMComponent->OverrideMaterials = OverrideMaterials;
	ISMComponent->Mobility = Mobility;
	ISMComponent->RuntimeVirtualTextures = RuntimeVirtualTextures;
	ISMComponent->VirtualTextureRenderPassType = VirtualTextureRenderPassType;
	ISMComponent->LightmapType = LightmapType;
	ISMComponent->LightingChannels = LightingChannels;
	ISMComponent->bHasCustomNavigableGeometry = bHasCustomNavigableGeometry;
	ISMComponent->CustomDepthStencilWriteMask = CustomDepthStencilWriteMask;
	ISMComponent->BodyInstance.CopyBodyInstancePropertiesFrom(&BodyInstance);
	ISMComponent->InstanceStartCullDistance = InstanceStartCullDistance;
	ISMComponent->InstanceEndCullDistance = InstanceEndCullDistance;
	ISMComponent->VirtualTextureCullMips = VirtualTextureCullMips;
	ISMComponent->TranslucencySortPriority = TranslucencySortPriority;
	ISMComponent->OverriddenLightMapRes = OverriddenLightMapRes;
	ISMComponent->CustomDepthStencilValue = CustomDepthStencilValue;
	ISMComponent->CastShadow = bCastShadow;
	ISMComponent->bCastStaticShadow = bCastStaticShadow;
	ISMComponent->bCastDynamicShadow = bCastDynamicShadow;
	ISMComponent->bCastContactShadow = bCastContactShadow;
	ISMComponent->bCastShadowAsTwoSided = bCastShadowAsTwoSided;
	ISMComponent->bAffectDynamicIndirectLighting = bAffectDynamicIndirectLighting;
	ISMComponent->bAffectDistanceFieldLighting = bAffectDistanceFieldLighting;
	ISMComponent->bReceivesDecals = bReceivesDecals;
	ISMComponent->bOverrideLightMapRes = bOverrideLightMapRes;
	ISMComponent->bUseAsOccluder = bUseAsOccluder;
	ISMComponent->bRenderCustomDepth = bRenderCustomDepth;
	ISMComponent->bEnableAutoLODGeneration = bIncludeInHLOD;;

	// HISM Specific
	if (UHierarchicalInstancedStaticMeshComponent* HISMComponent = Cast<UHierarchicalInstancedStaticMeshComponent>(ISMComponent))
	{
		HISMComponent->bEnableDensityScaling = bEnableDensityScaling;
	}

	return ISMComponent;
}

#endif

UISMComponentDescriptorHasher::UISMComponentDescriptorHasher(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}