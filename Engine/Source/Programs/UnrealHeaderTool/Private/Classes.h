// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject\ObjectMacros.h"

class UPackage;
class FString;
class FClass;

class FClasses
{
public:
	static FClass* FindClass(const TCHAR* ClassName);

	/** 
	 * Attempts to find a script class based on the given name. Will attempt to strip
	 * the prefix of the given name while searching. Throws an exception with the script error
	 * if the class could not be found.
	 *
	 * @param   InClassName  Name w/ Unreal prefix to use when searching for a class
	 * @return               The found class.
	 */
	static FClass* FindScriptClassOrThrow(const FString& InClassName);

	/** 
	 * Attempts to find a script class based on the given name. Will attempt to strip
	 * the prefix of the given name while searching. Optionally returns script errors when appropriate.
	 *
	 * @param   InClassName  Name w/ Unreal prefix to use when searching for a class
	 * @param   OutErrorMsg  Error message (if any) giving the caller flexibility in how they present an error
	 * @return               The found class, or NULL if the class was not found.
	 */
	static FClass* FindScriptClass(const FString& InClassName, FString* OutErrorMsg = nullptr);

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

	/**
	 * Find an optional object, no failure allowed
	 * @see StaticFindObjectChecked()
	 */
	template< class T >
	static inline T* FindObjectChecked(UObject* Outer, const TCHAR* Name, bool ExactClass = false)
	{
		return (T*)StaticFindObjectChecked(T::StaticClass(), Outer, Name, ExactClass);
	}
};
