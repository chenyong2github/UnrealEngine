// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MLDeformer.h"
#include "MLDeformerAsset.h"
#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "MLDeformerDebugDataInterface.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
struct FMLDeformerMeshMapping;
class FRHIShaderResourceView;
class FSkeletalMeshObject;
class UMLDeformerAsset;
class USkeletalMeshComponent;

/** 
 * Compute Framework Data Interface for MLDefomer debugging data. 
 * This interfaces to editor only data, and so will only give valid results in that context.
 */
UCLASS(Category = ComputeFramework)
class MLDEFORMER_API UMLDeformerDebugDataInterface : public UOptimusComputeDataInterface
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


/** Compute Framework Data Provider for MLDeformer debugging data. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class MLDEFORMER_API UMLDeformerDebugDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<UMLDeformerAsset> DeformerAsset = nullptr;

#if WITH_EDITORONLY_DATA
	/* 
	 * Mesh remapping data between the geometry cache and the rendered vertex buffer. 
	 * This is calculated (slow) on construction. The data isn't available outside of the editor.
	 */
	TArray<FMLDeformerMeshMapping> MeshMappings;
#endif

	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};


/** Compute Framework Data Provider Proxy for MLDeformer debugging data. */
class FMLDeformerDebugDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
#if WITH_EDITORONLY_DATA
	FMLDeformerDebugDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent, UMLDeformerAsset* DeformerAsset, TArray<FMLDeformerMeshMapping> const& MeshMappings);
#endif

	//~ Begin FComputeDataProviderRenderProxy Interface
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	FSkeletalMeshObject* SkeletalMeshObject;
	TArray<FVector3f> GroundTruthPositions;
	FRHIShaderResourceView* VertexMapBufferSRV = nullptr;
	FRDGBuffer* GroundTruthBuffer = nullptr;
	FRDGBufferSRV* GroundTruthBufferSRV = nullptr;
	int32 HeatMapMode = 0;
	float HeatMapScale = 0.0f;
	float GroundTruthLerp = 0.0f;
};
