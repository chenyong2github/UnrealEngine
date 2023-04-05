// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementLabelColumns.generated.h"


/**
 * Column that stores a label.
 */
USTRUCT()
struct FTypedElementLabelColumn final : public FTypedElementDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY()
	FString Label;
};

/**
 * Column that stores the hash of a label. This is typically paired with FTypedElementLabelColumn, but 
 * kept separate in order to iterate quickly over all hash values.
 */
USTRUCT()
struct FTypedElementLabelHashColumn final : public FTypedElementDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY()
	uint64 LabelHash;
};