// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "VirtualHeightfieldMeshComponent.generated.h"

/** Component to render a heightfield mesh using a virtual texture heightmap. */
UCLASS(Blueprintable, ClassGroup = Rendering, hideCategories = (Activation, Collision, Cooking, HLOD, Navigation, Mobility, Object, Physics, VirtualTexture))
class VIRTUALHEIGHTFIELDMESH_API UVirtualHeightfieldMeshComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

protected:
	/** The RuntimeVirtualTextureComponent that contains virtual texture heightmap. */
	UPROPERTY(EditAnywhere, Category = Heightfield)
	TSoftObjectPtr<class ARuntimeVirtualTextureVolume> VirtualTexture;

	/** The material to apply. */
	UPROPERTY(EditAnywhere, Category = Rendering)
	class UMaterialInterface* Material = nullptr;

	/** Factor used to scale the LOD ranges. A smaller value increases the geometry resolution. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "LOD Scale", ClampMin = "0.01", UIMin = "0.01"))
	float LodRangeScale = 1.f;

	/** Factor used to bias the LOD ranges. A larger value pushes back the distance to the first LOD transition. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "LOD Bias", ClampMin = "0.0", UIMin = "0.0"))
	float LodRangeBias = 0.f;

	/** The number of levels of geometry subdivision to apply before the LOD 0 from the source virtual texture. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "Subdivision LODs", ClampMin = "0", ClampMax = "8", UIMin = "0", UIMax = "8"))
	int32 NumSubdivisionLods = 0;

	/** The number of levels of geometry reduction to apply after the Max LOD from the source virtual texture. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "Tail LODs", ClampMin = "0", ClampMax = "8", UIMin = "0", UIMax = "8"))
	int32 NumTailLods = 0;

	/** The number of Lod levels that we calculate occlusion volumes for. A higher number gives finer occlusion at the cost of more queries. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "Occlusion LODs", ClampMin = "0", ClampMax = "5"))
	int32 NumOcclusionLods = 0;

	/** The number of Lods stored in BuiltOcclusionData. This can be less then NumOcclusionLods if NumOcclusionLods is greater than the number of mips in MinMaxTexture. */
	UPROPERTY()
	int32 NumBuiltOcclusionLods = 0;

	/** The MinMax height values stored for occlusion. */
	UPROPERTY()
	TArray<FVector2D> BuiltOcclusionData;
	
public:
	/** */
	ARuntimeVirtualTextureVolume* GetVirtualTextureVolume() const;
	/** */
	FTransform GetVirtualTextureTransform() const;
	/** */
	class URuntimeVirtualTexture* GetVirtualTexture() const;
	/** */
	class UTexture2D* GetMinMaxTexture() const;
	/** */
	virtual UMaterialInterface* GetMaterial(int32 Index) const override { return Material; }
	/** */
	float GetLodRamgeScale() const { return LodRangeScale; }
	/** */
	float GetLodRamgeBias() const { return LodRangeBias; }
	/** */
	int32 GetNumSubdivisionLods() const { return NumSubdivisionLods; }
	/** */
	int32 GetNumTailLods() const { return NumTailLods; }
	/** */
	int32 GetNumOcclusionLods() const { return NumBuiltOcclusionLods; }
	/** */
	TArray<FVector2D> const& GetOcclusionData() const { return BuiltOcclusionData; }

protected:
#if WITH_EDITOR
	/** Rebuild the stored occlusion data from the currently set MinMaxTexture. */
	void BuildOcclusionData();

	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif

	//~ Begin USceneComponent Interface
	virtual bool IsVisible() const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ EndUSceneComponent Interface

	//~ Begin UPrimitiveComponent Interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual bool SupportsStaticLighting() const override { return true; }
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	//~ End UPrimitiveComponent Interface
};
