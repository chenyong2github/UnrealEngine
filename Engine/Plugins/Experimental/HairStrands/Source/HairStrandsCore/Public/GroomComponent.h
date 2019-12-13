// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Components/MeshComponent.h"
#include "GroomAsset.h"
#include "RHIDefinitions.h"

#include "GroomComponent.generated.h"

USTRUCT(BlueprintType)
struct FHairGroupDesc
{
	GENERATED_USTRUCT_BODY()

	/** Number of hairs within this hair group.  */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Groom")
	int32 HairCount;

	/** Number of simulation guides within this hair group. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Groom")
	int32 GuideCount;

	/** Override the hair width (in centimeters) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "1.0", SliderExponent = 6))
	float HairWidth;

	/** Override the hair shadow density factor (unit less).  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "10.0", SliderExponent = 6))
	float HairShadowDensity;

	/** Scale the hair geometry radius for ray tracing effects (e.g. shadow) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "10.0", SliderExponent = 6))
	float HairRaytracingRadiusScale;
};

UCLASS(HideCategories = (Object, Physics, Activation, Mobility, "Components|Activation"), editinlinenew, meta = (BlueprintSpawnableComponent), ClassGroup = Rendering)
class HAIRSTRANDSCORE_API UGroomComponent : public UMeshComponent
{
	GENERATED_UCLASS_BODY()

public:

	/** Groom asset . */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom")
	UGroomAsset* GroomAsset;

	/** 
	 * When activated, the groom will be attached and skinned onto the skeletal mesh, if the groom component is a child of a skeletal/skinned component.
	 * This requires the following projection settings: 
	 * - Rendering settings: 'Skin cache' enabled
	 * - Animation settings: 'Tick Animation On Skeletal Mesh Init' disabled
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Groom")
	bool bBindGroomToSkeletalMesh;

	/** Boolean to check when animation has been loaded */
	bool bResetSimulation;

	/** Listen for the animation event to trigger the sim */
	UFUNCTION()
	void ResetSimulation();

	//~ Begin UActorComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void OnAttachmentChanged() override;
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

	/** Return the guide hairs root resources*/
	FHairStrandsRootResource* GetGuideStrandsRootResource(uint32 GroupIndex);

#if WITH_EDITOR
	virtual void CheckForErrors() override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	void ValidateMaterials(bool bMapCheck) const;
	void Invalidate();
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

	/** Groom's groups info. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom")
	TArray<FHairGroupDesc> GroomGroupsDesc;

	FHairGroupResources HairGroupResources;
	struct FHairStrandsInterpolationOutput* InterpolationOutput = nullptr;
	struct FHairStrandsInterpolationInput* InterpolationInput = nullptr;

protected:
	// Used for tracking if a Niagara component is attached or not
	virtual void OnChildAttached(USceneComponent* ChildComponent) override;
	virtual void OnChildDetached(USceneComponent* ChildComponent) override;

private:
	void* InitializedResources;

	enum class EMeshProjectionState
	{
		Invalid,
		InProgressBinding,
		WaitForRestPose,
		Completed
	};
	class USkeletalMeshComponent* RegisteredSkeletalMeshComponent;
	FVector SkeletalPreviousPositionOffset;
	int32 MeshProjectionLODIndex;
	uint32 MeshProjectionTickDelay;
	EMeshProjectionState MeshProjectionState;
	bool bIsGroomAssetCallbackRegistered;
	
	struct FSkeletalMeshConfiguration
	{
		int32 ForceLOD = -1;
		bool ForceRefPose = false;
		static bool Equals(const FSkeletalMeshConfiguration& A, const FSkeletalMeshConfiguration& B)
		{
			return A.ForceLOD == B.ForceLOD && A.ForceRefPose == B.ForceRefPose;
		}
	};
	FSkeletalMeshConfiguration SkeletalMeshConfiguration;

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
