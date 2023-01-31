// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "OptimusDataInterfaceHalfEdge.generated.h"

class FHalfEdgeDataInterfaceParameters;
class FRDGBuffer;
class FRDGBufferSRV;
class FSkeletalMeshObject;
class USkinnedMeshComponent;


/** Compute Framework Data Interface for reading mesh half edge data. */
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusHalfEdgeDataInterface : public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	TSubclassOf<UActorComponent> GetRequiredComponentClass() const override;
	//~ End UOptimusComputeDataInterface Interface
	
	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("HalfEdge"); }
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	TCHAR const* GetShaderVirtualPath() const override;
	void GetShaderHash(FString& InOutKey) const override;
	void GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const override;
	UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

private:
	static TCHAR const* TemplateFilePath;
};

/** Compute Framework Data Provider for reading skeletal mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UOptimusHalfEdgeDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkinnedMeshComponent> SkinnedMesh = nullptr;

	//~ Begin UComputeDataProvider Interface
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface

	TArray< TArray<int32> > VertexToEdgePerLod;
	TArray< TArray<int32> > EdgeToTwinEdgePerLod;
};

class FOptimusHalfEdgeDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FOptimusHalfEdgeDataProviderProxy(
		USkinnedMeshComponent* SkinnedMeshComponent, 
		TArray< TArray<int32> >& InVertexToEdgePerLod,
		TArray< TArray<int32> >& InEdgeToTwinEdgeBuffer);

	//~ Begin FComputeDataProviderRenderProxy Interface
	bool IsValid(FValidationData const& InValidationData) const override;
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GatherDispatchData(FDispatchData const& InDispatchData);
	//~ End FComputeDataProviderRenderProxy Interface

private:
	using FParameters = FHalfEdgeDataInterfaceParameters;

	FSkeletalMeshObject* SkeletalMeshObject = nullptr;

	TArray< TArray<int32> > const& VertexToEdgePerLod;
	FRDGBuffer* VertexToEdgeBuffer = nullptr;
	FRDGBufferSRV* VertexToEdgeBufferSRV = nullptr;

	TArray< TArray<int32> > const& EdgeToTwinEdgePerLod;
	FRDGBuffer* EdgeToTwinEdgeBuffer = nullptr;
	FRDGBufferSRV* EdgeToTwinEdgeBufferSRV = nullptr;
};
