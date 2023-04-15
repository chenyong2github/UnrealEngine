// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementMiscColumns.generated.h"


/**
 * Tag to indicate that there are one or more bits of information in the row that
 * need to be copied out the Data Storage and into the original object. This tag
 * will automatically be removed at the end of a tick.
 */
USTRUCT(meta = (DisplayName = "Sync back to world"))
struct FTypedElementSyncBackToWorldTag final : public FTypedElementDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Row reference"))
struct FTypedElementRowReferenceColumn final : public FTypedElementDataStorageColumn
{
	GENERATED_BODY()

	TypedElementRowHandle Row;
};