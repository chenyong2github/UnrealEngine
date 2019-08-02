// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiplexStorage.h"
#include "MultiplexRegistry.h"
#include "MultiplexByteCode.h"
#include "MultiplexVM.generated.h"

UCLASS()
class ANIMATIONCORE_API UMultiplexVM : public UObject
{
	GENERATED_BODY()

public:

	void Reset();

	bool Execute(FMultiplexStorage** Storage = nullptr, TArrayView<void*> AdditionalArgs = TArrayView<void*>());

	UFUNCTION()
	int32 AddMultiplexFunction(UScriptStruct* InMultiplexStruct, const FName& InFunctionName);

	UPROPERTY()
	FMultiplexStorage Literals;

	UPROPERTY()
	FMultiplexStorage WorkState;

	UPROPERTY()
	FMultiplexByteCode ByteCode;

	UPROPERTY()
	FMultiplexByteCodeTable Instructions;

private:

	void ResolveFunctionsIfRequired();
	void RefreshInstructionsIfRequired();

	UPROPERTY()
	TArray<FString> FunctionNames;

	TArray<FMultiplexFunctionPtr> Functions;
};
