// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraTypes.h"
#include "NiagaraScriptVariable.generated.h"

UCLASS()
class UNiagaraScriptVariable : public UObject
{
public:
	GENERATED_UCLASS_BODY()

	virtual void PostLoad() override;

	UPROPERTY()
	FNiagaraVariable Variable;

	UPROPERTY(EditAnywhere, Category = Metadata, meta=(NoResetToDefault, ShowOnlyInnerProperties))
	FNiagaraVariableMetaData Metadata;
};