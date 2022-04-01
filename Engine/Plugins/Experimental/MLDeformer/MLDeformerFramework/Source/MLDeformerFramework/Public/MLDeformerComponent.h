// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Components/ActorComponent.h"
#include "MLDeformerModelInstance.h"
#include "MLDeformerComponent.generated.h"

class UMLDeformerAsset;
class USkeletalMeshComponent;

namespace UE::MLDeformer
{
	class FMLDeformerModelInstance;
}

/**
 * The ML mesh deformer component.
 * This works in combination with a MLDeformerAsset and SkeletalMeshComponent.
 * The component will perform runtime inference of the deformer model setup inside the asset.
 */
UCLASS(Blueprintable, ClassGroup = Component, BlueprintType, meta = (BlueprintSpawnableComponent))
class MLDEFORMERFRAMEWORK_API UMLDeformerComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:
	// UActorComponent overrides.
	void Activate(bool bReset=false) override;
	void Deactivate() override;
	// ~END UActorComponent overrides.

	UFUNCTION(BlueprintCallable, Category = "MLDeformer")
	void SetupComponent(UMLDeformerAsset* InDeformerAsset, USkeletalMeshComponent* InSkelMeshComponent);

	UMLDeformerAsset* GetDeformerAsset() { return DeformerAsset; }
	void SetDeformerAsset(UMLDeformerAsset* InDeformerAsset) { DeformerAsset = InDeformerAsset; }
	float GetWeight() const { return Weight; }
	void SetWeight(float NormalizedWeightValue) { Weight = NormalizedWeightValue;  }
	UE::MLDeformer::FMLDeformerModelInstance* GetModelInstance() const { return ModelInstance.Get(); }
	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkelMeshComponent.Get(); }

protected:
	// AActorComponent overrides.
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// ~END AActorComponent overrides.

	void Init();

	/** Bind to the DeformerAsset NeuralNetworkModifyDelegate. */
	void AddNeuralNetworkModifyDelegate();

	/** Unbind from the DeformerAsset NeuralNetworkModifyDelegate. */
	void RemoveNeuralNetworkModifyDelegate();

protected:
	/** The deformer asset to use. */
	UPROPERTY(EditAnywhere, Category = "ML Deformer")
	TObjectPtr<UMLDeformerAsset> DeformerAsset = nullptr;

	/** How active is this deformer? Can be used to blend it in and out. */
	UPROPERTY(EditAnywhere, Category = "ML Deformer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.0f;

	/** 
	 * The skeletal mesh component we want to grab the bone transforms etc from. 
	 * This can be a nullptr. When it is a nullptr then it will internally try to find the first skeletal mesh component on the actor.
	 * You can see this as an override. You can specify this override through the SetupComponent function.
	 */
	TObjectPtr<USkeletalMeshComponent> SkelMeshComponent = nullptr;

	/** The deformation model instance. This is used to perform the runtime updates and run the inference. */
	TUniquePtr<UE::MLDeformer::FMLDeformerModelInstance> ModelInstance = nullptr;

	/** DelegateHandle for NeuralNetwork modification. */
	FDelegateHandle NeuralNetworkModifyDelegateHandle;
};
