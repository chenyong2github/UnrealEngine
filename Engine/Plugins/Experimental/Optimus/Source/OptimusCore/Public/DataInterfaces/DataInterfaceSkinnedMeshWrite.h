// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "DataInterfaceSkinnedMeshWrite.generated.h"

class USkinnedMeshComponent;
class FSkeletalMeshObject;
class FRDGBuffer;
class FRDGBufferUAV;

/** Compute Framework Data Interface for writing skinned mesh. */
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API USkinnedMeshWriteDataInterface : public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	//~ End UOptimusComputeDataInterface Interface
	
	//~ Begin UComputeDataInterface Interface
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const override;
	void GetHLSL(FString& OutHLSL) const override;
	void GetSourceTypes(TArray<UClass*>& OutSourceTypes) const override;
	UComputeDataProvider* CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface
};

/** Compute Framework Data Provider for writing skinned mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class OPTIMUSCORE_API USkinnedMeshWriteDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkinnedMeshComponent> SkinnedMesh = nullptr;

	uint64 OutputMask;

	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};

class FSkinnedMeshWriteDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FSkinnedMeshWriteDataProviderProxy(USkinnedMeshComponent* InSkinnedMeshComponent, uint64 InOutputMask);

	//~ Begin FComputeDataProviderRenderProxy Interface
	int32 GetInvocationCount() const override;
	FIntVector GetDispatchDim(int32 InvocationIndex, FIntVector GroupDim) const override;
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	FSkeletalMeshObject* SkeletalMeshObject;
	uint64 OutputMask;

	FRDGBuffer* PositionBuffer = nullptr;
	FRDGBufferUAV* PositionBufferUAV = nullptr;
	FRDGBuffer* TangentBuffer = nullptr;
	FRDGBufferUAV* TangentBufferUAV = nullptr;
	FRDGBuffer* ColorBuffer = nullptr;
	FRDGBufferUAV* ColorBufferUAV = nullptr;
};
