// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "VirtualHeightfieldMeshComponent.generated.h"

class ARuntimeVirtualTextureVolume;
class UHeightfieldMinMaxTexture;
class UMaterialInterface;
class URuntimeVirtualTexture;

/** Component to render a heightfield mesh using a virtual texture heightmap. */
UCLASS(Blueprintable, ClassGroup = Rendering, hideCategories = (Activation, Collision, Cooking, HLOD, Navigation, Mobility, Object, Physics, VirtualTexture))
class VIRTUALHEIGHTFIELDMESH_API UVirtualHeightfieldMeshComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

protected:
	/** The RuntimeVirtualTextureVolume that contains virtual texture heightmap. */
	UPROPERTY(EditAnywhere, Category = Heightfield)
	TSoftObjectPtr<ARuntimeVirtualTextureVolume> VirtualTexture;
	
	/** UObject ref resolved from VirtualTexture weak ref. */
	UPROPERTY(Transient)
	ARuntimeVirtualTextureVolume* VirtualTextureRef;

	/** Placeholder for details customization image. */
	UPROPERTY(VisibleAnywhere, Transient, Category = Heightfield)
	UObject* VirtualTextureThumbnail = nullptr;

	/** Placeholder for details customization button. */
	UPROPERTY(VisibleAnywhere, Transient, Category = Heightfield)
	bool bCopyBoundsButton;

	/** Texture object containing minimum and maximum height values. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = HeightfieldBuild, meta = (DisplayName = "MinMax Texture"))
	UHeightfieldMinMaxTexture* MinMaxTexture = nullptr;

	/** Number of levels to build in the MinMax Texture. A default value of 0 will build all levels from the heightfield. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = HeightfieldBuild, meta = (DisplayName = "MinMax Build Levels", UIMin = "0"))
	int32 NumMinMaxTextureBuildLevels = 0;

	/** Placeholder for details customization button. */
	UPROPERTY(VisibleAnywhere, Transient, Category = HeightfieldBuild)
	bool bBuildMinMaxTextureButton;

	/** The material to apply. */
	UPROPERTY(EditAnywhere, Category = Rendering)
	UMaterialInterface* Material = nullptr;

	/** Target sceen size for LOD 0. A larger value uniformly increases the geometry resolution on screen. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "LOD 0 Screen Size", ClampMin = "0.1", UIMin = "0.1"))
	float Lod0ScreenSize = 1.f;

	/** Distribution multiplier applied only for LOD 0. A larger value increases the distance to the first LOD transition. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "LOD 0 Distance Scale", ClampMin = "1.0", UIMin = "1.0"))
	float Lod0Distribution = 1.f;

	/** Distribution multiplier applied for each LOD level. A larger value increases the distance exponentially between each LOD transition. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "LOD Distribution", ClampMin = "1.0", UIMin = "1.0"))
	float LodDistribution = 2.f;

	/** The number of levels of geometry subdivision to apply before the LOD 0 from the source virtual texture. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "Subdivision LODs", ClampMin = "0", ClampMax = "8", UIMin = "0", UIMax = "8"))
	int32 NumSubdivisionLods = 0;

	/** The number of levels of geometry reduction to apply after the Max LOD from the source virtual texture. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "Tail LODs", ClampMin = "0", ClampMax = "8", UIMin = "0", UIMax = "8"))
	int32 NumTailLods = 0;

	/** The number of levels that we calculate occlusion volumes for. A higher number gives finer occlusion at the cost of more queries. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "Occlusion LODs", ClampMin = "0", ClampMax = "5"))
	int32 NumOcclusionLods = 0;

	/** Allows us to only see this actor in game and not in the Editor. This is useful if we only want to see the Heightfield virtual texture source primitives during edition. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (DisplayName = "Actor Hidden In Editor", DisplayAfter = "bHiddenInGame"))
	bool bHiddenInEditor = true;

public:
	/** Get the HiddenInEditor flag  on this component. */
	bool GetHiddenInEditor() const { return bHiddenInEditor; }

	/** Get the associated runtime virtual texture volume. Can return nullptr if the volume is from an unloaded level. */
	ARuntimeVirtualTextureVolume* GetVirtualTextureVolume() const;
	/** Get the associated runtime virtual texture transform including any texel snap offset. */
	FTransform GetVirtualTextureTransform() const;
	/** Get the associated runtime virtual texture. */
	URuntimeVirtualTexture* GetVirtualTexture() const;
	
	/** Returns true if a MinMax height texture is relevant for this virtual texture type. */
	bool IsMinMaxTextureEnabled() const;
	/** Get the MinMax height texture on this component. */
	UHeightfieldMinMaxTexture* GetMinMaxTexture() const { return MinMaxTexture; }
	/** Get the number of levels to build in the MinMax Texture. */
	int32 GetNumMinMaxTextureBuildLevels() { return NumMinMaxTextureBuildLevels; }

#if WITH_EDITOR
	/** Set a new asset to hold the MinMax height texture. This should only be called directly before setting data to the new asset. */
	void SetMinMaxTexture(UHeightfieldMinMaxTexture* InTexture) { MinMaxTexture = InTexture; }
	/** Initialize the MinMax height texture with the passed in size and data. */
	void InitializeMinMaxTexture(uint32 InSizeX, uint32 InSizeY, uint32 InNumMips, uint8* InData);
#endif

	UMaterialInterface* GetMaterial() const { return Material; }
	float GetLod0ScreenSize() const { return Lod0ScreenSize; }
	float GetLod0Distribution() const { return Lod0Distribution; }
	float GetLodDistribution() const { return LodDistribution; }
	int32 GetNumSubdivisionLods() const { return NumSubdivisionLods; }
	int32 GetNumTailLods() const { return NumTailLods; }
	int32 GetNumOcclusionLods() const { return NumOcclusionLods; }

protected:
	/** Function used by the VirtualTexture delegate to retrieve our HidePrimitives flags. */
	UFUNCTION()
	void GatherHideFlags(bool& InOutHidePrimitivesInEditor, bool& InOutHidePrimitivesInGame) const;

	//~ Begin UObject Interface.
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface.

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	//~ Begin USceneComponent Interface
	virtual bool IsVisible() const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ EndUSceneComponent Interface

	//~ Begin UPrimitiveComponent Interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual bool SupportsStaticLighting() const override { return true; }
	virtual UMaterialInterface* GetMaterial(int32 Index) const override { return Material; }
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	//~ End UPrimitiveComponent Interface
};
