// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "NiagaraDataInterfaceVolumeTexture.generated.h"

class UVolumeTexture;

/** Data Interface allowing sampling of a texture */
UCLASS(EditInlineNew, Category = "Texture", meta = (DisplayName = "Volume Texture Sample"))
class NIAGARA_API UNiagaraDataInterfaceVolumeTexture : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()
public:

	DECLARE_NIAGARA_DI_PARAMETER();

	UPROPERTY(EditAnywhere, Category = "Texture")
	UVolumeTexture* Texture;

	//UObject Interface
	virtual void PostInitProperties()override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target)const override { return Target == ENiagaraSimTarget::GPUComputeSim; }

	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	//UNiagaraDataInterface Interface End

	void SampleVolumeTexture(FVectorVMContext& Context);
	void GetTextureDimensions(FVectorVMContext& Context);

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
#if WITH_EDITORONLY_DATA
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif

	//FRWBuffer& GetGPUBuffer();
	static const FString TextureName;
	static const FString SamplerName;
	static const FString DimensionsBaseName;

	void SetTexture(UVolumeTexture* InTexture);

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

	virtual void PushToRenderThreadImpl() override;

protected:
	FIntVector TextureSize = FIntVector::ZeroValue;

	static const FName SampleVolumeTextureName;
	static const FName TextureDimsName;
};

struct FNiagaraDataInterfaceProxyVolumeTexture : public FNiagaraDataInterfaceProxy
{
	FSamplerStateRHIRef SamplerStateRHI;
	FTextureRHIRef TextureRHI;
	FVector TexDims;

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override { check(false); }
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}
};