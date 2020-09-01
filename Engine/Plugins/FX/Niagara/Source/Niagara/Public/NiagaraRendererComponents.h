// Copyright Epic Games, Inc. All Rights Reserved.

/*========================================================================================
NiagaraRendererComponents.h: Renderer for rendering Niagara particles as scene components.
=========================================================================================*/
#pragma once

#include "NiagaraRenderer.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/UnrealType.h"

struct FNiagaraPropertySetter
{
	UFunction* Function;
	bool bIgnoreConversion = false;
};

/**
* NiagaraRendererComponents renders an FNiagaraEmitterInstance as scene components
*/
class NIAGARA_API FNiagaraRendererComponents : public FNiagaraRenderer
{
public:

	FNiagaraRendererComponents(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter);

	//FNiagaraRenderer interface
	virtual void Initialize(const UNiagaraRendererProperties* InProps, const FNiagaraEmitterInstance* Emitter, const UNiagaraComponent* InComponent) override;
	virtual FNiagaraDynamicDataBase *GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const override;
	//FNiagaraRenderer interface END

	static TArray<FString> SetterPrefixes;

private:
	TMap<FName, FNiagaraPropertySetter> SetterFunctionMapping;

	// These property accessor methods are largely copied over from MovieSceneCommonHelpers.h
	static FComponentPropertyAddress FindPropertyRecursive(void* BasePointer, UStruct* InStruct, TArray<FString>& InPropertyNames, uint32 Index);
	static FComponentPropertyAddress FindProperty(const UObject& Object, const FString& InPropertyPath);
};


