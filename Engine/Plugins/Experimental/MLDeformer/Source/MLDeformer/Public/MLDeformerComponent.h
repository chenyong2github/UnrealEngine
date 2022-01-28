// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "InputCoreTypes.h"
#include "Components/ActorComponent.h"
#include "MLDeformer.h"
#include "MLDeformerInstance.h"
#include "MLDeformerComponent.generated.h"

// Forward declarations.
class UMLDeformerAsset;
class USkeletalMeshComponent;

/**
 * The ML mesh deformer component.
 * This works in combination with a MLDeformerAsset and SkeletalMeshComponent.
 * The component will perform runtime inference using the network trained in the asset.
 */
UCLASS(Blueprintable, ClassGroup = Component, BlueprintType, meta = (BlueprintSpawnableComponent))
class MLDEFORMER_API UMLDeformerComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:
	UMLDeformerAsset* GetDeformerAsset() { return DeformerAsset; }
	void SetDeformerAsset(UMLDeformerAsset* InDeformerAsset) { DeformerAsset = InDeformerAsset; }

	//~UActorComponent interface
	void Activate(bool bReset=false) override;
	void Deactivate() override;
	//End of ~UActorComponent interface

	/**
	 * Configure MLDeformerComponent with an MLRigDeformerAsset (network + parameters).
	 * @param InDeformerAsset Pointer to MLDeformerAsset.
	 */
	UFUNCTION(BlueprintCallable, Category = "MLDeformer")
	void SetupComponent(UMLDeformerAsset* InDeformerAsset, USkeletalMeshComponent* InSkelMeshComponent);

	float GetVertexDeltaMultiplier() const { return VertexDeltaMultiplier; }
	void SetVertexDeltaMultiplier(float ScaleFactor) { VertexDeltaMultiplier = ScaleFactor;  }

	const FMLDeformerInstance& GetDeformerInstance() const { return DeformerInstance; }
	FMLDeformerInstance& GetDeformerInstance() { return DeformerInstance; }

private:
	/** The deformer asset to use. */
	UPROPERTY(EditAnywhere, Category = "ML Deformer")
	TObjectPtr<UMLDeformerAsset> DeformerAsset = nullptr;

	/** The scale factor of the vertex deltas that are being applied. */
	UPROPERTY(EditAnywhere, Category = "ML Deformer")
	float VertexDeltaMultiplier = 1.0f;

	/** 
	 * The skeletal mesh component we want to grab the bone transforms etc from. 
	 * This can be a nullptr. When it is a nullptr then it will internally try to find the first skeletal mesh component on the actor.
	 * You can see this as an override. You can specify this override through the SetupComponent function.
	 */
	TObjectPtr<USkeletalMeshComponent> SkelMeshComponent = nullptr;

	/** The mesh deformer instance. This is used to perform the runtime updates and run the inference. */
	FMLDeformerInstance DeformerInstance;

	/** DelegateHandle for NeuralNetwork modification. */
	FDelegateHandle NeuralNetworkModifyDelegateHandle;

	/** Bind to the DeformerAsset NeuralNetworkModifyDelegate. */
	void AddNeuralNetworkModifyDelegate();
	/** Unbind from the DeformerAsset NeuralNetworkModifyDelegate. */
	void RemoveNeuralNetworkModifyDelegate();

	// BEGIN AActorComponent interface
	/**
	 * Tick component, i.e., run MLDeformer inference.
	 * @param DeltaTime Elapsed time since last tick
	 * @param TickType Sort of tick
	 * @param ThisTickFunction Tick function that triggers this tick.
	 */
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// END AActorComponent interface
};
