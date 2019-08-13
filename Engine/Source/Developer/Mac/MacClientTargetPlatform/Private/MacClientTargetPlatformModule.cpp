// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MacClientTargetPlatformModule.cpp: Implements the FMacClientTargetPlatformModule class.
=============================================================================*/

#include "CoreMinimal.h"
#include "GenericMacTargetPlatform.h"
#include "Interfaces/ITargetPlatformModule.h"
#include "Modules/ModuleManager.h"

/**
 * Holds the target platform singleton.
 */
static ITargetPlatform* Singleton = NULL;


/**
 * Module for the Mac target platform (without editor).
 */
class FMacClientTargetPlatformModule
	: public ITargetPlatformModule
{
public:

	virtual ~FMacClientTargetPlatformModule( )
	{
		Singleton = NULL;
	}

	virtual ITargetPlatform* GetTargetPlatform( ) override
	{
		if (Singleton == NULL && TGenericMacTargetPlatform<false, false, true>::IsUsable())
		{
			Singleton = new TGenericMacTargetPlatform<false, false, true>();
		}

		return Singleton;
	}
};


IMPLEMENT_MODULE(FMacClientTargetPlatformModule, MacClientTargetPlatform);