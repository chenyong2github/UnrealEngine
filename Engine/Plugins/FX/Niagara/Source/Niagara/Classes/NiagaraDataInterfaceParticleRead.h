// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterfaceRW.h"
#include "NiagaraCommon.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraDataInterfaceParticleRead.generated.h"

UCLASS(EditInlineNew, Category = "ParticleRead", meta = (DisplayName = "Particle Attribute Reader"))
class NIAGARA_API UNiagaraDataInterfaceParticleRead : public UNiagaraDataInterfaceRWBase
{
	GENERATED_UCLASS_BODY()
public:
	DECLARE_NIAGARA_DI_PARAMETER();

	UPROPERTY(EditAnywhere, Category = "ParticleRead")
	FString EmitterName;

	//UObject Interface
	virtual void PostInitProperties()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent);
#endif
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override;
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
#if WITH_EDITORONLY_DATA
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;
#if WITH_EDITOR	
	virtual void GetFeedback(UNiagaraSystem* Asset, UNiagaraComponent* Component, TArray<FNiagaraDataInterfaceError>& OutErrors, TArray<FNiagaraDataInterfaceFeedback>& Warnings, TArray<FNiagaraDataInterfaceFeedback>& Info) override;
#endif
#if WITH_EDITORONLY_DATA
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override;
#endif
	virtual void GetEmitterDependencies(UNiagaraSystem* Asset, TArray<UNiagaraEmitter*>& Dependencies) const override;
	virtual bool ReadsEmitterParticleData(const FString& EmitterName) const override;

	virtual bool HasInternalAttributeReads(const UNiagaraEmitter* OwnerEmitter, const UNiagaraEmitter* Provider) const override;
	//UNiagaraDataInterface Interface End

	void GetNumSpawnedParticles(FVectorVMContext& Context);
	void GetSpawnedIDAtIndex(FVectorVMContext& Context);
	void GetNumParticles(FVectorVMContext& Context);
	void GetParticleIndex(FVectorVMContext& Context);
	void GetParticleIndexFromIDTable(FVectorVMContext& Context);
	void ReadInt(FVectorVMContext& Context, FName AttributeToRead);
	void ReadBool(FVectorVMContext& Context, FName AttributeToRead);
	void ReadFloat(FVectorVMContext& Context, FName AttributeToRead);
	void ReadVector2(FVectorVMContext& Context, FName AttributeToRead);
	void ReadVector3(FVectorVMContext& Context, FName AttributeToRead);
	void ReadVector4(FVectorVMContext& Context, FName AttributeToRead);
	void ReadColor(FVectorVMContext& Context, FName AttributeToRead);
	void ReadQuat(FVectorVMContext& Context, FName AttributeToRead);
	void ReadID(FVectorVMContext& Context, FName AttributeToRead);
	void ReadIntByIndex(FVectorVMContext& Context, FName AttributeToRead);
	void ReadBoolByIndex(FVectorVMContext& Context, FName AttributeToRead);
	void ReadFloatByIndex(FVectorVMContext& Context, FName AttributeToRead);
	void ReadVector2ByIndex(FVectorVMContext& Context, FName AttributeToRead);
	void ReadVector3ByIndex(FVectorVMContext& Context, FName AttributeToRead);
	void ReadVector4ByIndex(FVectorVMContext& Context, FName AttributeToRead);
	void ReadColorByIndex(FVectorVMContext& Context, FName AttributeToRead);
	void ReadQuatByIndex(FVectorVMContext& Context, FName AttributeToRead);
	void ReadIDByIndex(FVectorVMContext& Context, FName AttributeToRead);

protected:
	void GetPersistentIDFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions);
	void GetIndexFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions);
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};