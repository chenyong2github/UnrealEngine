// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterfaceRW.h"
#include "ClearQuad.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceRenderTargetVolume.generated.h"

class FNiagaraSystemInstance;
class UTextureRenderTargetVolume;


struct FRenderTargetVolumeRWInstanceData_GameThread
{
	FIntVector Size = FIntVector(EForceInit::ForceInitToZero);
	
	UTextureRenderTargetVolume* TargetTexture = nullptr;

};

struct FRenderTargetVolumeRWInstanceData_RenderThread
{
	FIntVector Size = FIntVector(EForceInit::ForceInitToZero);
	
	FTextureReferenceRHIRef TextureReferenceRHI;
	FUnorderedAccessViewRHIRef UAV;
};

struct FNiagaraDataInterfaceProxyRenderTargetVolumeProxy : public FNiagaraDataInterfaceProxy
{
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }
	virtual void PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) override;

	/* List of proxy data for each system instances*/
	// #todo(dmp): this should all be refactored to avoid duplicate code
	TMap<FNiagaraSystemInstanceID, FRenderTargetVolumeRWInstanceData_RenderThread> SystemInstancesToProxyData_RT;
};

UCLASS(EditInlineNew, Category = "Grid", meta = (DisplayName = "Render Target Volume", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceRenderTargetVolume : public UNiagaraDataInterfaceRWBase
{
	GENERATED_UCLASS_BODY()

public:
	DECLARE_NIAGARA_DI_PARAMETER();	
		
	virtual void PostInitProperties() override;
	
	//~ UNiagaraDataInterface interface
	// VM functionality
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target)const override { return true; }
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

	// GPU sim functionality
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override {}
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FRenderTargetVolumeRWInstanceData_GameThread); }
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance, float DeltaSeconds) override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool HasPostSimulateTick() const override { return true; }

	virtual bool CanExposeVariables() const override { return true;}
	virtual void GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const override;
	virtual bool GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const override;
	//~ UNiagaraDataInterface interface END
	
	void GetSize(FVectorVMContext& Context); 
	void SetSize(FVectorVMContext& Context);

	static const FName SetValueFunctionName;
	static const FName SetSizeFunctionName;
	static const FName GetSizeFunctionName;
	static const FName LinearToIndexName;

	static const FString RWOutputName;
	static const FString OutputName;
	static const FString SizeName;

	UPROPERTY(EditAnywhere, Category = "Render Target")
	FIntVector Size;

protected:

	//~ UNiagaraDataInterface interface END

	static FNiagaraVariableBase ExposedRTVar;
	
	UPROPERTY(Transient)
	TMap<uint64, UTextureRenderTargetVolume*> ManagedRenderTargets;
};
