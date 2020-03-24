// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DerivedDataBackendInterface.h"


/** 
 * A file backed backend. This is used for local caching of data where more flexibility is needed than loose files.
**/
class FFileBackedDerivedDataBackend : public FDerivedDataBackendInterface
{
public:
	
	/**
	 * Save the cache to disk
	 * @param	Filename	Filename to save
	 * @return	true if file was saved successfully
	 */
	virtual bool SaveCache(const TCHAR* Filename) = 0;

	/**
	 * Load the cache from disk
	 * @param	Filename	Filename to load
	 * @return	true if file was loaded successfully
	 */
	virtual bool LoadCache(const TCHAR* Filename) = 0;

	/**
	 * Disables use of this cache and allows the underlying implementation to free up memory
	 */
	virtual void Disable() = 0;

private:

};

