// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "ClearQuad.h"
#include "NiagaraComponent.h"
#include "Niagara/Private/NiagaraStats.h"

#include "NiagaraDataInterfaceGrid3DCollection.generated.h"

class FNiagaraSystemInstance;
class UTextureRenderTarget;
class UTextureRenderTargetVolume;

class FGrid3DBuffer
{
public:
	FGrid3DBuffer(int NumX, int NumY, int NumZ, EPixelFormat PixelFormat)
	{
		GridBuffer.Initialize(GPixelFormats[PixelFormat].BlockBytes, NumX, NumY, NumZ, PixelFormat);
		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GridBuffer.NumBytes);
	}

	~FGrid3DBuffer()
	{
		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GridBuffer.NumBytes);
		GridBuffer.Release();
	}

	FTextureRWBuffer3D GridBuffer;	
};

struct FGrid3DCollectionRWInstanceData_GameThread
{
	FIntVector NumCells = FIntVector::ZeroValue;
	FIntVector NumTiles = FIntVector::ZeroValue;
	FVector CellSize = FVector::ZeroVector;
	FVector WorldBBoxSize = FVector::ZeroVector;
	EPixelFormat PixelFormat = EPixelFormat::PF_R32_FLOAT;
#if WITH_EDITOR
	bool bPreviewGrid = false;
	FIntVector4 PreviewAttribute = FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE);
#endif

	bool NeedsRealloc = false;

	/** A binding to the user ptr we're reading the RT from (if we are). */
	FNiagaraParameterDirectBinding<UObject*> RTUserParamBinding;

	UTextureRenderTargetVolume* TargetTexture = nullptr;

	bool UpdateTargetTexture(ENiagaraGpuBufferFormat BufferFormat);
};

struct FGrid3DCollectionRWInstanceData_RenderThread
{
	FIntVector NumCells = FIntVector::ZeroValue;
	FIntVector NumTiles = FIntVector::ZeroValue;
	FVector CellSize = FVector::ZeroVector;
	FVector WorldBBoxSize = FVector::ZeroVector;
	EPixelFormat PixelFormat = EPixelFormat::PF_R32_FLOAT;
#if WITH_EDITOR
	bool bPreviewGrid = false;
	FIntVector4 PreviewAttribute = FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE);
#endif

	TArray<TUniquePtr<FGrid3DBuffer>> Buffers;
	FGrid3DBuffer* CurrentData = nullptr;
	FGrid3DBuffer* DestinationData = nullptr;

	FTextureRHIRef RenderTargetToCopyTo;

	void BeginSimulate(FRHICommandList& RHICmdList);
	void EndSimulate(FRHICommandList& RHICmdList);
};

struct FNiagaraDataInterfaceProxyGrid3DCollectionProxy : public FNiagaraDataInterfaceProxyRW
{
	FNiagaraDataInterfaceProxyGrid3DCollectionProxy() {}

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;
	virtual void PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;
	virtual void PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) override;
	virtual void ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) override;

	virtual FIntVector GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const override;

	/* List of proxy data for each system instances*/
	// #todo(dmp): this should all be refactored to avoid duplicate code
	TMap<FNiagaraSystemInstanceID, FGrid3DCollectionRWInstanceData_RenderThread> SystemInstancesToProxyData_RT;
};

UCLASS(EditInlineNew, Category = "Grid", meta = (DisplayName = "Grid3D Collection", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceGrid3DCollection : public UNiagaraDataInterfaceGrid3D
{
	GENERATED_UCLASS_BODY()

public:
	DECLARE_NIAGARA_DI_PARAMETER();

	// Number of attributes stored on the grid
	UPROPERTY(EditAnywhere, Category = "Grid")
	int32 NumAttributes;

	/** Reference to a user parameter if we're reading one. */
	UPROPERTY(EditAnywhere, Category = "Grid3DCollection")
	FNiagaraUserParameterBinding RenderTargetUserParameter;

	/** When enabled overrides the format used to store data inside the grid, otherwise uses the project default setting.  Lower bit depth formats will save memory and performance at the cost of precision. */
	UPROPERTY(EditAnywhere, Category = "Grid3DCollection", meta = (EditCondition = "bOverrideFormat"))
	ENiagaraGpuBufferFormat OverrideBufferFormat;

	UPROPERTY(EditAnywhere, Category = "Grid3DCollection", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverrideFormat : 1;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Grid3DCollection", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bPreviewGrid : 1;

	UPROPERTY(EditAnywhere, Category = "Grid3DCollection", meta = (EditCondition = "bPreviewGrid", ToolTip = "When enabled allows you to preview the grid in a debug display") )
	FName PreviewAttribute = NAME_None;
#endif

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
	virtual int32 PerInstanceDataSize()const override { return sizeof(FGrid3DCollectionRWInstanceData_GameThread); }
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool HasPostSimulateTick() const override { return true; }

	virtual bool CanExposeVariables() const override { return true; }
	virtual void GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const override;
	virtual bool GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const override;
	//~ UNiagaraDataInterface interface END

	// Fills a texture render target 2d with the current data from the simulation
	// #todo(dmp): this will eventually go away when we formalize how data makes it out of Niagara
	// #todo(dmp): reimplement for 3d

	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DeprecatedFunction, DeprecationMessage = "This function has been replaced by object user variables on the emitter to specify render targets to fill with data."))
	virtual bool FillVolumeTexture(const UNiagaraComponent *Component, UVolumeTexture *dest, int AttributeIndex);
	
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DeprecatedFunction, DeprecationMessage = "This function has been replaced by object user variables on the emitter to specify render targets to fill with data."))
	virtual bool FillRawVolumeTexture(const UNiagaraComponent *Component, UVolumeTexture*Dest, int &TilesX, int &TilesY, int &TileZ);
	
	UFUNCTION(BlueprintCallable, Category = Niagara)
	virtual void GetRawTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY, int &SizeZ);

	UFUNCTION(BlueprintCallable, Category = Niagara)
	virtual void GetTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY, int &SizeZ);	

	void GetWorldBBoxSize(FVectorVMContext& Context);
	void GetCellSize(FVectorVMContext& Context);

	void SetNumCells(FVectorVMContext& Context);
	void GetNumCells(FVectorVMContext& Context);

	static const FName SetNumCellsFunctionName;

	static const FString NumTilesName;

	static const FString GridName;
	static const FString OutputGridName;
	static const FString SamplerName;

	static const FName SetValueFunctionName;
	static const FName GetValueFunctionName;
	static const FName SampleGridFunctionName;

	static const FString AnonymousAttributeString;

protected:
	//~ UNiagaraDataInterface interface
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	//~ UNiagaraDataInterface interface END

	TMap<FNiagaraSystemInstanceID, FGrid3DCollectionRWInstanceData_GameThread*> SystemInstancesToProxyData_GT;

	static FNiagaraVariableBase ExposedRTVar;
};
