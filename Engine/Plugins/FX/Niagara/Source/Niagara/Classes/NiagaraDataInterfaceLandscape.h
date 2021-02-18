// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"

#include "NiagaraDataInterfaceLandscape.generated.h"

class ALandscape;
struct FNDILandscapeData_GameThread;

UENUM()
enum class ENDILandscape_SourceMode : uint8
{
	/**
	Default behavior.
	- Use "Source" when explicitly specified.
	- When no source is specified, fall back on attached actor or component or world.
	*/
	Default,

	/**
	Only use "Source".
	*/
	Source,

	/**
	Only use the parent actor or component the system is attached to.
	*/
	AttachParent
};

/** Data Interface allowing sampling of a Landscape */
UCLASS(EditInlineNew, Category = "Landscape", meta = (DisplayName = "Landscape Sample"))
class NIAGARA_API UNiagaraDataInterfaceLandscape : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()
public:

	DECLARE_NIAGARA_DI_PARAMETER();

	UPROPERTY(EditAnywhere, Category = "Landscape")
	TObjectPtr<AActor> SourceLandscape;

	UPROPERTY(EditAnywhere, Category = "Landscape")
	ENDILandscape_SourceMode SourceMode = ENDILandscape_SourceMode::Default;

	//UObject Interface
	virtual void PostInitProperties() override;	
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
#if WITH_EDITORONLY_DATA
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override;
#endif
	//UNiagaraDataInterface Interface End

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;	
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool HasPreSimulateTick() const override { return true; }

	ALandscape* GetLandscape(const FNiagaraSystemInstance& SystemInstance) const;
	
	static const FString HeightVirtualTextureEnabledName;
	static const FString HeightVirtualTextureName;
	static const FString HeightVirtualTexturePageTableName;
	static const FString HeightVirtualTexturePageTableUniform0Name;
	static const FString HeightVirtualTexturePageTableUniform1Name;
	static const FString HeightVirtualTextureSamplerName;
	static const FString HeightVirtualTextureUniformsName;
	static const FString HeightVirtualTextureWorldToUvTransformName;

	static const FString CachedHeightTextureEnabledName;
	static const FString CachedHeightTextureName;
	static const FString CachedHeightTextureSamplerName;
	static const FString CachedHeightTextureUvScaleBiasName;
	static const FString CachedHeightTextureWorldToUvTransformName;

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	void ApplyLandscape(const FNiagaraSystemInstance& SystemInstance, FNDILandscapeData_GameThread& InstanceData) const;
		
	static const FName GetHeightName;
};