// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

/**
 * Describes a type comparable with another type.
 */
struct CEqualityComparableWith {
	template <typename T>
	auto Requires(bool& Result, const T& A, const T& B) -> decltype(
		Result = A == B,
		Result = B == A,
		Result = A != B,
		Result = B != A
	);
};

/**
 * Describes a type comparable with itself.
 */
struct CEqualityComparable {
	template <typename T>
	auto Requires() -> decltype(
		Refines<CEqualityComparableWith, T, T>()
	);
};
