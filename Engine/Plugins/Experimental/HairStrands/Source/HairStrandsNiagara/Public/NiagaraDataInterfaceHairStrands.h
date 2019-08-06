// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "HairStrandsAsset.h"
#include "NiagaraDataInterfaceHairStrands.generated.h"

/** Size of each strands*/
UENUM(BlueprintType)
enum class EHairStrandsSize : uint8
{
	Size2 = 0x02 UMETA(DisplatName = "2"),
	Size4 = 0x04 UMETA(DisplatName = "4"),
	Size8 = 0x08 UMETA(DisplatName = "8"),
	Size16 = 0x10 UMETA(DisplatName = "16"),
	Size32 = 0x20 UMETA(DisplatName = "32")
};

/** Render buffers that will be used in hlsl functions */
struct FNDIHairStrandsBuffer : public FRenderResource
{
	/** Set the asset that will be used to affect the buffer */
	void SetAsset(const UHairStrandsAsset*  HairStrandsAsset);

	/** Set the strand size */
	void SetStrandSize(const uint32 StrandSize);

	/** Init the buffer */
	virtual void InitRHI() override;

	/** Release the buffer */
	virtual void ReleaseRHI() override;

	/** Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FNDIHairStrandsBuffer"); }

	/** Position Buffer UAV */
	FUnorderedAccessViewRHIRef PointsPositionsBufferUav;

	/** Strand nodes positions buffer */
	FRWBuffer NodesPositionsBuffer;

	/** Strand points node index buffer */
	FRWBuffer PointsNodesBuffer;

	/** Strand points node weights buffer */
	FRWBuffer PointsWeightsBuffer;

	/** Strand curves point offset buffer */
	FRWBuffer CurvesOffsetsBuffer;

	/** The strand asset from which to sample */
	const UHairStrandsAsset* SourceAsset;

	/** Strand size that will be used for resampling*/
	uint32 StrandSize;
};

/** Data stored per strand base instance*/
struct FNDIHairStrandsData
{
	/** Cached World transform. */
	FMatrix WorldTransform;

	/** Number of strands*/
	int32 NumStrands;

	/** Strand size */
	int32 StrandSize;

	/** Strand Density */
	float StrandDensity;

	/** Root Thickness */
	float RootThickness;

	/** Tip Thickness */
	float TipThickness;

	/** Strands Gpu buffer */
	FNDIHairStrandsBuffer* StrandBuffer;
};

/** Data Interface for the strand base */
UCLASS(EditInlineNew, Category = "Strands", meta = (DisplayName = "Hair Strands"))
class HAIRSTRANDSNIAGARA_API UNiagaraDataInterfaceHairStrands : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	/** Size of each strand. */
	UPROPERTY(EditAnywhere, Category = "Spawn")
	EHairStrandsSize StrandSize;

	/** Density of each strand. */
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float StrandDensity;

	/** Strand Root thickness. */
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float RootThickness;

	/** Strand Tip thickness. */
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float TipThickness;

	/** Mesh used to sample from when not overridden by a source actor from the scene. Also useful for previewing in the editor. */
	UPROPERTY(EditAnywhere, Category = "Source")
	UHairStrandsAsset* SourceAsset;

	/** Source transform to be applied to the asset. */
	UPROPERTY(EditAnywhere, Category = "Source")
	FMatrix SourceTransform;

	/** Static mesh on which the roots are attached. */
	UPROPERTY(EditAnywhere, Category = "Attachment")
	UStaticMesh* StaticMesh;

	///** The source actor from which to sample */
	//UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Source")
	//AActor* SourceActor;

	///** The source component from which to sample */
	//class UHairStrandsComponent* SourceComponent;

	/** UObject Interface */
	virtual void PostInitProperties() override;

	/** UNiagaraDataInterface Interface */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FNDIHairStrandsData); }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	/** GPU simulation  functionality */
	virtual bool GetFunctionHLSL(const FName&  DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters() const override;
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FGuid& SystemInstance) override;
	virtual void GetCommonHLSL(FString& OutHLSL) override;

	/** Get the number of strands */
	void GetNumStrands(FVectorVMContext& Context);

	/** Get the strand size  */
	void GetStrandSize(FVectorVMContext& Context);

	/** Get the strand density */
	void GetStrandDensity(FVectorVMContext& Context);

	/** Get the strand thickness at the root */
	void GetRootThickness(FVectorVMContext& Context);

	/** Get the strand thickness at the tip */
	void GetTipThickness(FVectorVMContext& Context);

	/** Get the world transform */
	void GetWorldTransform(FVectorVMContext& Context);

	/** Get the strand vertex position in world space*/
	void GetVertexPosition(FVectorVMContext& Context);

	/** Get the strand node position in world space*/
	void ComputeNodePosition(FVectorVMContext& Context);

	/** Name of the world transform */
	static const FString WorldTransformName;

	/** Name of the number of strands */
	static const FString NumStrandsName;

	/** Name of the strand size */
	static const FString StrandSizeName;

	/** Name of the strand density */
	static const FString StrandDensityName;

	/** Name of the root thickness */
	static const FString RootThicknessName;

	/** Name of the tip thickness */
	static const FString TipThicknessName;

	/** Name of the points positions buffer */
	static const FString PointsPositionsBufferName;

	/** Name of the curves offsets buffer */
	static const FString CurvesOffsetsBufferName;

	/** Name of the nodes positions buffer */
	static const FString NodesPositionsBufferName;

	/** Name of the points nodes buffer */
	static const FString PointsNodesBufferName;

	/** Name of the points weights buffer */
	static const FString PointsWeightsBufferName;

protected:
	/** Copy one niagara DI to this */
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};

/** Proxy to send data to gpu */
struct FNDIHairStrandsProxy : public FNiagaraDataInterfaceProxy
{
	/** Destroy internal data */
	virtual void DeferredDestroy() override;

	/** Get the size of the data that will be passed to render*/
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIHairStrandsData); }

	/** Get the data that will be passed to render*/
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FGuid& Instance) override;

	/** Initialize the Proxy data strands buffer */
	void InitializePerInstanceData(const FGuid& SystemInstance, FNDIHairStrandsBuffer* StrandsBuffer, const uint32 NumStrands, const uint8 StrandSize,
		const float StrandDensity, const float RootThickness, const float TipThickness);

	/** Destroy the proxy data if necessary */
	void DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FGuid& SystemInstance);

	/** List of proxy data for each system instances*/
	TMap<FGuid, FNDIHairStrandsData> SystemInstancesToProxyData;

	/** List of proxy data to destroy later */
	TSet<FGuid> DeferredDestroyList;
};

