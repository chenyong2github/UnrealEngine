// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "SkeletalRenderPublic.h"
#include "MLDeformerGraphDataInterface.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
class FRHIShaderResourceView;
class FSkeletalMeshObject;
class UMLDeformerComponent;
class UNeuralNetwork;
class USkeletalMeshComponent;
class UMLDeformerModel;

#define MLDEFORMER_SHADER_PARAMETERS() \
	SHADER_PARAMETER(uint32, NumVertices) \
	SHADER_PARAMETER(uint32, InputStreamStart) \
	SHADER_PARAMETER(float, Weight) \
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, PositionDeltaBuffer) \
	SHADER_PARAMETER_SRV(Buffer<uint>, VertexMapBuffer)

#define MLDEFORMER_GRAPH_DISPATCH_START(ParameterStructType, InDispatchSetup, InOutDispatchData) \
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(ParameterStructType))) \
	{ \
		return; \
	} \
	const FSkeletalMeshRenderData& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData(); \
	const FSkeletalMeshLODRenderData* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0); \
	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex) \
	{ \
		const FSkelMeshRenderSection& RenderSection = LodRenderData->RenderSections[InvocationIndex]; \
		ParameterStructType* Parameters = (ParameterStructType*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);

#define MLDEFORMER_GRAPH_DISPATCH_DEFAULT_PARAMETERS() \
	Parameters->NumVertices = 0; \
	Parameters->InputStreamStart = RenderSection.BaseVertexIndex; \
	Parameters->Weight = Weight; \
	Parameters->PositionDeltaBuffer = BufferSRV; \
	Parameters->VertexMapBuffer = VertexMapBufferSRV;

#define MLDEFORMER_GRAPH_DISPATCH_END() }

#define MLDEFORMER_GRAPH_IMPLEMENT_BASICS(InterfaceClassName, DataProviderClassName, DataProviderProxyClassName, ParameterStructType, HLSLText, DisplayName) \
	FString InterfaceClassName::GetDisplayName() const \
	{ \
		return DisplayName; \
	} \
	void InterfaceClassName::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const \
	{ \
		InOutBuilder.AddNestedStruct<ParameterStructType>(UID); \
	} \
	void InterfaceClassName::GetHLSL(FString& OutHLSL) const \
	{ \
		OutHLSL += HLSLText; \
	} \
	UComputeDataProvider* InterfaceClassName::CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const \
	{ \
		DataProviderClassName* Provider = NewObject<DataProviderClassName>(); \
		Provider->DeformerComponent = Cast<UMLDeformerComponent>(InBinding); \
		return Provider; \
	} \
	FComputeDataProviderRenderProxy* DataProviderClassName::GetRenderProxy() \
	{ \
		return new DataProviderProxyClassName(DeformerComponent); \
	}

#if WITH_EDITORONLY_DATA
	#define MLDEFORMER_EDITORDATA_ONLY(Statement, ElseStatement) Statement
#else
	#define MLDEFORMER_EDITORDATA_ONLY(X, ElseStatement) ElseStatement
#endif

/** Compute Framework Data Interface for MLDefomer data. */
UCLASS(Category = ComputeFramework)
class MLDEFORMERFRAMEWORK_API UMLDeformerGraphDataInterface
	: public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	// UOptimusComputeDataInterface overrides.
	virtual FString GetDisplayName() const override;
	virtual TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	virtual TSubclassOf<UActorComponent> GetRequiredComponentClass() const override;
	// ~END UOptimusComputeDataInterface overrides.

	// UComputeDataInterface overrides.
	TCHAR const* GetClassName() const override { return TEXT("MLDeformer"); }
	virtual void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	virtual void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	virtual void GetHLSL(FString& OutHLSL) const override;
	virtual UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	// ~END UComputeDataInterface overrides.
};

/** Compute Framework Data Provider for MLDeformer data. */
UCLASS(BlueprintType, EditInlineNew, Category = ComputeFramework)
class MLDEFORMERFRAMEWORK_API UMLDeformerGraphDataProvider
	: public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<UMLDeformerComponent> DeformerComponent = nullptr;

	// UComputeDataProvider overrides.
	virtual bool IsValid() const override;
	virtual FComputeDataProviderRenderProxy* GetRenderProxy() override;
	// ~END UComputeDataProvider overrides.
};

namespace UE::MLDeformer
{
	/** Compute Framework Data Provider Proxy for MLDeformer data. */
	class MLDEFORMERFRAMEWORK_API FMLDeformerGraphDataProviderProxy
		: public FComputeDataProviderRenderProxy
	{
	public:
		FMLDeformerGraphDataProviderProxy(UMLDeformerComponent* DeformerComponent);

		// FComputeDataProviderRenderProxy overrides.
		virtual void AllocateResources(FRDGBuilder& GraphBuilder) override;
		virtual void GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData) override;
		// ~END FComputeDataProviderRenderProxy overrides.

	protected:
		FSkeletalMeshObject* SkeletalMeshObject;
		TObjectPtr<UNeuralNetwork> NeuralNetwork = nullptr;
		FRHIShaderResourceView* VertexMapBufferSRV = nullptr;
		FRDGBuffer* Buffer = nullptr;
		FRDGBufferSRV* BufferSRV = nullptr;
		float Weight = 1.0f;
		int32 NeuralNetworkInferenceHandle = -1;
		bool bCanRunNeuralNet = false;
	};
}	// namespace UE::MLDeformer
