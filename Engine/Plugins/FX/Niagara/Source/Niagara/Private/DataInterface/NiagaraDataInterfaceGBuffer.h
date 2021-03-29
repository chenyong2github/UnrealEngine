// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceGBuffer.generated.h"

UCLASS(Experimental, EditInlineNew, Category = "Camera", meta = (DisplayName = "GBuffer"))
class NIAGARA_API UNiagaraDataInterfaceGBuffer : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	//UObject Interface
	virtual void PostInitProperties() override;
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;

#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif

	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool RequiresDepthBuffer() const override { return true; }
	//UNiagaraDataInterface Interface
};
