// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Factories/Factory.h"
#include "UObject/ObjectMacros.h"

#include "SoundSurroundFactory.generated.h"


UCLASS(MinimalAPI, hidecategories=Object)
class USoundSurroundFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	float CueVolume;

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateBinary( UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn ) override;
	virtual bool FactoryCanImport( const FString& Filename ) override;
	//~ End UFactory Interface
};
