// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PropertyBag.h"

#include "MovieGraphCommon.generated.h"

// Note: This is a copy of the property bag's types so the implementation details of the graph
// members don't leak to the external API. Not ideal, but UHT doesn't let us alias types.
/** The type of a graph member's value. */
UENUM()
enum class EMovieGraphValueType : uint8
{
	None UMETA(Hidden),
	Bool,
	Byte,
	Int32,
	Int64,
	Float,
	Double,
	Name,
	String,
	Text,
	Enum UMETA(Hidden),
	Struct UMETA(Hidden),
	Object UMETA(Hidden),
	SoftObject UMETA(Hidden),
	Class UMETA(Hidden),
	SoftClass UMETA(Hidden),

	Branch UMETA(Hidden),	// Added for MRQ purposes
	Unknown UMETA(Hidden),	// Added for MRQ purposes

	Count UMETA(Hidden)
};

// Note: The +2 is to account for the MRQ-added items in the enum
// TODO: We may want a method which converts between these enum types instead
static_assert((uint8)EMovieGraphValueType::Count == (uint8)EPropertyBagPropertyType::Count + 2);

// Note: Also a copy of the property bag's container types for the same reason as EMovieGraphValueType.
/** The container type of a graph member's value. */
UENUM()
enum class EMovieGraphContainerType : uint8
{
	None UMETA(Hidden),
	Array,

	Count UMETA(Hidden)
};

static_assert((uint8)EMovieGraphContainerType::Count == (uint8)EPropertyBagContainerType::Count);