// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "PhysicsCore.h"

namespace PhysDLLHelper
{
	/**
	 *	Load the required modules for PhysX
	 */
	PHYSICSCORE_API bool LoadPhysXModules(bool bLoadCooking);


#if WITH_APEX
	PHYSICSCORE_API void* LoadAPEXModule(const FString& Path);
	PHYSICSCORE_API void UnloadAPEXModule(void* Handle);
#endif

	/**
	 *	Unload the required modules for PhysX
	 */
	PHYSICSCORE_API void UnloadPhysXModules();
}

bool PHYSICSCORE_API InitGamePhysCore();
void PHYSICSCORE_API TermGamePhysCore();