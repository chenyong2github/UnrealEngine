// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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
};
