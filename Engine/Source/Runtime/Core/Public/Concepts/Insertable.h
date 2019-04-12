// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

/**
 * Describes a type with a GetTypeHash overload.
 */
struct CInsertable {
	template <typename DestType, typename T>
	auto Requires(DestType& Dest, T& Val) -> decltype(
		Dest << Val
	);
};
