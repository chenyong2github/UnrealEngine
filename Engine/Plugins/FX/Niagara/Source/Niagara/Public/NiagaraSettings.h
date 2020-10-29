// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "NiagaraSettings.generated.h"


UCLASS(config = Niagara, defaultconfig, meta=(DisplayName="Niagara"))
class NIAGARA_API UNiagaraSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY(config, EditAnywhere, Category = Niagara, meta = (AllowedClasses = "ScriptStruct"))
	TArray<FSoftObjectPath> AdditionalParameterTypes;

	UPROPERTY(config, EditAnywhere, Category = Niagara, meta = (AllowedClasses = "ScriptStruct"))
	TArray<FSoftObjectPath> AdditionalPayloadTypes;

	UPROPERTY(config, EditAnywhere, Category = Niagara, meta = (AllowedClasses = "Enum"))
	TArray<FSoftObjectPath> AdditionalParameterEnums;
#endif // WITH_EDITORONLY_DATA

	/** Default effect type to use for effects that don't define their own. Can be null. */
	UPROPERTY(config, EditAnywhere, Category = Niagara, meta = (AllowedClasses = "NiagaraEffectType"))
	FSoftObjectPath DefaultEffectType;

	/** The quality levels Niagara uses. */
	UPROPERTY(config, EditAnywhere, Category = Scalability)
	TArray<FText> QualityLevels;

	/** Info texts that the component renderer shows the user depending on the selected component class. */
	UPROPERTY(config, EditAnywhere, Category = Renderer)
	TMap<FString, FText> ComponentRendererWarningsPerClass;

	/** The default render target format used by all Niagara Render Target Data Interfaces unless overridden. */
	UPROPERTY(config, EditAnywhere, Category = Renderer)
	TEnumAsByte<ETextureRenderTargetFormat> DefaultRenderTargetFormat = RTF_RGBA16f;

	/** The default buffer format used by all Niagara Grid Data Interfaces unless overridden. */
	UPROPERTY(config, EditAnywhere, Category = Renderer)
	ENiagaraGpuBufferFormat DefaultGridFormat = ENiagaraGpuBufferFormat::HalfFloat;

	// Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif
	// END UDeveloperSettings Interface

	UNiagaraEffectType* GetDefaultEffectType()const;

	virtual void PostInitProperties();
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnNiagaraSettingsChanged, const FName&, const UNiagaraSettings*);

	/** Gets a multicast delegate which is called whenever one of the parameters in this settings object changes. */
	static FOnNiagaraSettingsChanged& OnSettingsChanged();

protected:
	static FOnNiagaraSettingsChanged SettingsChangedDelegate;
#endif


private:
	UPROPERTY(transient)
	mutable UNiagaraEffectType* DefaultEffectTypePtr;
};