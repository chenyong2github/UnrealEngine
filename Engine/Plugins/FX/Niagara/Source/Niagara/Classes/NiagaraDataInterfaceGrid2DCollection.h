// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "ClearQuad.h"
#include "NiagaraComponent.h"

#include "NiagaraDataInterfaceGrid2DCollection.generated.h"

class FNiagaraSystemInstance;
class UTextureRenderTarget2D;

class FGrid2DBuffer
{
public:
	FGrid2DBuffer(int NumX, int NumY)
	{		
		GridBuffer.Initialize(4, NumX, NumY, EPixelFormat::PF_R32_FLOAT);
	}

	FTextureRWBuffer2D GridBuffer;	
};

struct FGrid2DCollectionRWInstanceData_GameThread
{
	FIntPoint NumCells = FIntPoint(EForceInit::ForceInitToZero);
	FIntPoint NumTiles = FIntPoint(EForceInit::ForceInitToZero);
	FVector2D CellSize = FVector2D::ZeroVector;
	FVector2D WorldBBoxSize = FVector2D::ZeroVector;
};

struct FGrid2DCollectionRWInstanceData_RenderThread
{
	FIntPoint NumCells = FIntPoint(EForceInit::ForceInitToZero);
	FIntPoint NumTiles = FIntPoint(EForceInit::ForceInitToZero);
	FVector2D CellSize = FVector2D::ZeroVector;
	FVector2D WorldBBoxSize = FVector2D::ZeroVector;

	TArray<TUniquePtr<FGrid2DBuffer>> Buffers;
	FGrid2DBuffer* CurrentData = nullptr;
	FGrid2DBuffer* DestinationData = nullptr;

	void BeginSimulate();
	void EndSimulate();
};

struct FNiagaraDataInterfaceProxyGrid2DCollectionProxy : public FNiagaraDataInterfaceProxyRW
{
	FNiagaraDataInterfaceProxyGrid2DCollectionProxy() {}

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;
	virtual void PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;
	virtual void ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;

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
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override { return false; }
	virtual int32 PerInstanceDataSize()const override { return sizeof(FGrid2DCollectionRWInstanceData_GameThread); }
	//~ UNiagaraDataInterface interface END

	// Fills a texture render target 2d with the current data from the simulation
	// #todo(dmp): this will eventually go away when we formalize how data makes it out of Niagara
	UFUNCTION(BlueprintCallable, Category = Niagara)
	virtual bool FillTexture2D(const UNiagaraComponent *Component, UTextureRenderTarget2D *dest, int AttributeIndex);
	
	UFUNCTION(BlueprintCallable, Category = Niagara)
	virtual bool FillRawTexture2D(const UNiagaraComponent *Component, UTextureRenderTarget2D *Dest, int &TilesX, int &TilesY);
	
	UFUNCTION(BlueprintCallable, Category = Niagara)
	virtual void GetRawTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY);

	UFUNCTION(BlueprintCallable, Category = Niagara)
	virtual void GetTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY);

	void GetWorldBBoxSize(FVectorVMContext& Context);
	void GetCellSize(FVectorVMContext& Context);

protected:
	//~ UNiagaraDataInterface interface
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	//~ UNiagaraDataInterface interface END

	TMap<FNiagaraSystemInstanceID, FGrid2DCollectionRWInstanceData_GameThread*> SystemInstancesToProxyData_GT;
};
