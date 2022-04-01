// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "DataInterfaceSkinnedMeshExec.generated.h"

class USkinnedMeshComponent;
class FSkeletalMeshObject;
class FRDGBuffer;
class FRDGBufferUAV;

UENUM()
enum class ESkinnedMeshExecDomain : uint8
{
	None = 0 UMETA(Hidden),
	/** Run kernel with one thread per vertex. */
	Vertex = 1,
	/** Run kernel with one thread per triangle. */
	Triangle,
};

/** Compute Framework Data Interface for executing kernels over a skinned mesh domain. */
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API USkinnedMeshExecDataInterface : public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	FName GetCategory() const override;
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	//~ End UOptimusComputeDataInterface Interface
	
	//~ Begin UComputeDataInterface Interface
	bool IsExecutionInterface() const override { return true; }
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const override;
	void GetHLSL(FString& OutHLSL) const override;
	void GetSourceTypes(TArray<UClass*>& OutSourceTypes) const override;
	UComputeDataProvider* CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

	UPROPERTY(EditAnywhere, Category = Execution)
	ESkinnedMeshExecDomain Domain = ESkinnedMeshExecDomain::Vertex;
};

/** Compute Framework Data Provider for executing kernels over a skinned mesh domain. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class USkinnedMeshExecDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<USkinnedMeshComponent> SkinnedMesh = nullptr;

	UPROPERTY()
	ESkinnedMeshExecDomain Domain = ESkinnedMeshExecDomain::Vertex;

	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};

class FSkinnedMeshExecDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FSkinnedMeshExecDataProviderProxy(USkinnedMeshComponent* InSkinnedMeshComponent, ESkinnedMeshExecDomain InDomain);

	//~ Begin FComputeDataProviderRenderProxy Interface
	int32 GetDispatchThreadCount(TArray<FIntVector>& ThreadCounts) const override;
	void GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	FSkeletalMeshObject* SkeletalMeshObject = nullptr;
	ESkinnedMeshExecDomain Domain = ESkinnedMeshExecDomain::Vertex;
};
