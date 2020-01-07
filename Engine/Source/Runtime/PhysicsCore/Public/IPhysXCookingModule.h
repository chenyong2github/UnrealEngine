// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IPhysXCookingModule.h: Declares the IPhysXCookingModule interface.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class IPhysXCooking;

/**
 * Interface for PhysX format modules.
 */
class PHYSICSCORE_API IPhysXCookingModule
	: public IModuleInterface
{
public:

	/**
	 * Gets the PhysX format.
	 */
	virtual IPhysXCooking* GetPhysXCooking( ) = 0;

	/** Terminates any physx state related to cooking */
	virtual void Terminate() = 0;

public:

	/**
	 * Virtual destructor.
	 */
	~IPhysXCookingModule( ) { }
};
