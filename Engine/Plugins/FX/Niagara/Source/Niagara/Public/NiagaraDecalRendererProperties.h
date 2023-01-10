// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraParameterBinding.h"
#include "NiagaraDecalRendererProperties.generated.h"

class UMaterialInterface;
class FNiagaraEmitterInstance;
class SWidget;

UCLASS(editinlinenew, MinimalAPI, meta = (DisplayName = "Decal Renderer"))
class UNiagaraDecalRendererProperties : public UNiagaraRendererProperties
{
public:
	GENERATED_BODY()

	UNiagaraDecalRendererProperties();

	//UObject Interface
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;
	//UObject Interface END

	static void InitCDOPropertiesAfterModuleStartup();

	//~ UNiagaraRendererProperties interface
	virtual FNiagaraRenderer* CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter, const FNiagaraSystemInstanceController& InController) override;
	virtual class FNiagaraBoundsCalculator* CreateBoundsCalculator() override;
	virtual void GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const override;
	virtual bool IsSimTargetSupported(ENiagaraSimTarget InSimTarget) const override { return InSimTarget == ENiagaraSimTarget::CPUSim; };
#if WITH_EDITORONLY_DATA
	virtual const TArray<FNiagaraVariable>& GetOptionalAttributes() override;
	virtual void GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const override;
	virtual void GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const override;
	virtual void GetRendererFeedback(const FVersionedNiagaraEmitter& InEmitter, TArray<FText>& OutErrors, TArray<FText>& OutWarnings, TArray<FText>& OutInfo) const override;
#endif // WITH_EDITORONLY_DATA
	virtual void CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData) override;
	//UNiagaraRendererProperties Interface END

	UMaterialInterface* GetMaterial(const FNiagaraEmitterInstance* InEmitter) const;

	/** What material to use for the decal. */
	UPROPERTY(EditAnywhere, Category = "Decal Rendering")
	TObjectPtr<UMaterialInterface> Material;

	/** Binding to material. */
	UPROPERTY(EditAnywhere, Category = "Decal Rendering")
	FNiagaraParameterBinding MaterialParameterBinding;

	/** If a render visibility tag is present, particles whose tag matches this value will be visible in this renderer. */
	UPROPERTY(EditAnywhere, Category = "Decal Rendering")
	int32 RendererVisibility = 0;

	/** Position binding for the decals, should be center of the decal */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding PositionBinding;

	/** Orientation binding for the decal. */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding DecalOrientationBinding;

	/** Size binding for the decal. */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding DecalSizeBinding;

	/** When enabled we use the Color Binding as the fade control, otherwise we use the DecalFade binding. */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	bool bUseColorBindingAsFade = true;

	/** Fade binding for the decal, can be queried from the decal fade material node.. */
	UPROPERTY(EditAnywhere, Category = "Bindings", Meta = (EditCondition = "!bUseColorBindingAsFade"))
	FNiagaraVariableAttributeBinding DecalFadeBinding;

	/** Color binding for the decal, only the alpha channel is used and can be queried from the decal fade material node. */
	UPROPERTY(EditAnywhere, Category = "Bindings", Meta = (EditCondition = "bUseColorBindingAsFade"))
	FNiagaraVariableAttributeBinding ColorBinding;

	/** Visibility tag binding, when valid the returned values is compated with RendererVisibility. */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding RendererVisibilityTagBinding;

	FNiagaraDataSetAccessor<FNiagaraPosition>	PositionDataSetAccessor;
	FNiagaraDataSetAccessor<FQuat4f>			DecalOrientationDataSetAccessor;
	FNiagaraDataSetAccessor<FVector3f>			DecalSizeDataSetAccessor;
	FNiagaraDataSetAccessor<float>				DecalFadeDataSetAccessor;
	FNiagaraDataSetAccessor<FLinearColor>		ColorDataSetAccessor;
	FNiagaraDataSetAccessor<int32>				RendererVisibilityTagAccessor;
};
