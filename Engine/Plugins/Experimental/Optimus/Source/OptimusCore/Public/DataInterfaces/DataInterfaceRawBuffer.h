// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "DataInterfaceRawBuffer.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
class FRDGBufferUAV;

/** Compute Framework Data Interface for a transient buffer. */
UCLASS(Category = ComputeFramework)
class UTransientBufferDataInterface : public UComputeDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataInterface Interface
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const override;
	void GetHLSL(FString& OutHLSL) const override;
	void ModifyCompilationEnvironment(FShaderCompilerEnvironment& OutEnvironment) const override;
	//~ End UComputeDataInterface Interface

	UPROPERTY()
	FShaderParamTypeDefinition Type;

private:
	bool SupportsAtomics() const;
};

/** Compute Framework Data Provider for a transient buffer. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UTransientBufferDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataProvider Interface
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface

	UPROPERTY(BlueprintReadWrite, Category = "Buffer")
	int32 ElementStride = 4;
	UPROPERTY(BlueprintReadWrite, Category = "Buffer")
	int32 NumElements = 1;
	UPROPERTY(BlueprintReadWrite, Category = "Buffer")
	bool bClearBeforeUse = true;
};

class FTransientBufferDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FTransientBufferDataProviderProxy(int32 InElementStride, int32 InNumElements, bool bInClearBeforeUse);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	int32 ElementStride;
	int32 NumElements;
	bool bClearBeforeUse;

	TArray<FRDGBuffer*> Buffer;
	TArray<FRDGBufferSRV*> BufferSRV;
	TArray<FRDGBufferUAV*> BufferUAV;
};
