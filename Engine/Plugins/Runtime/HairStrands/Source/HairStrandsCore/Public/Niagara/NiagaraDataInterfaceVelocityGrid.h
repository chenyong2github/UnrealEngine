// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "NiagaraDataInterfaceVelocityGrid.generated.h"


/** Render buffers that will be used in hlsl functions */
struct FNDIVelocityGridBuffer : public FRenderResource
{
	/** Set the grid size */
	void Initialize(const FIntVector GridSize, const int32 NumAttributes);

	/** Init the buffer */
	virtual void InitRHI() override;

	/** Release the buffer */
	virtual void ReleaseRHI() override;

	/** Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FNDIVelocityGridBuffer"); }

	/** Grid data texture */
	FTextureRWBuffer3D GridDataBuffer;

	/** Grid size that will be used for the collision*/
	FIntVector GridSize;

	/** Num attributes in the buffer*/
	int32 NumAttributes;
};

/** Data stored per strand base instance*/
struct FNDIVelocityGridData
{
	/** Swap the current and the destination data */
	void Swap();

	/** Initialize the buffers */
	bool Init(const FIntVector& InGridSize, const int32 InNumAttributes, FNiagaraSystemInstance* SystemInstance);

	/** Release the buffers */
	void Release();

	/** Resize the buffers */
	void Resize();

	/** Grid Size */
	FIntVector GridSize;

	/** Num Attributes */
	int32 NumAttributes;

	/** Need a resize */
	bool NeedResize;

	/** World Transform */
	FMatrix WorldTransform;

	/** Inverse world transform */
	FMatrix WorldInverse;

	/** Pointer to the current buffer */
	FNDIVelocityGridBuffer* CurrentGridBuffer;

	/** Pointer to the destination buffer */
	FNDIVelocityGridBuffer* DestinationGridBuffer;
};

/** Data Interface paramaters name */
struct FNDIVelocityGridParametersName
{
	FNDIVelocityGridParametersName(const FString& Suffix);

	FString GridCurrentBufferName;
	FString GridDestinationBufferName;

	FString GridSizeName;
	FString WorldTransformName;
	FString WorldInverseName;
};

struct FNDIVelocityGridParametersCS : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNDIVelocityGridParametersCS, NonVirtual);

	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap);

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const;

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const;

private:

	LAYOUT_FIELD(FShaderResourceParameter, GridCurrentBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, GridDestinationBuffer);

	LAYOUT_FIELD(FShaderParameter, GridSize);
	LAYOUT_FIELD(FShaderParameter, WorldTransform);
	LAYOUT_FIELD(FShaderParameter, WorldInverse);
};

/** Data Interface for the strand base */
UCLASS(EditInlineNew, Category = "Grid", meta = (DisplayName = "Velocity Grid"))
class HAIRSTRANDSCORE_API UNiagaraDataInterfaceVelocityGrid : public UNiagaraDataInterfaceRWBase
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	/** Grid size along the X axis. */
	UPROPERTY(EditAnywhere, Category = "Spawn")
	FIntVector GridSize;

	/** Num Attributes */
	int32 NumAttributes;

	/** UObject Interface */
	virtual void PostInitProperties() override;

	/** UNiagaraDataInterface Interface */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FNDIVelocityGridData); }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	/** GPU simulation  functionality */
#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

	/** Build the velocity field */
	void BuildVelocityField(FVectorVMContext& Context);

	/** Sample the grid */
	void SampleVelocityField(FVectorVMContext& Context);

	/** Compute the grid Size (Origin and length) */
	void ComputeGridSize(FVectorVMContext& Context);

	/** Update the grid transform */
	void UpdateGridTransform(FVectorVMContext& Context);

	/** Set the grid dimension */
	void SetGridDimension(FVectorVMContext& Context);

	/** Name of the grid current buffer */
	static const FString GridCurrentBufferName;

	/** Name of the grid X velocity buffer */
	static const FString GridDestinationBufferName;

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
struct FNDIVelocityGridProxy : public FNiagaraDataInterfaceProxyRW
{
	/** Get the size of the data that will be passed to render*/
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIVelocityGridData); }

	/** Get the data that will be passed to render*/
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;

	/** Launch all pre stage functions */
	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;

	/** Launch all post stage functions */
	virtual void PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;

	/** Reset the buffers  */
	virtual void ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) override;

	// Get the element count for this instance
	virtual FIntVector GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const override;

	/** List of proxy data for each system instances*/
	TMap<FNiagaraSystemInstanceID, FNDIVelocityGridData> SystemInstancesToProxyData;
};

