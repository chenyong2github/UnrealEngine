// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "OptimusDataInterfaceJiggleSpring.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
class FSkeletalMeshObject;
class USkeletalMeshComponent;

/** 
* User modifyable jiggle spring attributes. These attributes appear in the Optimus 
* editor's Details panel.
*/
USTRUCT()
struct FOptimusJiggleSpringParameters
{
	GENERATED_BODY();

	/** Uniform stiffness value, multiplied against per-vertex StiffnessWeights. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	float BaselineStiffness = 100.0f;

	/** Per vertex spring stiffness. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	TArray<float> StiffnessWeights;

	/** Stiffness weights file. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	FFilePath StiffnessWeightsFile;

	/** Uniform damping value, multiplied against per-vertex DampingWeights. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	float BaselineDamping = 10.0f;

	/** Per vertex spring damping. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	TArray<float> DampingWeights;

	/** Damping weights file. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	FFilePath DampingWeightsFile;

	bool ReadWeightsFile(const FFilePath& FilePath, TArray<float>& Values) const;
};


/** 
* Compute Framework Data Interface for reading skeletal mesh. 
*
* Defines the output pins of the data interface node available in the Optimus graph 
* editor.  Inputs exposed to the user are dictated by the Parameters UPROPERTY member.
*
* This class establishes a dependency on an external HLSL resource file associated with
* this data interface, usually located in "/Plugin/Optimus/Private/.". 
*/
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusJiggleSpringDataInterface : public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	TSubclassOf<UActorComponent> GetRequiredComponentClass() const override;
	//~ End UOptimusComputeDataInterface Interface
	
	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("JiggleSpring"); }
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	void GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const override;
	void GetShaderHash(FString& InOutKey) const override;
	void GetHLSL(FString& OutHLSL) const override;
	UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

	UPROPERTY(EditAnywhere, Category = Deformer, meta = (ShowOnlyInnerProperties))
	FOptimusJiggleSpringParameters JiggleSpringParameters;
};

/** Compute Framework Data Provider for reading skeletal mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UOptimusJiggleSpringDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkinnedMeshComponent> SkinnedMesh = nullptr;

	UPROPERTY()
	FOptimusJiggleSpringParameters JiggleSpringParameters;

	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};

class FOptimusJiggleSpringDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	//FOptimusClothDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent);
	FOptimusJiggleSpringDataProviderProxy(
		USkinnedMeshComponent* SkeletalMeshComponent,
		FOptimusJiggleSpringParameters const& InJiggleSpringParameters);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData);
	//~ End FComputeDataProviderRenderProxy Interface

private:
	FSkeletalMeshObject* SkeletalMeshObject = nullptr;
	FOptimusJiggleSpringParameters JiggleSpringParameters;
	FRDGBuffer* StiffnessWeightsBuffer = nullptr;
	FRDGBufferSRV* StiffnessWeightsBufferSRV = nullptr;
	FRDGBuffer* DampingWeightsBuffer = nullptr;
	FRDGBufferSRV* DampingWeightsBufferSRV = nullptr;
	float NullFloatBuffer = 0.0f;
};

