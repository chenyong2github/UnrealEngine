// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraCollision.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraShared.h"
#include "VectorVM.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceCollisionQuery.generated.h"

class INiagaraCompiler;
class FNiagaraSystemInstance;


struct CQDIPerInstanceData
{
	FNiagaraSystemInstance *SystemInstance;
	FNiagaraDICollisionQueryBatch CollisionBatch;
};


/** Data Interface allowing sampling of color curves. */
UCLASS(EditInlineNew, Category = "Collision", meta = (DisplayName = "Collision Query"))
class NIAGARA_API UNiagaraDataInterfaceCollisionQuery : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()
public:

	DECLARE_NIAGARA_DI_PARAMETER();

	UPROPERTY(EditAnywhere, Category = "Ray Trace")
	int32 MaxRayTraceCount = 0;

	FNiagaraSystemInstance *SystemInstance;

	//UObject Interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//UObject Interface End

	/** Initializes the per instance data for this interface. Returns false if there was some error and the simulation should be disabled. */
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance) override;
	/** Destroys the per instence data for this interface. */
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance) override;

	/** Ticks the per instance data for this interface, if it has any. */
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize() const override { return sizeof(CQDIPerInstanceData); }
	
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual void GetAssetTagsForContext(const UObject* InAsset, const TArray<const UNiagaraDataInterface*>& InProperties, TMap<FName, uint32>& NumericKeys, TMap<FName, FString>& StringKeys) const override;
	virtual void PushToRenderThreadImpl() override;

	// VM functions
	void PerformQuerySyncCPU(FVectorVMContext& Context);
	void PerformQueryAsyncCPU(FVectorVMContext& Context);
	void QuerySceneDepth(FVectorVMContext& Context);
	void QueryMeshDistanceField(FVectorVMContext& Context);

	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
	virtual bool RequiresDistanceFieldData() const override { return true; }
	virtual bool RequiresDepthBuffer() const override { return true; }
	virtual bool RequiresRayTracingScene() const override;

#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual void ModifyCompilationEnvironment(EShaderPlatform ShaderPlatform, struct FShaderCompilerEnvironment& OutEnvironment) const override;
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override;
#endif

#if WITH_EDITOR	
	virtual void ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors) override;
#endif
	
	virtual bool HasPreSimulateTick() const override{ return true; }
	virtual bool HasPostSimulateTick() const override { return true; }

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

private:

	static FCriticalSection CriticalSection;
	UEnum* TraceChannelEnum;
};
