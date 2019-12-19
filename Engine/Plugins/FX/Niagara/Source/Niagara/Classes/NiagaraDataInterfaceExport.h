// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraDataInterface.h"
#include "NiagaraParameterStore.h"
#include "NiagaraDataInterfaceExport.generated.h"

USTRUCT(BlueprintType, Blueprintable)
struct FBasicParticleData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Particle Data")
	FVector Position;

	UPROPERTY(BlueprintReadOnly, Category = "Particle Data")
	float Size;

	UPROPERTY(BlueprintReadOnly, Category = "Particle Data")
	FVector Velocity;
};

UINTERFACE(BlueprintType)
class UNiagaraParticleCallbackHandler : public UInterface
{
	GENERATED_BODY()
};

class INiagaraParticleCallbackHandler
{
	GENERATED_BODY()

public:
	/** This function is called once per tick with the gathered particle data. It will not be called if there is no particle data to call it with. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Niagara")
	void ReceiveParticleData(const TArray<FBasicParticleData>& Data, UNiagaraSystem* NiagaraSystem);
};

struct ExportInterface_InstanceData
{
	UObject* CallbackHandler = nullptr;

	/** We use a lock-free queue here because multiple threads might try to push data to it at the same time. */
	TQueue<FBasicParticleData, EQueueMode::Mpsc> GatheredData;

	/** A binding to the user ptr we're reading the handler from. */
	FNiagaraParameterDirectBinding<UObject*> UserParamBinding;
};

/** This Data Interface can be used to gather particles at execution time and call either a 
C++ or blueprint object with the gathered particle data each tick. */
UCLASS(EditInlineNew, Category = "Export", meta = (DisplayName = "Export particle data"))
class NIAGARA_API UNiagaraDataInterfaceExport : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	/** Reference to a user parameter that should receive the particle data after the simulation tick. The supplied parameter object needs to implement the INiagaraParticleCallbackHandler interface. */
	UPROPERTY(EditAnywhere, Category = "Export")
	FNiagaraUserParameterBinding CallbackHandlerParameter;

	//UObject Interface
	virtual void PostInitProperties() override;
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override { return sizeof(ExportInterface_InstanceData); }
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool GetFunctionHLSL(const FName&  DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::CPUSim; }
	//UNiagaraDataInterface Interface

	virtual void StoreData(FVectorVMContext& Context);

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	
private:
	static const FName StoreDataName;
};
