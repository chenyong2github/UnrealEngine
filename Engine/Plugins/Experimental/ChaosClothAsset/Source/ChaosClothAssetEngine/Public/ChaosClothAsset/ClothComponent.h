// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SkinnedMeshComponent.h"
#include "ClothingSystemRuntimeTypes.h"

#include "ClothComponent.generated.h"

class UChaosClothAsset;
class UChaosClothComponent;

namespace UE::Chaos::ClothAsset
{
	class FClothSimulationProxy;
}
/**
 * Cloth simulation component.
 */
UCLASS(ClassGroup = Physics, Meta = (BlueprintSpawnableComponent, ToolTip = "Chaos cloth simulation component."), DisplayName = "Cloth Simulation", HideCategories = (Object, "Mesh|SkeletalAsset", Physics, Constraints, Advanced, Cooking, Collision))
class CHAOSCLOTHASSETENGINE_API UChaosClothComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()
public:
	UChaosClothComponent(const FObjectInitializer& ObjectInitializer);
	UChaosClothComponent(FVTableHelper& Helper);
	~UChaosClothComponent();

	/** Set the cloth asset used by this component. */
	UFUNCTION(BlueprintCallable, Category = "Components|ClothAsset")
	void SetClothAsset(UChaosClothAsset* InClothAsset);

	/** Get the cloth asset used by this component. */
	UFUNCTION(BlueprintPure, Category = "Components|ClothAsset")
	UChaosClothAsset* GetClothAsset() const;

	/** Reset the teleport mode. */
	UFUNCTION(BlueprintCallable, Category = "Components|ClothAsset|Teleport")
	void ResetClothTeleportMode() { ClothTeleportMode = EClothingTeleportMode::None; }

	/**
	 * Used to indicate we should force 'teleport' during the next call to UpdateClothState,
	 * This will transform positions and velocities and thus keep the simulation state, just translate it to a new pose.
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|ClothAsset|Teleport")
	void ForceClothNextUpdateTeleport() { ClothTeleportMode = EClothingTeleportMode::Teleport; }

	/**
	 * Used to indicate we should force 'teleport and reset' during the next call to UpdateClothState.
	 * This can be used to reset it from a bad state or by a teleport where the old state is not important anymore.
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|ClothAsset|Teleport")
	void ForceClothNextUpdateTeleportAndReset() { ClothTeleportMode = EClothingTeleportMode::TeleportAndReset; }

	/** Return the current teleport mode currently requested if any. */
	EClothingTeleportMode GetClothTeleportMode() const { return ClothTeleportMode; }

protected:
	//~ Begin UObject Interface
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual bool RequiresPreEndOfFrameSync() const override;
	virtual void OnPreEndOfFrameSync() override;
	//~ End UActorComponent Interface

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End USceneComponent Interface

	//~ Begin USkinnedMeshComponent Interface
	virtual void RefreshBoneTransforms(FActorComponentTickFunction* TickFunction = nullptr) override;
	virtual void GetUpdateClothSimulationData_AnyThread(TMap<int32, FClothSimulData>& OutClothSimulData, FMatrix& OutLocalToWorld, float& OutClothBlendWeight) override;
	//~ End USkinnedMeshComponent Interface

private:
	void StartNewParallelSimulation(float DeltaTime);
	void HandleExistingParallelSimulation();
	bool ShouldWaitForClothInTickFunction() const;
	bool IsSimulationSuspended() const;

#if WITH_EDITORONLY_DATA
	/** Cloth asset used by this component. */
	UE_DEPRECATED(5.1, "This property isn't deprecated, but getter and setter must be used at all times to preserve correct operations.")
	UPROPERTY(EditAnywhere, Transient, BlueprintSetter = SetClothAsset, BlueprintGetter = GetClothAsset, Category = Mesh)
	TObjectPtr<UChaosClothAsset> ClothAsset;
#endif

	UPROPERTY()
	uint8 bDisableClothSimulation : 1;

	UPROPERTY()
	uint8 bSuspendSimulation : 1;

	UPROPERTY()
	uint8 bWaitForParallelClothTask : 1;

	UPROPERTY()
	uint8 bBindClothToLeaderComponent : 1;

	UPROPERTY()
	float ClothBlendWeight = 1.f;

	EClothingTeleportMode ClothTeleportMode = EClothingTeleportMode::None;

	TUniquePtr<UE::Chaos::ClothAsset::FClothSimulationProxy> ClothSimulationProxy;
};
