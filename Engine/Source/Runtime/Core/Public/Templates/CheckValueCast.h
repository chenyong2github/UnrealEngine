// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UnrealTemplate.h"

/**
 * Template to cast to another type and ensure the value is not changed
 *
 */

template <typename T_to,typename T_fm>
T_to TCheckValueCast(T_fm FromValue)
{
	// cast to T_to type :
	T_to ReturnValue = static_cast<T_to>(FromValue);
	// cast back to T_fm to check equality was preserved :
	check( static_cast<T_fm>(ReturnValue) == FromValue );
	return ReturnValue;
}

