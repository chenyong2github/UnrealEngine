// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "ClearQuad.h"
#include "NiagaraComponent.h"

#include "NiagaraDataInterfaceGrid2DCollection.generated.h"

class FNiagaraSystemInstance;
class UTextureRenderTarget2D;

class Grid2DBuffer
{
public:
	Grid2DBuffer(int NumX, int NumY)
	{		
		GridBuffer.Initialize(4, NumX, NumY, EPixelFormat::PF_R32_FLOAT);
	}

	FTextureRWBuffer2D GridBuffer;	
};

class Grid2DCollectionRWInstanceData
{
public:

	int NumCellsX;
	int NumCellsY;
	int NumAttributes;
	int NumTilesX;
	int NumTilesY;

	FVector2D CellSize;
	bool SetGridFromVoxelSize;
	
	FVector2D WorldBBoxSize;

	TArray<Grid2DBuffer*> Buffers;

	Grid2DBuffer* CurrentData;
	Grid2DBuffer* DestinationData;

	Grid2DCollectionRWInstanceData() : CurrentData(nullptr), DestinationData(nullptr) {}

	~Grid2DCollectionRWInstanceData()
	{
		for (Grid2DBuffer* Buffer : Buffers)
		{
			Buffer->GridBuffer.Release();
			delete Buffer;
		}
	}

	Grid2DBuffer* GetCurrentData() { return CurrentData; }
	Grid2DBuffer* GetDestinationData() { return DestinationData; }
	Grid2DBuffer &BeginSimulate()
	{
		for (Grid2DBuffer* Buffer : Buffers)
		{
			check(Buffer);
			if (Buffer != CurrentData)
			{
				DestinationData = Buffer;
				break;
			}
		}

		if (DestinationData == nullptr)
		{
			DestinationData = new Grid2DBuffer(NumCellsX * NumTilesX, NumCellsY * NumTilesY);
			Buffers.Add(DestinationData);
		}

		return *DestinationData;
	}

	void EndSimulate()
	{
		CurrentData = DestinationData;
		DestinationData = nullptr;
	}
};

struct FNiagaraDataInterfaceProxyGrid2DCollection : public FNiagaraDataInterfaceProxyRW
{
	FNiagaraDataInterfaceProxyGrid2DCollection() {}		

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;
	virtual void PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;
	virtual void ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;

	virtual void DeferredDestroy() override;		
	void DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance);

	/* List of proxy data for each system instances*/
	// #todo(dmp): this should all be refactored to avoid duplicate code
	TMap<FNiagaraSystemInstanceID, Grid2DCollectionRWInstanceData> SystemInstancesToProxyData;

	/* List of proxy data to destroy later */
	// #todo(dmp): this should all be refactored to avoid duplicate code
	TSet<FNiagaraSystemInstanceID> DeferredDestroyList;
};

UCLASS(EditInlineNew, Category = "Grid", meta = (DisplayName = "Grid2D Collection"), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceGrid2DCollection : public UNiagaraDataInterfaceGrid2D
{
	GENERATED_UCLASS_BODY()

public:
	
	virtual void PostInitProperties() override;
	
	//~ UNiagaraDataInterface interface
	// VM functionality
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters() const override;
	
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override {}
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override { return false; }
	virtual int32 PerInstanceDataSize()const override { return sizeof(Grid2DCollectionRWInstanceData); }
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
};


