// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LocalizableMessageParameter.h"

#include "Containers/UnrealString.h"

#include "LocalizableMessageBaseParameters.generated.h"

USTRUCT()
struct FLocalizableMessageParameterInt : public FLocalizableMessageParameter
{
	GENERATED_BODY()

public:

	UPROPERTY()
	int64 Value = 0;

	virtual UScriptStruct* GetScriptStruct() const override { return FLocalizableMessageParameterInt::StaticStruct(); }
};

USTRUCT()
struct FLocalizableMessageParameterFloat : public FLocalizableMessageParameter
{
	GENERATED_BODY()

public:

	UPROPERTY()
	double Value = 0;

	virtual UScriptStruct* GetScriptStruct() const override { return FLocalizableMessageParameterFloat::StaticStruct(); }
};

USTRUCT()
struct FLocalizableMessageParameterString : public FLocalizableMessageParameter
{
	GENERATED_BODY()

public:

	UPROPERTY()
	FString Value;

	virtual UScriptStruct* GetScriptStruct() const override { return FLocalizableMessageParameterString::StaticStruct(); }
};
