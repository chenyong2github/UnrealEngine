// Copyright Epic Games, Inc. All Rights Reserved.

#include "ISMPartition/ISMComponentDescriptor.h"
#include "Concepts/StaticStructProvider.h"
#include "Materials/MaterialInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ISMComponentDescriptor)

#include "Components/StaticMeshComponent.h"
#include "Serialization/ArchiveCrc32.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

FISMComponentDescriptor::FISMComponentDescriptor()
{
	// Make sure we have proper defaults
	InitFrom(UHierarchicalInstancedStaticMeshComponent::StaticClass()->GetDefaultObject<UHierarchicalInstancedStaticMeshComponent>());
}

FISMComponentDescriptor FISMComponentDescriptor::CreateFrom(const TSubclassOf<UStaticMeshComponent>& From)
{
	FISMComponentDescriptor ComponentDescriptor;

	ComponentDescriptor.InitFrom(From->GetDefaultObject<UStaticMeshComponent>());
	ComponentDescriptor.ComputeHash();

	return ComponentDescriptor;
}

void FISMComponentDescriptor::InitFrom(const UStaticMeshComponent* Template, bool bInitBodyInstance)
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
	RayTracingGroupId = Template->RayTracingGroupId;
	RayTracingGroupCullingPriority = Template->RayTracingGroupCullingPriority;
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
	bHiddenInGame = Template->bHiddenInGame;
	bIsEditorOnly = Template->bIsEditorOnly;
	bVisible = Template->GetVisibleFlag();
	bVisibleInRayTracing = Template->bVisibleInRayTracing;
	bEvaluateWorldPositionOffset = Template->bEvaluateWorldPositionOffset;
	WorldPositionOffsetDisableDistance = Template->WorldPositionOffsetDisableDistance;
	// Determine if this instance must render with reversed culling based on both scale and the component property
	const bool bIsLocalToWorldDeterminantNegative = Template->GetRenderMatrix().Determinant() < 0;
	bReverseCulling = Template->bReverseCulling != bIsLocalToWorldDeterminantNegative;

#if WITH_EDITORONLY_DATA
	HLODBatchingPolicy = Template->HLODBatchingPolicy;
	bIncludeInHLOD = Template->bEnableAutoLODGeneration;
	bConsiderForActorPlacementWhenHidden = Template->bConsiderForActorPlacementWhenHidden;
#endif

	if (const UInstancedStaticMeshComponent* ISMTemplate = Cast<UInstancedStaticMeshComponent>(Template))
	{
		InstanceStartCullDistance = ISMTemplate->InstanceStartCullDistance;
		InstanceEndCullDistance = ISMTemplate->InstanceEndCullDistance;

		// HISM Specific
		if (const UHierarchicalInstancedStaticMeshComponent* HISMTemplate = Cast<UHierarchicalInstancedStaticMeshComponent>(Template))
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
	RayTracingGroupId == Other.RayTracingGroupId &&
	RayTracingGroupCullingPriority == Other.RayTracingGroupCullingPriority &&
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
	bEnableDiscardOnLoad == Other.bEnableDiscardOnLoad &&
	bHiddenInGame == Other.bHiddenInGame &&
	bIsEditorOnly == Other.bIsEditorOnly &&
	bVisible == Other.bVisible &&
	bVisibleInRayTracing == Other.bVisibleInRayTracing &&
	bEvaluateWorldPositionOffset == Other.bEvaluateWorldPositionOffset &&
	bReverseCulling == Other.bReverseCulling &&
	WorldPositionOffsetDisableDistance == Other.WorldPositionOffsetDisableDistance &&
#if WITH_EDITORONLY_DATA
	HLODBatchingPolicy == Other.HLODBatchingPolicy &&
	bIncludeInHLOD == Other.bIncludeInHLOD &&
	bConsiderForActorPlacementWhenHidden == Other.bConsiderForActorPlacementWhenHidden &&
#endif // WITH_EDITORONLY_DATA
	BodyInstance.GetCollisionEnabled() == Other.BodyInstance.GetCollisionEnabled() && 
	BodyInstance.GetCollisionResponse() == Other.BodyInstance.GetCollisionResponse() &&
	BodyInstance.DoesUseCollisionProfile() == Other.BodyInstance.DoesUseCollisionProfile() &&
	(!BodyInstance.DoesUseCollisionProfile() || (BodyInstance.GetCollisionProfileName() == Other.BodyInstance.GetCollisionProfileName()));
}

uint32 FISMComponentDescriptor::ComputeHash() const
{
	FArchiveCrc32 CrcArchive;

	Hash = 0; // we don't want the hash to impact the calculation
	CrcArchive << *this;
	Hash = CrcArchive.GetCrc();

	return Hash;
}

UInstancedStaticMeshComponent* FISMComponentDescriptor::CreateComponent(UObject* Outer, FName Name, EObjectFlags ObjectFlags) const
{
	UInstancedStaticMeshComponent* ISMComponent = NewObject<UInstancedStaticMeshComponent>(Outer, ComponentClass, Name, ObjectFlags);
	
	InitComponent(ISMComponent);

	return ISMComponent;
}

void FISMComponentDescriptor::InitComponent(UInstancedStaticMeshComponent* ISMComponent) const
{
	ISMComponent->SetStaticMesh(StaticMesh);

	ISMComponent->OverrideMaterials.Empty(OverrideMaterials.Num());
	for (UMaterialInterface* OverrideMaterial : OverrideMaterials)
	{
		if (OverrideMaterial && !OverrideMaterial->IsAsset())
		{
			// As override materials are normally outered to their owner component, we need to duplicate them here to make sure we don't create
			// references to actors in other levels (for packed level instances or HLOD actors).
			OverrideMaterial = DuplicateObject<UMaterialInterface>(OverrideMaterial, ISMComponent);
		}

		ISMComponent->OverrideMaterials.Add(OverrideMaterial);
	}

	ISMComponent->Mobility = Mobility;
	ISMComponent->RuntimeVirtualTextures = RuntimeVirtualTextures;
	ISMComponent->VirtualTextureRenderPassType = VirtualTextureRenderPassType;
	ISMComponent->LightmapType = LightmapType;
	ISMComponent->LightingChannels = LightingChannels;
	ISMComponent->RayTracingGroupId = RayTracingGroupId;
	ISMComponent->RayTracingGroupCullingPriority = RayTracingGroupCullingPriority;
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
	ISMComponent->bHiddenInGame = bHiddenInGame;
	ISMComponent->bIsEditorOnly = bIsEditorOnly;
	ISMComponent->SetVisibleFlag(bVisible);
	ISMComponent->bVisibleInRayTracing = bVisibleInRayTracing;
	ISMComponent->bEvaluateWorldPositionOffset = bEvaluateWorldPositionOffset;
	ISMComponent->bReverseCulling = bReverseCulling;
	ISMComponent->WorldPositionOffsetDisableDistance = WorldPositionOffsetDisableDistance;
	
#if WITH_EDITORONLY_DATA
	ISMComponent->HLODBatchingPolicy = HLODBatchingPolicy;
	ISMComponent->bEnableAutoLODGeneration = bIncludeInHLOD;
	ISMComponent->bConsiderForActorPlacementWhenHidden = bConsiderForActorPlacementWhenHidden;
#endif // WITH_EDITORONLY_DATA

	// HISM Specific
	if (UHierarchicalInstancedStaticMeshComponent* HISMComponent = Cast<UHierarchicalInstancedStaticMeshComponent>(ISMComponent))
	{
		HISMComponent->bEnableDensityScaling = bEnableDensityScaling;
	}
}
