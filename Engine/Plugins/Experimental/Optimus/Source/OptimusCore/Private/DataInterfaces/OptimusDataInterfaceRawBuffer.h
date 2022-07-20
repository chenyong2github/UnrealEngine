// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeDataProvider.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "OptimusComputeDataInterface.h"
#include "OptimusDataDomain.h"

#include "OptimusDataInterfaceRawBuffer.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
class FRDGBufferUAV;
class FOptimusPersistentBufferPool;
class UOptimusComponentSourceBinding;
class UOptimusRawBufferDataProvider;
class USkinnedMeshComponent;

UCLASS(Abstract)
class OPTIMUSCORE_API UOptimusRawBufferDataInterface : public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	static const int32 ReadValueInputIndex;
	static const int32 WriteValueOutputIndex;
	
	//~ Begin UOptimusComputeDataInterface Interface
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	bool IsVisible() const override	{ return false;	}
	TSubclassOf<UActorComponent> GetRequiredComponentClass() const override;
	//~ End UOptimusComputeDataInterface Interface

	//~ Begin UComputeDataInterface Interface
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderHash(FString& InOutKey) const override;
	void GetHLSL(FString& OutHLSL) const override;
	//~ End UComputeDataInterface Interface

	/** The value type we should be allocating elements for */
	UPROPERTY()
	FShaderValueTypeHandle ValueType;

	UPROPERTY()
	TWeakObjectPtr<UOptimusComponentSourceBinding> ComponentBinding;
	
	/** The data domain this buffer covers */
	UPROPERTY()
	FOptimusMultiLevelDataDomain DataDomain;

protected:
	void FillProviderFromComponent(const USkinnedMeshComponent* InComponent, UOptimusRawBufferDataProvider* InProvider) const;
	
	virtual bool UseSplitBuffers() const { return true; } 
private:
	bool SupportsAtomics() const;
};


/** Compute Framework Data Interface for a transient buffer. */
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusTransientBufferDataInterface : public UOptimusRawBufferDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	//~ End UOptimusComputeDataInterface Interface

	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("TransientBuffer"); }
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

	/** Set to true if the buffer should be cleared prior to each render */ 
	UPROPERTY()
	bool bClearBeforeUse = true;
};


/** Compute Framework Data Interface for a transient buffer. */
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusPersistentBufferDataInterface : public UOptimusRawBufferDataInterface
{
	GENERATED_BODY()

public:
	
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	//~ End UOptimusComputeDataInterface Interface

	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("PersistentBuffer"); }
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

	UPROPERTY()
	FName ResourceName;

protected:
	// For persistent buffers, we only provide the UAV, not the SRV.
	bool UseSplitBuffers() const override { return false; } 
};

/** Compute Framework Data Provider for a transient buffer. */
UCLASS(Abstract)
class OPTIMUSCORE_API UOptimusRawBufferDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	//~ End UComputeDataProvider Interface

	UPROPERTY(BlueprintReadWrite, Category = "Buffer")
	int32 ElementStride = 4;
	
	UPROPERTY(BlueprintReadWrite, Category = "Buffer")
	TArray<int32> NumElementsPerInvocation = {1};
};


/** Compute Framework Data Provider for a transient buffer. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusTransientBufferDataProvider : public UOptimusRawBufferDataProvider
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataProvider Interface
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface

	UPROPERTY(BlueprintReadWrite, Category = "Buffer")
	bool bClearBeforeUse = true;
};


/** Compute Framework Data Provider for a transient buffer. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusPersistentBufferDataProvider : public UOptimusRawBufferDataProvider
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface

	UPROPERTY()
	TObjectPtr<USkinnedMeshComponent> SkinnedMeshComponent = nullptr;

	/** The resource this buffer is provider to */
	FName ResourceName;
};


class FOptimusTransientBufferDataProviderProxy :
	public FComputeDataProviderRenderProxy
{
public:
	FOptimusTransientBufferDataProviderProxy(
			int32 InElementStride,
			TArray<int32> InInvocationElementCount,
			bool bInClearBeforeUse
			);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	const int32 ElementStride;
	const TArray<int32> InvocationElementCount;
	const bool bClearBeforeUse;

	TArray<FRDGBuffer*> Buffer;
	TArray<FRDGBufferSRV*> BufferSRV;
	TArray<FRDGBufferUAV*> BufferUAV;
};


class FOptimusPersistentBufferDataProviderProxy :
	public FComputeDataProviderRenderProxy
{
public:
	FOptimusPersistentBufferDataProviderProxy(
		TSharedPtr<FOptimusPersistentBufferPool> InBufferPool,
		FName InResourceName,
		int32 InElementStride,
		TArray<int32> InInvocationElementCount
		);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	const TSharedPtr<FOptimusPersistentBufferPool> BufferPool;
	const FName ResourceName;
	const int32 ElementStride;
	const TArray<int32> InvocationElementCount;

	TArray<FRDGBuffer*> Buffers;
	TArray<FRDGBufferUAV*> BufferUAVs;
};
