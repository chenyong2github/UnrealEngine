// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "VirtualHeightfieldMeshComponent.generated.h"

/** Component to render a heightfield mesh using a virtual texture heightmap. */
UCLASS(ClassGroup = Rendering, hideCategories = (Activation, Collision, Cooking, HLOD, Navigation, Mobility, Object, Physics, VirtualTexture))
class VIRTUALHEIGHTFIELDMESH_API UVirtualHeightfieldMeshComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

protected:
	/** The RuntimeVirtualTextureComponent that contains virtual texture heightmap. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "Virtual Texture Heightmap", UseComponentPicker, AllowAnyActor, AllowedClasses = "RuntimeVirtualTextureComponent"))
	FComponentReference VirtualTexture;

	/** The material to apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Rendering)
	class UMaterialInterface* Material = nullptr;

	/** The number of Lod levels that we calculate occlusion volumes for. A higher number gives finer occlusion at the cost of more queries. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "0", ClampMax = "5"))
	int32 NumOcclusionLods = 0;

	/** LOD scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = LOD, meta = (ClampMin = "0.01"))
	float LODDistanceScale = 1.f;

	/** The MinMax height values stored for occlusion. */
	UPROPERTY()
	TArray<FVector2D> BuiltOcclusionData;
	
	/** The number of Lods stored in BuiltOcclusionData. This can be less then NumOcclusionLods if NumOcclusionLods is greater than the number of mips in MinMaxTexture. */
	UPROPERTY()
	int32 NumBuiltOcclusionLods = 0;

public:
	/** */
	class URuntimeVirtualTexture* GetVirtualTexture() const;
	/** */
	class UTexture2D* GetMinMaxTexture() const;
	/** */
	virtual UMaterialInterface* GetMaterial(int32 Index) const override { return Material; }
	/** */
	float GetLODDistanceScale() const { return LODDistanceScale; }
	/** */
	TArray<FVector2D> const& GetOcclusionData() const { return BuiltOcclusionData; }
	/** */
	int32 GetNumOcclusionLods() const { return  NumBuiltOcclusionLods; }

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
