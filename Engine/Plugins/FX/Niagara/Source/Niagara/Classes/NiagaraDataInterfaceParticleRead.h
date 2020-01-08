// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraDataInterfaceParticleRead.generated.h"

struct FNDIParticleRead_InstanceData
{
	FNiagaraSystemInstance* SystemInstance;
	FNiagaraEmitterInstance* EmitterInstance;
};

UCLASS(EditInlineNew, Category = "ParticleRead", meta = (DisplayName = "Particle Attribute Reader"))
class NIAGARA_API UNiagaraDataInterfaceParticleRead : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "ParticleRead")
	FString EmitterName;

	//UObject Interface
	virtual void PostInitProperties()override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const { return sizeof(FNDIParticleRead_InstanceData); }
	
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds);
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds);

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target)const override { return Target == ENiagaraSimTarget::CPUSim; }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
	virtual bool GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters() const override;

	//UNiagaraDataInterface Interface End

	virtual void ReadFloat(FVectorVMContext& Context, FName AttributeToRead);
	virtual void ReadVector2(FVectorVMContext& Context, FName AttributeToRead);
	virtual void ReadVector3(FVectorVMContext& Context, FName AttributeToRead);
	virtual void ReadVector4(FVectorVMContext& Context, FName AttributeToRead);
	virtual void ReadInt(FVectorVMContext& Context, FName AttributeToRead);
	virtual void ReadBool(FVectorVMContext& Context, FName AttributeToRead);
	virtual void ReadColor(FVectorVMContext& Context, FName AttributeToRead);
	virtual void ReadQuat(FVectorVMContext& Context, FName AttributeToRead);

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

private:
	template<typename T>
	T RetrieveValueWithCheck(FNiagaraEmitterInstance* EmitterInstance, const FNiagaraTypeDefinition& Type, const FName& Attr, const FNiagaraID& ParticleID, bool &bValid);

};