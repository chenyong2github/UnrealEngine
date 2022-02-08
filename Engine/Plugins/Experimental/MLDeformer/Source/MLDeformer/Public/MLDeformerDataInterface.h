// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MLDeformer.h"
#include "MLDeformerAsset.h"
#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "MLDeformerDataInterface.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
class FRHIShaderResourceView;
class FSkeletalMeshObject;
class UMLDeformerComponent;
class UNeuralNetwork;
class USkeletalMeshComponent;

/** Compute Framework Data Interface for MLDefomer data. */
UCLASS(Category = ComputeFramework)
class MLDEFORMER_API UMLDeformerDataInterface : public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	//~ End UOptimusComputeDataInterface Interface

	//~ Begin UComputeDataInterface Interface
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const override;
	void GetHLSL(FString& OutHLSL) const override;
	void GetSourceTypes(TArray<UClass*>& OutSourceTypes) const override;
	UComputeDataProvider* CreateDataProvider(TArrayView<TObjectPtr<UObject>> InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface
};


/** Compute Framework Data Provider for MLDeformer data. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class MLDEFORMER_API UMLDeformerDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = nullptr;

	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};


/** Compute Framework Data Provider Proxy for MLDeformer data. */
class FMLDeformerDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FMLDeformerDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent, UMLDeformerComponent* DeformerComponent);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	FSkeletalMeshObject* SkeletalMeshObject;
	UNeuralNetwork* NeuralNetwork = nullptr;
	bool bCanRunNeuralNet = false;
	int32 NeuralNetworkInferenceHandle = -1;
	FRHIShaderResourceView* VertexMapBufferSRV = nullptr;
	FRDGBuffer* Buffer = nullptr;
	FRDGBufferSRV* BufferSRV = nullptr;
	FVector VertexDeltaScale = FVector(1.0f, 1.0f, 1.0f);
	FVector VertexDeltaMean = FVector(0.0f, 0.0f, 0.0f);
	float VertexDeltaMultiplier = 1.0f;
};
