// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ParserClass.h"
#include "UObject/ClassTree.h"

#define WIP_UHT_REFACTOR 1

class UPackage;
class FString;
class FClass;

class FClasses
{
public:
	explicit FClasses(const TArray<UClass*>* Classes);

	/**
	 * Returns the root class (i.e. UObject)
	 *
	 * @return The root class.
	 */
	FClass* GetRootClass() const;

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
	 * Returns an array of classes for the given package.
	 *
	 * @param   InPackage  The package to return the classes from.
	 * @return             The classes in the specified package.
	 */
	TArray<FClass*> GetClassesInPackage(const UPackage* InPackage = ANY_PACKAGE) const;

// Anything in here should eventually be removed when this class encapsulates its own data structure, rather than being 'poked' by the outside
#if WIP_UHT_REFACTOR

	/**
	 * Validates the state of the tree (shouldn't be needed once this class has well-defined invariants).
	 */
	void Validate();

	FORCEINLINE FClassTree& GetClassTree()
	{
		return ClassTree;
	}

#endif

private:
	FClass*     UObjectClass;
	FClassTree  ClassTree;
};
