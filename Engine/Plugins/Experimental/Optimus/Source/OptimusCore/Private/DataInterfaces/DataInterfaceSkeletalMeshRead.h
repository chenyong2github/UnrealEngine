// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "DataInterfaceSkeletalMeshRead.generated.h"

class FGPUSkinCache;
class FSkeletalMeshObject;
class USkeletalMeshComponent;

/** Compute Framework Data Interface for reading skeletal mesh. */
UCLASS(Category = ComputeFramework)
class USkeletalMeshReadDataInterface : public UComputeDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataInterface Interface
	void GetPermutations(FComputeKernelPermutationSet& OutPermutationSet) const override;
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const override;
	void GetHLSL(FString& OutHLSL) const override;
	//~ End UComputeDataInterface Interface
};

/** Compute Framework Data Provider for reading skeletal mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class USkeletalMeshReadDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkeletalMeshComponent> SkeletalMesh = nullptr;

	//~ Begin UComputeDataProvider Interface
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};

class FSkeletalMeshReadDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FSkeletalMeshReadDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent);

	//~ Begin FComputeDataProviderRenderProxy Interface
	int32 GetInvocationCount() const override;
	FIntVector GetDispatchDim(int32 InvocationIndex, FIntVector GroupDim) const override;
	void GetPermutations(int32 InvocationIndex, FComputeKernelPermutationSet& OutPermutationSet) const override;
	void GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	FSkeletalMeshObject* SkeletalMeshObject;
	FGPUSkinCache* GPUSkinCache;
};
