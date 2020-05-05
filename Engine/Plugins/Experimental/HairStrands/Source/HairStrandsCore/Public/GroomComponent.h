// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Components/MeshComponent.h"
#include "GroomAsset.h"
#include "RHIDefinitions.h"
#include "GroomDesc.h"

#include "GroomComponent.generated.h"

UCLASS(HideCategories = (Object, Physics, Activation, Mobility, "Components|Activation"), editinlinenew, meta = (BlueprintSpawnableComponent), ClassGroup = Rendering)
class HAIRSTRANDSCORE_API UGroomComponent : public UMeshComponent
{
	GENERATED_UCLASS_BODY()

public:

	/** Groom asset . */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom")
	UGroomAsset* GroomAsset;

	/** Niagara components that will be attached to the system*/
	UPROPERTY(Transient)
	TArray<class UNiagaraComponent*> NiagaraComponents;

	/** 
	 * When activated, the groom will be attached and skinned onto the skeletal mesh, if the groom component is a child of a skeletal/skinned component.
	 * This requires the following projection settings: 
	 * - Rendering settings: 'Skin cache' enabled
	 * - Animation settings: 'Tick Animation On Skeletal Mesh Init' disabled
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Groom")
	bool bBindGroomToSkeletalMesh;

	/** Skeletal mesh on which the groom has been authored. If not provided, the skeletal mesh on which the groom component is attached will be used. If provided, both skeletal mesh needs to share the same topology. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Groom")
	class USkeletalMesh* SourceSkeletalMesh;

	/** Optional binding asset for binding a groom onto a skeletal mesh. If the binding asset is not provided the projection is done at runtime, which implies a large GPU cost at startup time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Groom")
	class UGroomBindingAsset* BindingAsset;

	UPROPERTY()
	UMaterialInterface* HairDebugMaterial;

	UPROPERTY()
	UMaterialInterface* HairDefaultMaterial;

	/** Boolean to check when the simulation should be reset */
	bool bResetSimulation;

	/** Boolean to check when the simulation should be initialized */
	bool bInitSimulation;

	/** Previous bone matrix to compare the difference and decide to reset or not the simulation */
	FMatrix	PrevBoneMatrix;

	/** Update Niagara components */
	void UpdateHairSimulation();

	/** Release Niagara components */
	void ReleaseHairSimulation();

	/** Update Group Description */
	void UpdateHairGroupsDesc();

	/** Update simulated groups */
	void UpdateSimulatedGroups();

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
	FHairStrandsRestRootResource* GetGuideStrandsRestRootResource(uint32 GroupIndex);
	FHairStrandsDeformedRootResource* GetGuideStrandsDeformedRootResource(uint32 GroupIndex);

#if WITH_EDITOR
	virtual void CheckForErrors() override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	void ValidateMaterials(bool bMapCheck) const;
	void Invalidate();
	void InvalidateAndRecreate();
#endif

	void SetStableRasterization(bool bEnable);
	void SetGroomAsset(UGroomAsset* Asset);
	void SetGroomAsset(UGroomAsset* Asset, UGroomBindingAsset* InBinding);
	void SetHairLengthScale(float Scale);
	void SetHairRootScale(float Scale);
	void SetHairWidth(float HairWidth);
	void SetScatterSceneLighting(bool Enable);
	void SetBinding(bool bBind);
	void SetBinding(UGroomBindingAsset* InBinding);

	/** Groom's groups info. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom")
		TArray<FHairGroupDesc> GroomGroupsDesc;

private:

	struct FHairGroupResource
	{
		// Sim to rendering interpolation resources
		FHairStrandsInterpolationResource* InterpolationResource = nullptr;

		// Projection resources
		bool bOwnRootResourceAllocation = true;
		struct FHairStrandsRestRootResource* RenRestRootResources = nullptr;
		struct FHairStrandsRestRootResource* SimRestRootResources = nullptr;

		struct FHairStrandsDeformedRootResource* RenDeformedRootResources = nullptr;
		struct FHairStrandsDeformedRootResource* SimDeformedRootResources = nullptr;

	#if RHI_RAYTRACING
		FHairStrandsRaytracingResource* RaytracingResources = nullptr;
	#endif

		// Deformed position
		FHairStrandsDeformedResource* RenderDeformedResources = nullptr;
		FHairStrandsDeformedResource* SimDeformedResources = nullptr;

		FHairStrandsClusterCullingResource* ClusterCullingResources = nullptr; // TODO merge into FHairGroupPublicData
		FHairGroupPublicData* HairGroupPublicDatas = nullptr;

		// Rest resources, owned by the asset
		FHairStrandsRestResource* RenderRestResources = nullptr;
		FHairStrandsRestResource* SimRestResources = nullptr;
	};

	struct FHairGroupResources
	{
		TArray<FHairGroupResource> HairGroups;
	};
	static void DeleteHairGroupResources(FHairGroupResources*& InHairGroupResources);

	FHairGroupResources* HairGroupResources = nullptr;
	struct FHairStrandsInterpolationOutput* InterpolationOutput = nullptr;
	struct FHairStrandsInterpolationInput* InterpolationInput = nullptr;

protected:
	// Used for tracking if a Niagara component is attached or not
	virtual void OnChildAttached(USceneComponent* ChildComponent) override;
	virtual void OnChildDetached(USceneComponent* ChildComponent) override;

private:
	void* InitializedResources;
	class USkeletalMeshComponent* RegisteredSkeletalMeshComponent;
	FVector SkeletalPreviousPositionOffset;
	bool bIsGroomAssetCallbackRegistered;
	bool bIsGroomBindingAssetCallbackRegistered;

	EWorldType::Type GetWorldType() const; 
	void InitResources(bool bIsBindingReloading=false);
	void ReleaseResources();
	void UpdateHairGroupsDescAndInvalidateRenderState();

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const override;

	friend class FGroomComponentRecreateRenderStateContext;
	friend class FHairStrandsSceneProxy;
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
