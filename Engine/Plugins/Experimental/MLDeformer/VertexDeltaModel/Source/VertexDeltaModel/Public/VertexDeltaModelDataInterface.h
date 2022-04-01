// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerGraphDataInterface.h"
#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "VertexDeltaModelDataInterface.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
class FRHIShaderResourceView;
class FSkeletalMeshObject;
class UMLDeformerComponent;
class UNeuralNetwork;
class USkeletalMeshComponent;
class UMLDeformerModel;

/** Compute Framework Data Interface for MLDefomer data. */
UCLASS(Category = ComputeFramework)
class VERTEXDELTAMODEL_API UVertexDeltaModelDataInterface
	: public UMLDeformerGraphDataInterface
{
	GENERATED_BODY()

public:
	// UOptimusComputeDataInterface overrides.
	virtual FString GetDisplayName() const override;
	// ~END UOptimusComputeDataInterface overrides.

	// UComputeDataInterface overrides.
	virtual void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const override;
	virtual void GetHLSL(FString& OutHLSL) const override;
	virtual UComputeDataProvider* CreateDataProvider(TArrayView<TObjectPtr<UObject>> InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const override;
	// ~END UComputeDataInterface overrides.
};

/** Compute Framework Data Provider for MLDeformer data. */
UCLASS(BlueprintType, EditInlineNew, Category = ComputeFramework)
class VERTEXDELTAMODEL_API UVertexDeltaModelDataProvider
	: public UMLDeformerGraphDataProvider
{
	GENERATED_BODY()

public:
	// UComputeDataProvider overrides.
	virtual FComputeDataProviderRenderProxy* GetRenderProxy() override;
	// ~END UComputeDataProvider overrides.
};

namespace UE::VertexDeltaModel
{
	/** Compute Framework Data Provider Proxy for MLDeformer data. */
	class VERTEXDELTAMODEL_API FVertexDeltaModelDataProviderProxy
		: public UE::MLDeformer::FMLDeformerGraphDataProviderProxy
	{
	public:
		FVertexDeltaModelDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent, UMLDeformerComponent* DeformerComponent);

		// FComputeDataProviderRenderProxy overrides.
		virtual void GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData) override;
		// ~END FComputeDataProviderRenderProxy overrides.

	private:
		FVector3f VertexDeltaScale = FVector3f(1.0f, 1.0f, 1.0f);
		FVector3f VertexDeltaMean = FVector3f(0.0f, 0.0f, 0.0f);
	};
}	// namespace UE::VertexDeltaModel
