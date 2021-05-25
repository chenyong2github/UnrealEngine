// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterfaceArray.h"
#include "NiagaraDataInterfaceArrayInt.generated.h"

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Int32 Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayInt32 : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<int32> IntData;

	TArray<int32>& GetArrayReference() { return IntData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Bool Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayBool : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<bool> BoolData;

	TArray<bool>& GetArrayReference() { return BoolData; }
};
