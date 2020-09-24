// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DatasmithUtils.h"


class DATASMITHFACADE_API FDatasmithFacadeUniqueNameProvider
{
public:
	/**
	 * Generates a unique name
	 * @param BaseName Name that will be suffixed with an index to be unique
	 * @return TCHAR* Unique name. Calling "Contains()" with this name will be false. 
	 *                Pointer is only valid until the next GenerateUniqueName() call.
	 */
	const TCHAR* GenerateUniqueName(const TCHAR* BaseName)
	{
		CachedGeneratedName = InternalNameProvider.GenerateUniqueName(BaseName);
		return *CachedGeneratedName;
	}

	/**
	 * Reserves space in the internal data structures to contain at least the number of name specified.
	 * @param NumberOfName The number of name to reserve for.
	 */
	void Reserve( int32 NumberOfName ) { InternalNameProvider.Reserve(NumberOfName); }

	/**
	 * Register a name as known
	 * @param Name name to register
	 */
	void AddExistingName(const TCHAR* Name) { InternalNameProvider.AddExistingName(Name); }
	
	/**
	 * Remove a name from the list of existing name
	 * @param Name name to unregister
	 */
	void RemoveExistingName(const TCHAR* Name) { InternalNameProvider.RemoveExistingName(Name); }

private:
	FDatasmithUniqueNameProvider InternalNameProvider;
	FString CachedGeneratedName;
};