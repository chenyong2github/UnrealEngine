// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraDataInterface.h"
#include "NiagaraParameterStore.h"
#include "NiagaraDataInterfaceMeshRendererInfo.generated.h"

class UNiagaraMeshRendererProperties;
class FNDIMeshRendererInfo;

using FNDIMeshRendererInfoRef = TSharedRef<FNDIMeshRendererInfo, ESPMode::ThreadSafe>;
using FNDIMeshRendererInfoPtr = TSharedPtr<FNDIMeshRendererInfo, ESPMode::ThreadSafe>;

/** This Data Interface can be used to query information about the mesh renderers of an emitter */
UCLASS(EditInlineNew, Category = "Mesh Particles", meta = (DisplayName = "Mesh Renderer Info"))
class NIAGARA_API UNiagaraDataInterfaceMeshRendererInfo : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters,)
		SHADER_PARAMETER(uint32,			NumMeshes)
		SHADER_PARAMETER_SRV(Buffer<float>,	MeshDataBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	UNiagaraMeshRendererProperties* GetMeshRenderer() const { return MeshRenderer; }

	//UObject Interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR	
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif 
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif
	virtual bool UseLegacyShaderBindings() const override { return false; }
	virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
	virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;

	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
#if WITH_EDITOR
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override;
	virtual void GetFeedback(UNiagaraSystem* InAsset, UNiagaraComponent* InComponent, TArray<FNiagaraDataInterfaceError>& OutErrors, TArray<FNiagaraDataInterfaceFeedback>& OutWarnings,
		TArray<FNiagaraDataInterfaceFeedback>& OutInfo) override;
#endif
	//UNiagaraDataInterface Interface	

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	virtual void PushToRenderThreadImpl() override;

	void GetNumMeshes(FVectorVMExternalFunctionContext& Context);
	void GetMeshLocalBounds(FVectorVMExternalFunctionContext& Context);

	/** The name of the mesh renderer */
	UPROPERTY(EditAnywhere, Category = "Source")
	TObjectPtr<UNiagaraMeshRendererProperties> MeshRenderer;

	FNDIMeshRendererInfoPtr Info;
};
