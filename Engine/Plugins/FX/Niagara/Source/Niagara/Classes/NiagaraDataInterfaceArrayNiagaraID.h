// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterfaceArray.h"
#include "NiagaraDataInterfaceArrayNiagaraID.generated.h"

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "NiagaraID Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayNiagaraID : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FNiagaraID> IntData;

	TArray<FNiagaraID>& GetArrayReference() { return IntData; }
};

