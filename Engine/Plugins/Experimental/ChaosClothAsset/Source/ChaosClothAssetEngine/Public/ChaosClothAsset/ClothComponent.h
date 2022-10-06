// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SkinnedMeshComponent.h"

#include "ClothComponent.generated.h"

class UChaosClothAsset;

/**
 * Cloth simulation component.
 */
UCLASS(ClassGroup = Physics, hidecategories = (Object, "Mesh|SkeletalAsset"), meta = (BlueprintSpawnableComponent, ToolTip = "Chaos cloth simulation component."), DisplayName = "Cloth Simulation", HideCategories = (Physics, Constraints, Advanced, Cooking, Collision))
class CHAOSCLOTHASSETENGINE_API UChaosClothComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()
public:
	UChaosClothComponent(const FObjectInitializer& ObjectInitializer);

	/** Set the cloth asset used by this component. */
	UFUNCTION(BlueprintCallable, Category = "Components|ClothAsset")
	void SetClothAsset(UChaosClothAsset* InClothAsset);

	/** Get the cloth asset used by this component. */
	UFUNCTION(BlueprintPure, Category = "Components|ClothAsset")
	UChaosClothAsset* GetClothAsset() const;

protected:
	//~ Begin UObject Interface
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	//~ End UActorComponent Interface

	//~ Begin USkinClothComponent Interface
	virtual void RefreshBoneTransforms(FActorComponentTickFunction* TickFunction = nullptr) override;
	//~ End USkinClothComponent Interface

private:
#if WITH_EDITORONLY_DATA
	/** Cloth asset used by this component. */
	UE_DEPRECATED(5.1, "This property isn't deprecated, but getter and setter must be used at all times to preserve correct operations.")
	UPROPERTY(EditAnywhere, Transient, BlueprintSetter = SetClothAsset, BlueprintGetter = GetClothAsset, Category = Mesh)
	TObjectPtr<UChaosClothAsset> ClothAsset;
#endif
};
