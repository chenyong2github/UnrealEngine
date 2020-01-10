// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "NiagaraDataInterfacePressureGrid.generated.h"


/** Render buffers that will be used in hlsl functions */
struct FNDIPressureGridBuffer : public FRenderResource
{
	/** Set the grid size */
	void SetGridSize(const FUintVector4 GridSize);

	/** Init the buffer */
	virtual void InitRHI() override;

	/** Release the buffer */
	virtual void ReleaseRHI() override;

	/** Clear all UAV*/
	void ClearBuffers(FRHICommandList& RHICmdList);

	/** Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FNDIPressureGridBuffer"); }

	/** Grid data texture */
	FTextureRWBuffer3D GridDataBuffer;

	/** Grid size that will be used for the collision*/
	FUintVector4 GridSize;
};

/** Data stored per strand base instance*/
struct FNDIPressureGridData
{
	/** Swap the current and the destination data */
	void SwapBuffers();

	/** Grid Origin */
	FVector4 GridOrigin;

	/** Grid Size */
	FUintVector4 GridSize;
	
	/** World Transform */
	FMatrix WorldTransform;

	/** Inverse world transform */
	FMatrix WorldInverse;

	/** Pointer to the current buffer */
	FNDIPressureGridBuffer* CurrentGridBuffer;

	/** Pointer to the destination buffer */
	FNDIPressureGridBuffer* DestinationGridBuffer;
};

/** Data Interface for the strand base */
UCLASS(EditInlineNew, Category = "Grid", meta = (DisplayName = "Pressure Grid"))
class HAIRSTRANDSNIAGARA_API UNiagaraDataInterfacePressureGrid : public UNiagaraDataInterfaceRWBase
{
	GENERATED_UCLASS_BODY()

public:

	/** Grid size along the X axis. */
	UPROPERTY(EditAnywhere, Category = "Spawn")
	FIntVector GridSize;

	/** Grid size along the Y axis. */
	UPROPERTY(EditAnywhere, Category = "Spawn")
	FVector GridOrigin;

	/** Grid size along the Z axis. */
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float GridLength;

	/** UObject Interface */
	virtual void PostInitProperties() override;

	/** UNiagaraDataInterface Interface */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FNDIPressureGridData); }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	/** GPU simulation  functionality */
	virtual bool GetFunctionHLSL(const FName&  DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters() const override;
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;
	virtual void GetCommonHLSL(FString& OutHLSL) override;

	/** Build the velocity field */
	void BuildVelocityField(FVectorVMContext& Context);

	/** Sample the grid */
	void SampleVelocityField(FVectorVMContext& Context);

	/** Project the velocity field to be divergence free */
	void ProjectVelocityField(FVectorVMContext& Context);

	/** Compute the cell position*/
	void GetCellPosition(FVectorVMContext& Context);

	/** Transfer the cell velocity */
	void TransferCellVelocity(FVectorVMContext& Context);

	/** Set the solid boundary */
	void SetSolidBoundary(FVectorVMContext& Context);

	/** Compute the solid weights */
	void ComputeBoundaryWeights(FVectorVMContext& Context);

	/** Build the grid topology */
	void BuildGridTopology(FVectorVMContext& Context);

	/** Update the grid transform */
	void UpdateGridTransform(FVectorVMContext& Context);

	/** Name of the grid current buffer */
	static const FString GridCurrentBufferName;

	/** Name of the grid X velocity buffer */
	static const FString GridDestinationBufferName;

	/** Name of the grid origin  */
	static const FString GridOriginName;

	/** Name of the grid size */
	static const FString GridSizeName;

	/** Name of the world transform  */
	static const FString WorldTransformName;

	/** Name of the World transform inverse */
	static const FString WorldInverseName;

protected:
	/** Copy one niagara DI to this */
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};

/** Proxy to send data to gpu */
struct FNDIPressureGridProxy : public FNiagaraDataInterfaceProxy
{
	/** Destroy internal data */
	virtual void DeferredDestroy() override;

	/** Get the size of the data that will be passed to render*/
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIPressureGridData); }

	/** Get the data that will be passed to render*/
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;

	/** Initialize the Proxy data strands buffer */
	void InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance, FNDIPressureGridBuffer* CurrentGridBuffer, FNDIPressureGridBuffer* DestinationGridBuffer, const FVector4& GridOrigin, const FUintVector4& GridSize);

	/** Destroy the proxy data if necessary */
	void DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance);

	/** Launch all pre stage functions */
	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;

	/** Launch all post stage functions */
	virtual void PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;

	/** Reset the buffers  */
	virtual void ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;

	/** List of proxy data for each system instances*/
	TMap<FNiagaraSystemInstanceID, FNDIPressureGridData> SystemInstancesToProxyData;

	/** List of proxy data to destroy later */
	TSet<FNiagaraSystemInstanceID> DeferredDestroyList;
};

