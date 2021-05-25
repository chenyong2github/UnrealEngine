// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterfaceArray.h"
#include "NiagaraDataInterfaceArrayFloat.generated.h"

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Float Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayFloat : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<float> FloatData;

	TArray<float>& GetArrayReference() { return FloatData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Vector 2D Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayFloat2 : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FVector2D> FloatData;

	TArray<FVector2D>& GetArrayReference() { return FloatData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Vector Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayFloat3 : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FVector> FloatData;

	TArray<FVector>& GetArrayReference() { return FloatData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Vector 4 Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayFloat4 : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FVector4> FloatData;

	TArray<FVector4>& GetArrayReference() { return FloatData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Color Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayColor : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FLinearColor> ColorData;

	TArray<FLinearColor>& GetArrayReference() { return ColorData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Quaternion Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayQuat : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FQuat> QuatData;

	TArray<FQuat>& GetArrayReference() { return QuatData; }
};
