// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Components/PrimitiveComponent.h"
#include "ISMComponentDescriptor.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
enum class ERuntimeVirtualTextureMainPassType : uint8;
enum class ERendererStencilMask : uint8;

/** Struct that holds the relevant properties that can help decide if instances of different 
	StaticMeshComponents/InstancedStaticMeshComponents/HISM can be merged into a single component. */
USTRUCT()
struct ENGINE_API FISMComponentDescriptor
{
	GENERATED_USTRUCT_BODY()

	FISMComponentDescriptor();
#if WITH_EDITOR
	static FISMComponentDescriptor CreateFrom(const TSubclassOf<UStaticMeshComponent>& ComponentClass);
	void InitFrom(const UStaticMeshComponent* Component, bool bInitBodyInstance = true);

	uint32 ComputeHash() const;
	UInstancedStaticMeshComponent* CreateComponent(UObject* Outer, FName Name = NAME_None, EObjectFlags ObjectFlags = EObjectFlags::RF_NoFlags) const;
	void InitComponent(UInstancedStaticMeshComponent* ISMComponent) const;

	friend inline uint32 GetTypeHash(const FISMComponentDescriptor& Key)
	{
		if (Key.Hash == 0)
		{
			Key.ComputeHash();
		}
		return Key.Hash;
	}

	bool operator!=(const FISMComponentDescriptor& Other) const;
	bool operator==(const FISMComponentDescriptor& Other) const;

	friend inline bool operator<(const FISMComponentDescriptor& Lhs, const FISMComponentDescriptor& Rhs)
	{
		return Lhs.Hash < Rhs.Hash;
	}
#endif

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	mutable uint32 Hash = 0;

	UPROPERTY()
	TSubclassOf<UInstancedStaticMeshComponent> ComponentClass;
	
	UPROPERTY()
	UStaticMesh* StaticMesh = nullptr;
	
	UPROPERTY()
	TArray<UMaterialInterface*> OverrideMaterials;
	
	UPROPERTY()
	TArray<URuntimeVirtualTexture*> RuntimeVirtualTextures;

	UPROPERTY()
	TEnumAsByte<EComponentMobility::Type> Mobility;
		
	UPROPERTY()
	ERuntimeVirtualTextureMainPassType VirtualTextureRenderPassType;
	
	UPROPERTY()
	ELightmapType LightmapType;

	UPROPERTY()
	FLightingChannels LightingChannels;

	UPROPERTY()
	int32 RayTracingGroupId;

	UPROPERTY()
	ERayTracingGroupCullingPriority RayTracingGroupCullingPriority;

	UPROPERTY()
	TEnumAsByte<EHasCustomNavigableGeometry::Type> bHasCustomNavigableGeometry;

	UPROPERTY()
	ERendererStencilMask CustomDepthStencilWriteMask;

	UPROPERTY()
	FBodyInstance BodyInstance;
		
	UPROPERTY()
	int32 InstanceStartCullDistance;

	UPROPERTY()
	int32 InstanceEndCullDistance;

	UPROPERTY()
	int32 VirtualTextureCullMips;

	UPROPERTY()
	int32 TranslucencySortPriority;

	UPROPERTY()
	int32 OverriddenLightMapRes;

	UPROPERTY()
	int32 CustomDepthStencilValue;
	
	UPROPERTY()
	uint8 bCastShadow : 1;

	UPROPERTY()
	uint8 bCastDynamicShadow : 1;

	UPROPERTY()
	uint8 bCastStaticShadow : 1;

	UPROPERTY()
	uint8 bCastContactShadow : 1;

	UPROPERTY()
	uint8 bCastShadowAsTwoSided : 1;

	UPROPERTY()
	uint8 bAffectDynamicIndirectLighting : 1;

	UPROPERTY()
	uint8 bAffectDistanceFieldLighting : 1;

	UPROPERTY()
	uint8 bReceivesDecals : 1;

	UPROPERTY()
	uint8 bOverrideLightMapRes : 1;

	UPROPERTY()
	uint8 bUseAsOccluder : 1;

	UPROPERTY()
	uint8 bEnableDensityScaling : 1;

	UPROPERTY()
	uint8 bEnableDiscardOnLoad : 1;

	UPROPERTY()
	uint8 bRenderCustomDepth : 1;

	UPROPERTY()
	uint8 bIncludeInHLOD : 1;

	UPROPERTY()
	uint8 bVisibleInRayTracing : 1;

	UPROPERTY()
	uint8 bHiddenInGame : 1;

	UPROPERTY()
	uint8 bIsEditorOnly : 1;

	UPROPERTY()
	uint8 bVisible : 1;

	UPROPERTY()
	uint8 bConsiderForActorPlacementWhenHidden : 1;
#endif
};
