// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "NNEModelDataFactory.generated.h"

UCLASS()
class NNEEDITORTOOLS_API UNNEModelDataFactory : public UFactory
{
	GENERATED_BODY()

public:

	UNNEModelDataFactory(const FObjectInitializer& ObjectInitializer);

public:

	virtual UObject* FactoryCreateBinary(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn) override;
	virtual bool FactoryCanImport(const FString& Filename) override;

};