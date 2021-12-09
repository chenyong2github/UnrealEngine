// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusComputeDataInterface.h"
#include "OptimusDataDomain.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "DataInterfaceRawBuffer.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
class FRDGBufferUAV;

/** Compute Framework Data Interface for a transient buffer. */
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API UTransientBufferDataInterface : public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	static const int32 ReadValueInputIndex;
	static const int32 WriteValueOutputIndex;
	
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	
	/// Returns the list of pins that will map to the shader functions provided by this data interface.
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	
	bool IsVisible() const override
	{
		return false;
	}
	//~ End UOptimusComputeDataInterface Interface

	//~ Begin UComputeDataInterface Interface
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const override;
	void GetHLSL(FString& OutHLSL) const override;
	void ModifyCompilationEnvironment(FShaderCompilerEnvironment& OutEnvironment) const override;
	void GetSourceTypes(TArray<UClass*>& OutSourceTypes) const override;
	UComputeDataProvider* CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

	void Serialize(FArchive& Ar) override
	{
		Super::Serialize(Ar);
	}

	void Serialize(FStructuredArchive::FRecord Record) override
	{
		Super::Serialize(Record);
	}

	UPROPERTY()
	FShaderValueTypeHandle ValueType;

	UPROPERTY()
	FOptimusDataDomain DataDomain;
private:
	bool SupportsAtomics() const;
};

/** Compute Framework Data Provider for a transient buffer. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class OPTIMUSCORE_API UTransientBufferDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface

	UPROPERTY(BlueprintReadWrite, Category = "Buffer")
	int32 ElementStride = 4;
	UPROPERTY(BlueprintReadWrite, Category = "Buffer")
	int32 NumInvocations = 1;
	UPROPERTY(BlueprintReadWrite, Category = "Buffer")
	int32 NumElements = 1;
	UPROPERTY(BlueprintReadWrite, Category = "Buffer")
	bool bClearBeforeUse = true;
};

class FTransientBufferDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FTransientBufferDataProviderProxy(int32 InElementStride, int32 InNumInvocations, int32 InNumElements, bool bInClearBeforeUse);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	int32 ElementStride;
	int32 NumInvocations;
	int32 NumElements;
	bool bClearBeforeUse;

	TArray<FRDGBuffer*> Buffer;
	TArray<FRDGBufferSRV*> BufferSRV;
	TArray<FRDGBufferUAV*> BufferUAV;
};
