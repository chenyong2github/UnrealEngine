// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Field/FieldSystemCoreAlgo.h"
#include "Field/FieldSystemNodes.h"

#include "FieldSystemAsset.generated.h"


UCLASS()
class FIELDSYSTEMENGINE_API UFieldSystem : public UObject
{
	GENERATED_BODY()

public:

	UFieldSystem() : UObject() {}
	virtual ~UFieldSystem() {}

	void Reset() { Commands.Empty(); }

	void Serialize(FArchive& Ar);

	TArray< FFieldSystemCommand > Commands;

};
