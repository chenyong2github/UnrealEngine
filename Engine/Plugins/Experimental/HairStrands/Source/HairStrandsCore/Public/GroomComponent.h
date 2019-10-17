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

	/** 
	 * When activated, the hair groom will be attached and skinned onto the mesh, if the groom component is a child of a skeletal/skinned component.
	 * This requires the following projection settings: 
	 * - Rendering settings: 'Skin cache' enabled
	 * - Animation settings: 'Tick Animation On Skeletal Mesh Init' disabled
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HairStrands Rendering")
	bool bSkinGroom;

	/** Scale the hair geometry radius for ray tracing effects (e.g. shadow) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HairStrands Rendering", meta = (ClampMin = "0.0001", UIMin = "0.01", UIMax = "10.0"))
	float HairRaytracingRadiusScale;

	//~ Begin UActorComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void SendRenderTransform_Concurrent() override;
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
	FHairStrandsDatas* GetGuideStrandsDatas(uint32 GroupIndex);

	/** Return the guide hairs rest resources*/
	FHairStrandsRestResource* GetGuideStrandsRestResource(uint32 GroupIndex);

	/** Return the guide hairs deformed resources*/
	FHairStrandsDeformedResource* GetGuideStrandsDeformedResource(uint32 GroupIndex);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const UProperty* InProperty) const override;
#endif

	struct FHairGroupResource
	{
		// Sim to rendering interpolation resources
		FHairStrandsInterpolationResource* InterpolationResource = nullptr;

		// Projection resources
		struct FHairStrandsRootResource* RenRootResources = nullptr;
		struct FHairStrandsRootResource* SimRootResources = nullptr;
	#if RHI_RAYTRACING
		FHairStrandsRaytracingResource* RaytracingResources = nullptr;
	#endif

		// Deformed position
		FHairStrandsDeformedResource* RenderDeformedResources = nullptr;
		FHairStrandsDeformedResource* SimDeformedResources = nullptr;

		// Rest resources, owned by the asset
		FHairStrandsRestResource* RenderRestResources = nullptr;
		FHairStrandsRestResource* SimRestResources = nullptr;
	};
	typedef TArray<FHairGroupResource> FHairGroupResources;

	FHairGroupResources HairGroupResources;
	struct FHairStrandsInterpolationOutput* InterpolationOutput = nullptr;
	struct FHairStrandsInterpolationInput* InterpolationInput = nullptr;
private:
	void* InitializedResources;

	enum class EMeshProjectionState
	{
		Invalid,
		WaitForData,
		Completed
	};
	class USkeletalMeshComponent* RegisteredSkeletalMeshComponent;
	int32 MeshProjectionLODIndex;
	uint32 MeshProjectionTickDelay;
	EMeshProjectionState MeshProjectionState;

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
