// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraScriptVariable.generated.h"

/*
* Used to store variable data and metadata per graph. 
*/
UCLASS()
class UNiagaraScriptVariable : public UObject
{
public:
	GENERATED_UCLASS_BODY()

	virtual void PostLoad() override;
	
	/** Variable type, name and data. The data is not persistent, but used as a buffer when interfacing elsewhere. */
	UPROPERTY()
	FNiagaraVariable Variable;
	/** The metadata associated with this script variable. */
	UPROPERTY(EditAnywhere, Category = "Variable", meta=(ShowOnlyInnerProperties))
	FNiagaraVariableMetaData Metadata;
}; 