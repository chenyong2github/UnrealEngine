// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "DataInterfaceGraph.generated.h"

class UOptimusDeformerInstance;
class USkinnedMeshComponent;

/** */
USTRUCT()
struct FGraphVariableDescription
{
	GENERATED_BODY()

	UPROPERTY()
	FString	Name;

	UPROPERTY()
	FShaderValueTypeHandle ValueType;

	UPROPERTY()
	TArray<uint8> Value;

	UPROPERTY()
	int32 Offset = 0;
};

/** Compute Framework Data Interface used for marshaling compute graph parameters and variables. */
UCLASS(Category = ComputeFramework)
class UGraphDataInterface : public UComputeDataInterface
{
	GENERATED_BODY()

public:
	void Init(TArray<FGraphVariableDescription> const& InVariables);

	//~ Begin UComputeDataInterface Interface
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const override;
	void GetHLSL(FString& OutHLSL) const override;
	void GetSourceTypes(TArray<UClass*>& OutSourceTypes) const override;
	UComputeDataProvider* CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

private:
	UPROPERTY()
	TArray<FGraphVariableDescription> Variables;

	UPROPERTY()
	int32 ParameterBufferSize = 0;
};

/** Compute Framework Data Provider for marshaling compute graph parameters and variables. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UGraphDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkinnedMeshComponent> SkinnedMeshComponent = nullptr;

	UPROPERTY()
	TArray<FGraphVariableDescription> Variables;

	int32 ParameterBufferSize = 0;

	void SetConstant(FString const& InVariableName, TArray<uint8> const& InValue);

	//~ Begin UComputeDataProvider Interface
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};

class FGraphDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FGraphDataProviderProxy(UOptimusDeformerInstance const* DeformerInstance, TArray<FGraphVariableDescription> const& Variables, int32 ParameterBufferSize);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	TArray<uint8> ParameterData;
};
