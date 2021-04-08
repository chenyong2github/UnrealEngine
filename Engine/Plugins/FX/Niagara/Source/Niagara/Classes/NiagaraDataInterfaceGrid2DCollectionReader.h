// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "NiagaraDataInterfaceGrid2DCollection.h"
#include "ClearQuad.h"
#include "NiagaraComponent.h"

#include "NiagaraDataInterfaceGrid2DCollectionReader.generated.h"

class FNiagaraSystemInstance;
class UTextureRenderTarget2D;


struct FGrid2DCollectionReaderInstanceData_GameThread
{
	FNiagaraSystemInstance* SystemInstance = nullptr;
	FNiagaraEmitterInstance* EmitterInstance = nullptr;	

	FString EmitterName;
	FString DIName;
};

struct FGrid2DCollectionReaderInstanceData_RenderThread
{
	FNiagaraDataInterfaceProxyGrid2DCollectionProxy* ProxyToUse = nullptr;
};

struct FNiagaraDataInterfaceProxyGrid2DCollectionReaderProxy : public FNiagaraDataInterfaceProxyRW
{
	FNiagaraDataInterfaceProxyGrid2DCollectionReaderProxy() {}

	virtual FIntVector GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const;

	/* List of proxy data for each system instances*/
	// #todo(dmp): this should all be refactored to avoid duplicate code
	TMap<FNiagaraSystemInstanceID, FGrid2DCollectionReaderInstanceData_RenderThread> SystemInstancesToProxyData_RT;
};

// #todo(dmp): base class has all the RW attributes, even though we only care about the attributes that query the grid.  Cleaning this up would be great
UCLASS(EditInlineNew, Category = "Grid", meta = (DisplayName = "Grid2D Collection Reader", Experimental), Blueprintable, BlueprintType, hidecategories = (Grid,RW))
class NIAGARA_API UNiagaraDataInterfaceGrid2DCollectionReader : public UNiagaraDataInterfaceGrid2D
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();
	
	// Name of the emitter to read from
	UPROPERTY(EditAnywhere, Category = "Reader")
	FString EmitterName;

	// Name of the Grid2DCollection Data Interface on the emitter
	UPROPERTY(EditAnywhere, Category = "Reader")
	FString DIName;

	virtual void PostInitProperties() override;
	
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
	virtual int32 PerInstanceDataSize() const override { return sizeof(FGrid2DCollectionReaderInstanceData_GameThread); }
	
	virtual void GetEmitterDependencies(UNiagaraSystem* Asset, TArray<UNiagaraEmitter*>& Dependencies) const override;
	//~ UNiagaraDataInterface interface END

	static const FString NumTilesName;

	static const FString GridName;
	static const FString OutputGridName;
	static const FString SamplerName;

	static const FName GetValueFunctionName;
	static const FName SampleGridFunctionName;

protected:
	//~ UNiagaraDataInterface interface
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	//~ UNiagaraDataInterface interface END

	TMap<FNiagaraSystemInstanceID, FGrid2DCollectionReaderInstanceData_GameThread*> SystemInstancesToProxyData_GT;	
};
