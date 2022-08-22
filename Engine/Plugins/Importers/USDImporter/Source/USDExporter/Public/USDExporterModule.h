// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class IUsdExporterModule : public IModuleInterface
{
public:
	/**
	 * Generates a unique identifier string that involves the package's persistent guid, the corresponding file save
	 * date and time, and the number of times the package has been dirtied since last being saved.
	 * This can be used to track the version of exported assets and levels, to prevent reexporting of actors and
	 * components.
	 */
	static FString GeneratePackageVersionGuidString( const UPackage* Package );
};
