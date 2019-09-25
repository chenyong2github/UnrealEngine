// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Components/MeshComponent.h"
#include "GroomAsset.h"
#include "RHIDefinitions.h"

#include "GroomComponent.generated.h"

/** Component that allows you to specify custom triangle mesh geometry */
UCLASS(HideCategories = (Object, Physics, Activation, Mobility, "Components|Activation"), editinlinenew, meta = (BlueprintSpawnableComponent), ClassGroup = Rendering)
class HAIRSTRANDSCORE_API UGroomComponent : public UMeshComponent
{
	GENERATED_UCLASS_BODY()

public:

	/** Hair strand asset used for rendering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom")
	UGroomAsset* GroomAsset;

	/** Controls the hair density, to reduce or increase hair count during shadow rendering. This allows to increase/decrease the shadowing on hair when the number of strand is not realistic */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HairStrands Rendering", meta = (ClampMin = "0.0001", UIMin = "0.01", UIMax = "10.0"))
	float HairDensity;

	/** Threshold for merging consecutive hair segments when loading asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HairStrands Rendering", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "1.0"))
	float MergeThreshold;

	//~ Begin UActorComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	//~ End UActorComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End USceneComponent Interface.

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual void PostLoad() override;
	virtual int32 GetNumMaterials() const override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	//~ End UMeshComponent Interface.

	/** Return the guide hairs datas */
	FHairStrandsDatas* GetGuideStrandsDatas() { return GroomAsset ? &GroomAsset->HairSimulationData : nullptr; }

	/** Return the guide hairs resources*/
	FHairStrandsResource* GetGuideStrandsResource() { return GroomAsset ? GroomAsset->HairSimulationResource : nullptr; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	// #hair_todo: cached/serialize interpolation data
	FHairStrandsInterpolationResource* InterpolationResource = nullptr;
	struct FHairStrandsInterpolationOutput* InterpolationOutput = nullptr;
	struct FHairStrandsInterpolationInput* InterpolationInput = nullptr;

#if RHI_RAYTRACING
	FHairStrandsRaytracingResource* RaytracingResources = nullptr;
#endif

private:
	void* InitializedResources;

	void InitResources();
	void ReleaseResources();

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const override;

	friend class FGroomComponentRecreateRenderStateContext;
};

/** Used to recreate render context for all GroomComponents that use a given GroomAsset */
class HAIRSTRANDSCORE_API FGroomComponentRecreateRenderStateContext
{
public:
	FGroomComponentRecreateRenderStateContext(UGroomAsset* GroomAsset);
	~FGroomComponentRecreateRenderStateContext();

private:
	TArray<UGroomComponent*> GroomComponents;
};
