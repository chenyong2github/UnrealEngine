// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "RHIUtilities.h"

#include "NiagaraDataInterfaceArray.generated.h"

struct INiagaraDataInterfaceArrayImpl
{
	virtual ~INiagaraDataInterfaceArrayImpl() {}
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) const = 0;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) = 0;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) const = 0;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) const = 0;
#if WITH_EDITORONLY_DATA
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) const = 0;
#endif
	virtual bool CopyToInternal(INiagaraDataInterfaceArrayImpl* Destination) const = 0;
	virtual bool Equals(const INiagaraDataInterfaceArrayImpl* Other) const = 0;
	virtual void PushToRenderThread() const = 0;
	virtual FNiagaraDataInterfaceParametersCS* CreateComputeParameters() const = 0;
	virtual const FTypeLayoutDesc* GetComputeParametersTypeDesc() const = 0;
	virtual void BindParameters(FNiagaraDataInterfaceParametersCS* Base, const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap) = 0;
	virtual void SetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const = 0;
	virtual void UnsetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const = 0;
};

UCLASS(abstract, EditInlineNew)
class NIAGARA_API UNiagaraDataInterfaceArray : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	//UObject Interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override { if (Impl) { Impl->GetFunctions(OutFunctions); } }
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override { if (Impl) { Impl->GetVMExternalFunction(BindingInfo, InstanceData, OutFunc); } }
#if WITH_EDITORONLY_DATA
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override { if (Impl) { Impl->GetParameterDefinitionHLSL(ParamInfo, OutHLSL); } }
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override { return Impl ? Impl->GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL) : false; }
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override { return Impl ? Impl->UpgradeFunctionCall(FunctionSignature) : false; }
#endif
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }

	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const;
	virtual bool Equals(const UNiagaraDataInterface* Other) const;

	virtual void PushToRenderThreadImpl() override;

	virtual FNiagaraDataInterfaceParametersCS* CreateComputeParameters() const override { return Impl ? Impl->CreateComputeParameters() : nullptr; }
	virtual const FTypeLayoutDesc* GetComputeParametersTypeDesc() const override { return Impl ? Impl->GetComputeParametersTypeDesc() : nullptr; }
	virtual void BindParameters(FNiagaraDataInterfaceParametersCS* Base, const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap) override { if (Impl) { return Impl->BindParameters(Base, ParameterInfo, ParameterMap); } }
	virtual void SetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override { if (Impl) { return Impl->SetParameters(Base, RHICmdList, Context); } }
	virtual void UnsetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override { if (Impl) { return Impl->UnsetParameters(Base, RHICmdList, Context); } }
	//UNiagaraDataInterface Interface

	/** ReadWrite lock to ensure safe access to the underlying array. */
	FRWLock ArrayRWGuard;

	/** When greater than 0 sets the maximum number of elements the array can hold, only relevant when using operations that modify the array size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category="Array", meta=(ClampMin="0"))
	int32 MaxElements;

protected:
	TUniquePtr<INiagaraDataInterfaceArrayImpl> Impl;
};
