// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"

#include "NiagaraVariant.generated.h"

class UNiagaraDataInterface;

UENUM()
enum class ENiagaraVariantMode
{
	None,
	Object,
	DataInterface,
	Bytes
};

USTRUCT()
struct NIAGARA_API FNiagaraVariant
{
	GENERATED_BODY()

	FNiagaraVariant();
	FNiagaraVariant(const FNiagaraVariant& Other);
	explicit FNiagaraVariant(UNiagaraDataInterface* InDataInterface);
	explicit FNiagaraVariant(UObject* InObject);
	explicit FNiagaraVariant(const TArray<uint8>& InBytes);
	FNiagaraVariant(const void* InBytes, int32 Size);

	UObject* GetUObject() const;
	void SetUObject(UObject* InObject);

	UNiagaraDataInterface* GetDataInterface() const;
	void SetDataInterface(UNiagaraDataInterface* InDataInterface);

	void SetBytes(uint8* InBytes, int32 InCount);
	uint8* GetBytes() const;

	bool IsValid() const { return CurrentMode != ENiagaraVariantMode::None; }
	ENiagaraVariantMode GetMode() const { return CurrentMode; }

	bool operator==(const FNiagaraVariant& Other) const;
	bool operator!=(const FNiagaraVariant& Other) const;

private:
	UPROPERTY(EditAnywhere, Category=Variant)
	UObject* Object;

	UPROPERTY(EditAnywhere, Category=Variant)
	UNiagaraDataInterface* DataInterface;

	UPROPERTY(EditAnywhere, Category=Variant)
	TArray<uint8> Bytes;

	UPROPERTY(EditAnywhere, Category=Variant)
	ENiagaraVariantMode CurrentMode;
};