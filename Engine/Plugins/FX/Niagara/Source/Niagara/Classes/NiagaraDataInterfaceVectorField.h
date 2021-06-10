// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "VectorField/VectorField.h"
#include "NiagaraDataInterfaceVectorField.generated.h"

class FNiagaraSystemInstance;

UCLASS(EditInlineNew, Category = "Vector Field", meta = (DisplayName = "Vector Field"))
class NIAGARA_API UNiagaraDataInterfaceVectorField : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	/** Vector field to sample from. */
	UPROPERTY(EditAnywhere, Category = VectorField)
	UVectorField* Field;

	UPROPERTY(EditAnywhere, Category = VectorField)
	bool bTileX;
	UPROPERTY(EditAnywhere, Category = VectorField)
	bool bTileY;
	UPROPERTY(EditAnywhere, Category = VectorField)
	bool bTileZ;

public:
	//~ UObject interface

	DECLARE_NIAGARA_DI_PARAMETER();

	virtual void PostInitProperties() override;
	virtual void PostLoad() override; 
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
#endif
	//~ UObject interface END

	//~ UNiagaraDataInterface interface
	// VM functionality
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override;

#if WITH_EDITOR	
	// Editor functionality
	virtual void GetFeedback(UNiagaraSystem* InAsset, UNiagaraComponent* InComponent, TArray<FNiagaraDataInterfaceError>& OutErrors, TArray<FNiagaraDataInterfaceFeedback>& OutWarnings, TArray<FNiagaraDataInterfaceFeedback>& OutInfo) override;

#endif

	// GPU sim functionality
#if WITH_EDITORONLY_DATA
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif
	//~ UNiagaraDataInterface interface END

	// VM functions
	void GetFieldDimensions(FVectorVMContext& Context);
	void GetFieldBounds(FVectorVMContext& Context); 
	void GetFieldTilingAxes(FVectorVMContext& Context);
	void SampleVectorField(FVectorVMContext& Context);
	
	//	
	FVector GetTilingAxes() const;
	FVector GetDimensions() const;
	FVector GetMinBounds() const;
	FVector GetMaxBounds() const;
protected:
	//~ UNiagaraDataInterface interface
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	//~ UNiagaraDataInterface interface END

	virtual void PushToRenderThreadImpl() override;
};

struct FNiagaraDataInterfaceProxyVectorField : public FNiagaraDataInterfaceProxy
{
	FVector Dimensions;
	FVector MinBounds;
	FVector MaxBounds;
	bool bTileX;
	bool bTileY;
	bool bTileZ;
	FTextureRHIRef TextureRHI;

	FVector GetTilingAxes() const
	{
		return FVector(float(bTileX), float(bTileY), float(bTileZ));
	}

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override { check(false); }
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}
};

struct FNiagaraDataInterfaceParametersCS_VectorField : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_VectorField, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap);
	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const;

private:
	LAYOUT_FIELD(FShaderResourceParameter, VectorFieldSampler);
	LAYOUT_FIELD(FShaderResourceParameter, VectorFieldTexture);
	LAYOUT_FIELD(FShaderParameter, TilingAxes);
	LAYOUT_FIELD(FShaderParameter, Dimensions);
	LAYOUT_FIELD(FShaderParameter, MinBounds);
	LAYOUT_FIELD(FShaderParameter, MaxBounds);
};
