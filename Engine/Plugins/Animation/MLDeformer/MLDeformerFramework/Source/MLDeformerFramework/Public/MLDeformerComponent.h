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
#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	// ~END UObject overrides.

	// UActorComponent overrides.
	void Activate(bool bReset=false) override;
	void Deactivate() override;
	// ~END UActorComponent overrides.

	/** 
	 * Setup the ML Deformer, by picking the deformer asset and skeletal mesh component. 
	 * Call this when you want to assign a specific ML Deformer to a specific skeletal mesh component.
	 * This is used in case there are multiple skeletal mesh components on your actor.
	 * On default the first skeletal mesh component it finds will be used. This function allows you to change that behavior.
	 * Keep in mind that the ML Deformer asset has to be trained using the same Skeletal Mesh as used by the skeletal mesh component you pick.
	 * @param InDeformerAsset The ML Deformer asset to apply to the specified skeletal mesh component.	 
	 * @param InSkelMeshComponent The skeletal mesh component to apply the specified deformer to.
	 */
	UFUNCTION(BlueprintCallable, Category = "MLDeformer")
	void SetupComponent(UMLDeformerAsset* InDeformerAsset, USkeletalMeshComponent* InSkelMeshComponent);

	/** 
	 * Get the current ML Deformer weight. A value of 0 means it is fully disabled, while 1 means fully active.
	 * Values can be anything between 0 and 1.
	 */
	UFUNCTION(BlueprintCallable, Category = "MLDeformer")
	float GetWeight() const										{ return Weight; }

	/**
	 * Set the ML Deformer weight. This determines how active the deformer is. You can see it as a blend weight.
	 * A value of 0 means it is inactive. Certain calculations will be skipped in that case.
	 * A value of 1 means it is fully active.
	 * Values between 0 and 1 blend between the two states.
	 * Call this after you call SetupComponent.
	 * @param NormalizedWeightValue The weight value.
	 */
	UFUNCTION(BlueprintCallable, Category = "MLDeformer")
	void SetWeight(float NormalizedWeightValue)					{ Weight = FMath::Clamp<float>(NormalizedWeightValue, 0.0f, 1.0f);  }

	/**
	 * Get the ML Deformer asset that is used by this component.
	 * @return A pointer to the ML Deformer asset.
	 */
	UMLDeformerAsset* GetDeformerAsset() const					{ return DeformerAsset; }

	/**
	 * Set the deformer asset that is used by this component.
	 * @param InDeformerAsset A pointer to the deformer asset.
	 */
	void SetDeformerAsset(UMLDeformerAsset* InDeformerAsset)	{ DeformerAsset = InDeformerAsset; }

	/**
	 * Get the ML Deformer model instance that this component currently uses.
	 * The instance is responsible for running inference and feeding the neural network with inputs.
	 * @return A pointer to the model instance object.
	 */
	UMLDeformerModelInstance* GetModelInstance() const			{ return ModelInstance; }

	/**
	 * Get the skeletal mesh component that the ML Deformer will work on.
	 * The skeletal mesh that is setup inside the skeletal mesh component will be the mesh that will be deformed by this ML Deformer component.
	 * @return A pointer to the skeletal mesh component who's mesh will be deformed by this ML Deformer component.
	 */
	USkeletalMeshComponent* GetSkeletalMeshComponent() const	{ return SkelMeshComponent.Get(); }

	// Get property names.
	static FName GetDeformerAssetPropertyName()					{ return GET_MEMBER_NAME_CHECKED(UMLDeformerComponent, DeformerAsset); }
	static FName GetWeightPropertyName()						{ return GET_MEMBER_NAME_CHECKED(UMLDeformerComponent, Weight); }

protected:
	// AActorComponent overrides.
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// ~END AActorComponent overrides.

	/** 
	 * Initialize the component. 
	 * This releases any existing deformer instance that is active, and creates a new one.
	 * It then also calls PostMLDeformerComponentInit.
	 * This method is called automatically by SetupComponent.
	 */
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
