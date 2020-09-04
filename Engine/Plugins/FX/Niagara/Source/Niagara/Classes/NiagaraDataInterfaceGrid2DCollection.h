// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "ClearQuad.h"
#include "NiagaraComponent.h"
#include "NiagaraStats.h"

#include "NiagaraDataInterfaceGrid2DCollection.generated.h"

class FNiagaraSystemInstance;
class UTextureRenderTarget2D;

class FGrid2DBuffer
{
public:
	FGrid2DBuffer(int NumX, int NumY, EPixelFormat PixelFormat)
	{
		GridBuffer.Initialize(GPixelFormats[PixelFormat].BlockBytes, NumX, NumY, PixelFormat);
		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GridBuffer.NumBytes);
	}
	~FGrid2DBuffer()
	{
		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GridBuffer.NumBytes);
		GridBuffer.Release();
	}

	FTextureRWBuffer2D GridBuffer;	
};

struct FGrid2DCollectionRWInstanceData_GameThread
{
	FIntPoint NumCells = FIntPoint(EForceInit::ForceInitToZero);
	FIntPoint NumTiles = FIntPoint(EForceInit::ForceInitToZero);
	FVector2D CellSize = FVector2D::ZeroVector;
	FVector2D WorldBBoxSize = FVector2D::ZeroVector;
	EPixelFormat PixelFormat = EPixelFormat::PF_R32_FLOAT;

	/** A binding to the user ptr we're reading the RT from (if we are). */
	FNiagaraParameterDirectBinding<UObject*> RTUserParamBinding;

	UTextureRenderTarget2D* TargetTexture = nullptr;
};

struct FGrid2DCollectionRWInstanceData_RenderThread
{
	FIntPoint NumCells = FIntPoint(EForceInit::ForceInitToZero);
	FIntPoint NumTiles = FIntPoint(EForceInit::ForceInitToZero);
	FVector2D CellSize = FVector2D::ZeroVector;
	FVector2D WorldBBoxSize = FVector2D::ZeroVector;
	EPixelFormat PixelFormat = EPixelFormat::PF_R32_FLOAT;

	TArray<TUniquePtr<FGrid2DBuffer>> Buffers;
	FGrid2DBuffer* CurrentData = nullptr;
	FGrid2DBuffer* DestinationData = nullptr;

	FTextureRHIRef RenderTargetToCopyTo;

	void BeginSimulate(FRHICommandList& RHICmdList);
	void EndSimulate(FRHICommandList& RHICmdList);
	void* DebugTargetTexture = nullptr;
};

struct FNiagaraDataInterfaceProxyGrid2DCollectionProxy : public FNiagaraDataInterfaceProxyRW
{
	FNiagaraDataInterfaceProxyGrid2DCollectionProxy() {}
	
	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;
	virtual void PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;
	virtual void PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) override;

	virtual void ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) override;

	/* List of proxy data for each system instances*/
	// #todo(dmp): this should all be refactored to avoid duplicate code
	TMap<FNiagaraSystemInstanceID, FGrid2DCollectionRWInstanceData_RenderThread> SystemInstancesToProxyData_RT;
};

UCLASS(EditInlineNew, Category = "Grid", meta = (DisplayName = "Grid2D Collection", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceGrid2DCollection : public UNiagaraDataInterfaceGrid2D
{
	GENERATED_UCLASS_BODY()

public:
	DECLARE_NIAGARA_DI_PARAMETER();

	/** Reference to a user parameter if we're reading one. */
	UPROPERTY(EditAnywhere, Category = "Grid2DCollection")
	FNiagaraUserParameterBinding RenderTargetUserParameter;

	UPROPERTY(EditAnywhere, Category = "Grid2DCollection")
	uint8 bCreateRenderTarget : 1;

	UPROPERTY(EditAnywhere, Category = "Grid2DCollection", meta = (ToolTip = "Changes the format used to store data inside the grid, low bit formats save memory and performance."))
	ENiagaraGpuBufferFormat BufferFormat;

	virtual void PostInitProperties() override;
	
	//~ UNiagaraDataInterface interface
	// VM functionality
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override {}
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FGrid2DCollectionRWInstanceData_GameThread); }
	virtual bool HasPreSimulateTick() const override { return true; }

	virtual bool CanExposeVariables() const override { return true;}
	virtual void GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const override;
	virtual bool GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const override;

	//~ UNiagaraDataInterface interface END

	// Fills a texture render target 2d with the current data from the simulation
	// #todo(dmp): this will eventually go away when we formalize how data makes it out of Niagara
	UFUNCTION(BlueprintCallable, Category = Niagara, meta=(DeprecatedFunction, DeprecationMessage = "This function has been replaced by object user variables on the emitter to specify render targets to fill with data."))
	virtual bool FillTexture2D(const UNiagaraComponent *Component, UTextureRenderTarget2D *dest, int AttributeIndex);
	
	UFUNCTION(BlueprintCallable, Category = Niagara, meta=(DeprecatedFunction, DeprecationMessage = "This function has been replaced by object user variables on the emitter to specify render targets to fill with data."))
	virtual bool FillRawTexture2D(const UNiagaraComponent *Component, UTextureRenderTarget2D *Dest, int &TilesX, int &TilesY);
	
	UFUNCTION(BlueprintCallable, Category = Niagara)
	virtual void GetRawTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY);

	UFUNCTION(BlueprintCallable, Category = Niagara)
	virtual void GetTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY);

	void GetWorldBBoxSize(FVectorVMContext& Context);
	void GetCellSize(FVectorVMContext& Context);
	void GetNumCells(FVectorVMContext& Context);

	static const FString NumTilesName;

	static const FString GridName;
	static const FString OutputGridName;
	static const FString SamplerName;

	static const FName SetValueFunctionName;
	static const FName GetValueFunctionName;
	static const FName SampleGridFunctionName;


protected:
	//~ UNiagaraDataInterface interface
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

	//~ UNiagaraDataInterface interface END

	static FNiagaraVariableBase ExposedRTVar;

	TMap<FNiagaraSystemInstanceID, FGrid2DCollectionRWInstanceData_GameThread*> SystemInstancesToProxyData_GT;

	UPROPERTY(Transient)
	TMap< uint64, UTextureRenderTarget2D*> ManagedRenderTargets;
	
};
