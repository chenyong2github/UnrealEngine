// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "VirtualHeightfieldMeshComponent.generated.h"

/** Component to render a heightfield mesh using a virtual texture heightmap. */
UCLASS(ClassGroup = Rendering, hideCategories = (Activation, Collision, Cooking, HLOD, Mobility, Object, Physics, VirtualTexture))
class VIRTUALHEIGHTFIELDMESH_API UVirtualHeightfieldMeshComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

public:
	/** The material to apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Rendering)
	class UMaterialInterface* Material = nullptr;

	/** LOD scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = LOD, meta = (ClampMin = "0.01"))
	float LODDistanceScale = 1.f;

	/** The runtime virtual texture that contains height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Heightmap, meta = (DisplayName = "Virtual Texture Heightmap"))
	class URuntimeVirtualTexture* RuntimeVirtualTexture = nullptr;

	/** Texture object containing min and max height for each virtual texture page. This texture is generated and updated by a build step for the virtual texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Heightmap)
	class UTexture2D* MinMaxTexture = nullptr;

	/** The number of Lod levels that we calculate occlusion volumes for. A higher number gives finer occlusion at the cost of more queries. */
	UPROPERTY(EditAnywhere, Category = Heightmap, meta = (ClampMin = "0", ClampMax = "5"))
	int32 NumOcclusionLods = 0;

protected:
	/** The MinMax height values stored for occlusion. */
	UPROPERTY()
	TArray<FVector2D> BuiltOcclusionData;
	/** The number of Lods stored in BuiltOcclusionData. This can be less then NumOcclusionLods if NumOcclusionLods is greater than the number of mips in MinMaxTexture. */
	UPROPERTY()
	int32 NumBuiltOcclusionLods = 0;

public:
	/** */
	TArray<FVector2D> const& GetOcclusionData() const { return BuiltOcclusionData; }
	/** */
	int32 GetNumOcclusionLods() const { return  NumBuiltOcclusionLods; }

#if WITH_EDITOR
	/** Rebuild the stored occlusion data from the currently set MinMaxTexture. */
	void BuildOcclusionData();
#endif

protected:
	//~ Begin UPrimitiveComponent Interface
	virtual bool IsVisible() const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UPrimitiveComponent Interface
};
