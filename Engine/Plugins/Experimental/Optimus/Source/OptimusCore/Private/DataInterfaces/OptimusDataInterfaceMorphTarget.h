// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"

#include "OptimusDataInterfaceMorphTarget.generated.h"

class FSkeletalMeshObject;
class USkeletalMeshComponent;

/** Compute Framework Data Interface for reading skeletal mesh. */
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusMorphTargetDataInterface : public UOptimusComputeDataInterface
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
	void GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const override;
	void GetHLSL(FString& OutHLSL) const override;
	void GetSourceTypes(TArray<UClass*>& OutSourceTypes) const override;
	UComputeDataProvider* CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface
};

/** Compute Framework Data Provider for reading skeletal mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UOptimusMorphTargetDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkeletalMeshComponent> SkeletalMesh = nullptr;

	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};

class FOptimusMorphTargetDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FOptimusMorphTargetDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData);
	//~ End FComputeDataProviderRenderProxy Interface

private:
	FSkeletalMeshObject* SkeletalMeshObject;
	uint32 FrameNumber = 0;
};
