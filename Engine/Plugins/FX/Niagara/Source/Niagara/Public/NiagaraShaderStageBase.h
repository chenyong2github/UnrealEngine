// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraMergeable.h"
#include "NiagaraShaderStageBase.generated.h"

class UNiagaraScript;

/**
* A base class for niagara shader stages.  This class should be derived to add stage specific information.
*/
UCLASS()
class NIAGARA_API UNiagaraShaderStageBase : public UNiagaraMergeable
{
	GENERATED_BODY()

public:
	UPROPERTY()
	UNiagaraScript* Script;

	UPROPERTY(EditAnywhere, Category = "Shader Stage")
	FName ShaderStageName;
};

UCLASS(meta = (DisplayName = "Placeholder Stage A"))
class NIAGARA_API UNiagaraShaderStagePlaceholderA : public UNiagaraShaderStageBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Shader Stage")
	int32 Iterations;
};

UCLASS(meta = (DisplayName = "Placeholder Stage B"))
class NIAGARA_API UNiagaraShaderStagePlaceholderB: public UNiagaraShaderStageBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Shader Stage")
	FVector Direction;
};