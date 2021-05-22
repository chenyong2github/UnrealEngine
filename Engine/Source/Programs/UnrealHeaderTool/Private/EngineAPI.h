// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"

/**
 * Wrapper class for access to the engine API.
 */
class FEngineAPI
{
public:
	/**
	 * Find an optional object.
	 * @see StaticFindObject()
	 */
	template< class T >
	static inline T* FindObject(UObject* Outer, const TCHAR* Name, bool ExactClass = false)
	{
		return (T*)StaticFindObject(T::StaticClass(), Outer, Name, ExactClass);
	}

	/**
	 * Find an optional object, relies on the name being unqualified
	 * @see StaticFindObjectFast()
	 */
	template< class T >
	static inline T* FindObjectFast(UObject* Outer, FName Name, bool ExactClass = false, bool AnyPackage = false, EObjectFlags ExclusiveFlags = RF_NoFlags)
	{
		return (T*)StaticFindObjectFast(T::StaticClass(), Outer, Name, ExactClass, AnyPackage, ExclusiveFlags);
	}
};
