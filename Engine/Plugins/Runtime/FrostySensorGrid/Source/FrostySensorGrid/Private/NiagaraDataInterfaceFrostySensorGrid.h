// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceFrostySensorGrid.generated.h"

/** Data Interface allowing spatial queries on 3d particles uniquely projected onto a 2d grid */
UCLASS(EditInlineNew, meta = (DisplayName = "Frosty Sensor Grid"))
class UNiagaraDataInterfaceFrostySensorGrid : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()
public:

	DECLARE_NIAGARA_DI_PARAMETER();

	UPROPERTY(EditAnywhere, Category = "Sensors")
	int32 SensorCountPerSide = 0;

	UPROPERTY(EditAnywhere, Category = "Sensors")
	float GlobalSensorAccuracy = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Sensors")
	float GlobalSensorRange = 0.0f;

	//UObject Interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//UObject Interface End

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void PushToRenderThreadImpl() override;

	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }

#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual void ModifyCompilationEnvironment(EShaderPlatform ShaderPlatform, struct FShaderCompilerEnvironment& OutEnvironment) const override;
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override;
#endif

	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool HasPostSimulateTick() const override { return true; }

	virtual int32 PerInstanceDataSize() const override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};
