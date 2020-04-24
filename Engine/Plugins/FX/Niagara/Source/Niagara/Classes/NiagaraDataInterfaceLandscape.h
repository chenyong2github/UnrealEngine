// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"

#include "NiagaraDataInterfaceLandscape.generated.h"

// Landscape data used on the render thread
struct FNDILandscapeData_RenderThread
{	
	TUniquePtr<FTextureReadBuffer2D> LandscapeTextureBuffer = nullptr;
	FIntPoint NumCells = FIntPoint(EForceInit::ForceInitToZero);
	FMatrix WorldToActorTransform = FMatrix::Identity;
	bool IsSet = false;
};

// Landscape data used for the game thread
struct FNDILandscapeData_GameThread
{
	// #todo(dmp): CPU particles access via a TArray?
	// TArray<float> Values;

	FIntPoint NumCells = FIntPoint(EForceInit::ForceInitToZero);
	FMatrix WorldToActorTransform = FMatrix::Identity;
	bool IsSet = false;
};

/** Data Interface allowing sampling of a Landscape */
UCLASS(EditInlineNew, Category = "Landscape", meta = (DisplayName = "Landscape Sample"))
class NIAGARA_API UNiagaraDataInterfaceLandscape : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()
public:

	DECLARE_NIAGARA_DI_PARAMETER();

	UPROPERTY(EditAnywhere, Category = "Landscape")
	AActor* SourceLandscape;

	//UObject Interface
	virtual void PostInitProperties() override;	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif	
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target)const override { return Target==ENiagaraSimTarget::GPUComputeSim; }
	//UNiagaraDataInterface Interface End

	void EmptyVMFunction(FVectorVMContext& Context) {}

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;	

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override {}
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize() const override { return sizeof(FNDILandscapeData_GameThread); }	
	
	static const FString LandscapeTextureName;
	static const FString SamplerName;
	static const FString NumCellsBaseName;
	static const FString WorldToActorBaseName;
protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
		
	static const FName GetHeightName;	
	static const FName GetNumCellsName;
};

struct FNiagaraDataInterfaceProxyLandscape : public FNiagaraDataInterfaceProxy
{

	FNiagaraDataInterfaceProxyLandscape() {}
	
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override	{ check(false);	}

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }

	TMap<FNiagaraSystemInstanceID, FNDILandscapeData_RenderThread> SystemInstancesToProxyData_RT;
};