// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Components/ActorComponent.h"
#include "MLDeformerComponent.generated.h"

class UMLDeformerAsset;
class USkeletalMeshComponent;
class UMLDeformerModelInstance;

/**
 * The ML mesh deformer component.
 * This works in combination with a MLDeformerAsset and SkeletalMeshComponent.
 * The component will perform runtime inference of the deformer model setup inside the asset.
 */
UCLASS(Blueprintable, ClassGroup = Component, BlueprintType, meta = (BlueprintSpawnableComponent))
class MLDEFORMERFRAMEWORK_API UMLDeformerComponent
	: public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:
	// UObject overrides.
	void BeginDestroy() override;
	// ~END UObject overrides.

	// UActorComponent overrides.
	void Activate(bool bReset=false) override;
	void Deactivate() override;
	// ~END UActorComponent overrides.

	/** Setup the ML Deformer, by picking the deformer asset and skeletal mesh component. */
	UFUNCTION(BlueprintCallable, Category = "MLDeformer")
	void SetupComponent(UMLDeformerAsset* InDeformerAsset, USkeletalMeshComponent* InSkelMeshComponent);

#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UMLDeformerAsset* GetDeformerAsset() { return DeformerAsset; }
	void SetDeformerAsset(UMLDeformerAsset* InDeformerAsset) { DeformerAsset = InDeformerAsset; }
	float GetWeight() const { return Weight; }
	void SetWeight(float NormalizedWeightValue) { Weight = NormalizedWeightValue;  }
	UMLDeformerModelInstance* GetModelInstance() const { return ModelInstance; }
	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkelMeshComponent.Get(); }

protected:
	// AActorComponent overrides.
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// ~END AActorComponent overrides.

	void Init();

	/** Bind to the MLDeformerModel's NeuralNetworkModifyDelegate. */
	void AddNeuralNetworkModifyDelegate();

	/** Unbind from the MLDeformerModel's NeuralNetworkModifyDelegate. */
	void RemoveNeuralNetworkModifyDelegate();

protected:
	/** 
	 * The skeletal mesh component we want to grab the bone transforms etc from. 
	 * This can be a nullptr. When it is a nullptr then it will internally try to find the first skeletal mesh component on the actor.
	 * You can see this as an override. You can specify this override through the SetupComponent function.
	 */
	TObjectPtr<USkeletalMeshComponent> SkelMeshComponent = nullptr;

	/** DelegateHandle for NeuralNetwork modification. */
	FDelegateHandle NeuralNetworkModifyDelegateHandle;

public:
	/** The deformer asset to use. */
	UPROPERTY(EditAnywhere, DisplayName = "ML Deformer Asset", Category = "ML Deformer")
	TObjectPtr<UMLDeformerAsset> DeformerAsset = nullptr;

	/** How active is this deformer? Can be used to blend it in and out. */
	UPROPERTY(EditAnywhere, Category = "ML Deformer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.0f;

	/** The deformation model instance. This is used to perform the runtime updates and run the inference. */
	UPROPERTY(Transient)
	TObjectPtr<UMLDeformerModelInstance> ModelInstance = nullptr;
};
