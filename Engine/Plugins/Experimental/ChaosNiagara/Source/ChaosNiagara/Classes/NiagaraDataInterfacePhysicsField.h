// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "Field/FieldSystem.h"

#include "NiagaraDataInterfacePhysicsField.generated.h"

/** Data stored per physics asset instance*/
struct FNDIPhysicsFieldData
{
	/** Initialize the resource */
	void Init(FNiagaraSystemInstance* SystemInstance);

	/** Update the commands */
	void Update(FNiagaraSystemInstance* SystemInstance);

	/** Release the buffers */
	void Release();

	/** Field render resource for gpu */
	class FPhysicsFieldResource* FieldResource;

	/** Field system commands for cpu */
	TArray<FFieldSystemCommand>	FieldCommands;

	/** Time in seconds*/
	float TimeSeconds = 0.0;
};

/** Data Interface for the strand base */
UCLASS(EditInlineNew, Category = "Chaos", meta = (DisplayName = "Physics Field"))
class CHAOSNIAGARA_API UNiagaraDataInterfacePhysicsField : public UNiagaraDataInterface
{
	GENERATED_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	UNiagaraDataInterfacePhysicsField();

	/** UObject Interface */
	virtual void PostInitProperties() override;

	/** UNiagaraDataInterface Interface */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FNDIPhysicsFieldData); }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool HasPostSimulateTick() const override { return true; }

	/** GPU simulation functionality */
#if WITH_EDITORONLY_DATA
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

	/** Sample the vector field */
	void SamplePhysicsVectorField(FVectorVMContext& Context);

	/** Sample the scalar field */
	void SamplePhysicsScalarField(FVectorVMContext& Context);

	/** Sample the integer field */
	void SamplePhysicsIntegerField(FVectorVMContext& Context);

	/** Get the field resolution */
	void GetPhysicsFieldResolution(FVectorVMContext& Context);

	/** Get the field bounds */
	void GetPhysicsFieldBounds(FVectorVMContext& Context);

	/** Shader attributes names */
	static const FString ClipmapBufferName;
	static const FString ClipmapCenterName;
	static const FString ClipmapDistanceName;
	static const FString ClipmapResolutionName;
	static const FString ClipmapExponentName;
	static const FString ClipmapCountName;
	static const FString TargetCountName;
	static const FString VectorTargetsName;
	static const FString ScalarTargetsName;
	static const FString IntegerTargetsName;

protected:
	/** Copy one niagara DI to this */
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};

/** Proxy to send data to gpu */
struct FNDIPhysicsFieldProxy : public FNiagaraDataInterfaceProxy
{
	/** Get the size of the data that will be passed to render*/
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIPhysicsFieldData); }

	/** Get the data that will be passed to render*/
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;

	/** Initialize the per instance data */
	void InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance);

	/** Destroy the proxy data if necessary */
	void DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance);

	/** List of proxy data for each system instances*/
	TMap<FNiagaraSystemInstanceID, FNDIPhysicsFieldData> SystemInstancesToProxyData;
};

