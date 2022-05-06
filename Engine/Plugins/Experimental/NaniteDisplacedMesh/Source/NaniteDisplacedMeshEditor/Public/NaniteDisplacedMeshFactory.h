// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "NaniteDisplacedMesh.h"
#include "NaniteDisplacedMeshFactory.generated.h"

UCLASS(hidecategories = Object)
class NANITEDISPLACEDMESHEDITOR_API UNaniteDisplacedMeshFactory : public UFactory
{
	GENERATED_BODY()

public:
	UNaniteDisplacedMeshFactory();

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	static UNaniteDisplacedMesh* StaticFactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn);
};
