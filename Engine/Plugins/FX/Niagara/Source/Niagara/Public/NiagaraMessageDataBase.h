// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "NiagaraMessageDataBase.generated.h"

UCLASS()
class NIAGARA_API UNiagaraMessageDataBase : public UObject
{
	GENERATED_BODY()

public:
	UNiagaraMessageDataBase() = default;

	virtual bool GetAllowDismissal() const { return false; }

#if WITH_EDITORONLY_DATA
	virtual bool Equals(const UNiagaraMessageDataBase* Other) const;
#endif
};
