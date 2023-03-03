// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "NiagaraDataInterfaceSparseVolumeTexture.generated.h"

class USparseVolumeTexture;

/** Data Interface allowing sampling of a sparse volume texture */
UCLASS(EditInlineNew, Category = "Texture", meta = (DisplayName = "Sparse Volume Texture Sample"))
class NIAGARA_API UNiagaraDataInterfaceSparseVolumeTexture : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER_TEXTURE(Texture3D<uint>, PageTableTexture)
		SHADER_PARAMETER_TEXTURE(Texture3D, PhysicalTileDataATexture)
		SHADER_PARAMETER_TEXTURE(Texture3D, PhysicalTileDataBTexture)
		SHADER_PARAMETER(FUintVector4, PackedUniforms0)
		SHADER_PARAMETER(FUintVector4, PackedUniforms1)
		SHADER_PARAMETER(FIntVector3, TextureSize)
		SHADER_PARAMETER(int32, MipLevels)
	END_SHADER_PARAMETER_STRUCT()

public:
	UPROPERTY(EditAnywhere, Category = "SparseVolumeTexture")
	TObjectPtr<USparseVolumeTexture> SparseVolumeTexture;

	UPROPERTY(EditAnywhere, Category = "SparseVolumeTexture", meta = (ToolTip = "When valid the user parameter is used as the texture rather than the one on the data interface"))
	FNiagaraUserParameterBinding SparseVolumeTextureUserParameter;

	//UObject Interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }

	virtual int32 PerInstanceDataSize() const override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;

	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	//UNiagaraDataInterface Interface End

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override;
#endif
	virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
	virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;

	void VMGetTextureDimensions(FVectorVMExternalFunctionContext& Context);
	void VMGetNumMipLevels(FVectorVMExternalFunctionContext& Context);

	void SetTexture(USparseVolumeTexture* InSparseVolumeTexture);

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

protected:
	static const TCHAR* TemplateShaderFilePath;
	static const FName LoadSparseVolumeTextureName;
	static const FName SampleSparseVolumeTextureName;
	static const FName GetTextureDimensionsName;
	static const FName GetNumMipLevelsName;
};
