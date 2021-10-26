// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UnrealTemplate.h"

/**
 * Template to cast to another type and ensure the value is not changed
 *  only for integer types
 * 
 * The cast back to T_fm detects narrowing casts that lose values.
 * The check for changing signed-ness catches same-size and expanding signed casts.
 * 
 */

template <typename T_to,typename T_fm>
inline
T_to CheckValueCast(T_fm FromValue)
{

	// cast to T_to type :
	T_to ReturnValue = static_cast<T_to>(FromValue);

	// cast back to T_fm to check equality was preserved :
	check( static_cast<T_fm>(ReturnValue) == FromValue );
	
	// ensure negative-ness doesn't change:
	//	this catches same-size and expanding signed-unsigned casts
	check( (FromValue < 0) == (ReturnValue < 0) );

	return ReturnValue;
}
