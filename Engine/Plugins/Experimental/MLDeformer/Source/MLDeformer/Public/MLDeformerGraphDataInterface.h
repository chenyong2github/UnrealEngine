// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MLDeformer.h"
#include "MLDeformerAsset.h"
#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "MLDeformerGraphDataInterface.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
class FRHIShaderResourceView;
class UMLDeformerComponent;
class UNeuralNetwork;

/** Compute Framework Data Interface for reading skeletal mesh. */
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


/** Compute Framework Data Provider for reading skeletal mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class MLDEFORMER_API UMLDeformerDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<UMLDeformerComponent> DeformerComponent = nullptr;

	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};


class FMLDeformerDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FMLDeformerDataProviderProxy(UMLDeformerComponent* DeformerComponent);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	UNeuralNetwork* NeuralNetwork = nullptr;
	FRHIShaderResourceView* VertexMapBufferSRV = nullptr;
	FRDGBuffer* Buffer = nullptr;
	FRDGBufferSRV* BufferSRV = nullptr;
	FVector VertexDeltaScale = FVector(1.0f, 1.0f, 1.0f);
	FVector VertexDeltaMean = FVector(0.0f, 0.0f, 0.0f);
	bool bCanRunNeuralNet = false;
	float VertexDeltaMultiplier = 1.0f;
	float HeatMapScale = 1.0f;
};
