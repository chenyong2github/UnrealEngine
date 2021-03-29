// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "NiagaraDataInterfaceRW.h"
#include "ClearQuad.h"
#include "NiagaraStats.h"

#include "NiagaraDataInterfaceNeighborGrid3D.generated.h"

class FNiagaraSystemInstance;

// store all data in in a class
// move all data management to use per instance data
// remove references to push data to render thread

class NeighborGrid3DRWInstanceData
{
public:

	~NeighborGrid3DRWInstanceData()
	{
#if STATS
		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GPUMemory);
#endif
	}

	void ResizeBuffers()
	{
		const int32 NumTotalCells = NumCells.X * NumCells.Y * NumCells.Z;
		const int32 NumIntsInGridBuffer = NumTotalCells * MaxNeighborsPerCell;

		NeighborhoodCountBuffer.Initialize(sizeof(int32), NumTotalCells, EPixelFormat::PF_R32_SINT, BUF_Static, TEXT("NiagaraNeighborGrid3D::NeighborCount"));
		NeighborhoodBuffer.Initialize(sizeof(int32), NumIntsInGridBuffer, EPixelFormat::PF_R32_SINT, BUF_Static, TEXT("NiagaraNeighborGrid3D::NeighborsGrid"));

#if STATS
		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GPUMemory);
		GPUMemory = NumTotalCells * sizeof(int32) + NumIntsInGridBuffer * sizeof(int32);
		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GPUMemory);
#endif
	}

	FIntVector NumCells;
	float CellSize;
	bool SetGridFromCellSize;
	uint32 MaxNeighborsPerCell;	
	FVector WorldBBoxSize;

	bool NeedsRealloc = false;

	FRWBuffer NeighborhoodBuffer;
	FRWBuffer NeighborhoodCountBuffer;

#if STATS
	int32 GPUMemory = 0;
#endif
};

struct FNiagaraDataInterfaceProxyNeighborGrid3D : public FNiagaraDataInterfaceProxyRW
{	
	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override {}
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(NeighborGrid3DRWInstanceData); }	

	virtual FIntVector GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const override;

	/* List of proxy data for each system instances*/
	// #todo(dmp): this should all be refactored to avoid duplicate code
	TMap<FNiagaraSystemInstanceID, NeighborGrid3DRWInstanceData> SystemInstancesToProxyData;
};

UCLASS(EditInlineNew, Category = "Grid", meta = (DisplayName = "Neighbor Grid3D"))
class NIAGARA_API UNiagaraDataInterfaceNeighborGrid3D : public UNiagaraDataInterfaceGrid3D
{
	GENERATED_UCLASS_BODY()

public:	

	UPROPERTY(EditAnywhere, Category = "Grid")
		uint32 MaxNeighborsPerCell;

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	virtual void PostInitProperties() override
	{
		Super::PostInitProperties();

		//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
		if (HasAnyFlags(RF_ClassDefaultObject))
		{
			ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
			FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
		}
	}

	//~ UNiagaraDataInterface interface
	// VM functionality
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
#if WITH_EDITORONLY_DATA
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override {}
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override { return false;  }
	virtual int32 PerInstanceDataSize()const override { return sizeof(NeighborGrid3DRWInstanceData); }
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool HasPostSimulateTick() const override { return true; }
	virtual bool HasPreSimulateTick() const override { return true; }
	//~ UNiagaraDataInterface interface END

	void GetWorldBBoxSize(FVectorVMContext& Context);
	void GetNumCells(FVectorVMContext& Context);
	void GetMaxNeighborsPerCell(FVectorVMContext& Context);
	void SetNumCells(FVectorVMContext& Context);

	static const FName SetNumCellsFunctionName;


protected:
	//~ UNiagaraDataInterface interface
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	//~ UNiagaraDataInterface interface END	
};


