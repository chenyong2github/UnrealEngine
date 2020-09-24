// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

// Custom serialization version for changes made in Dev-Anim stream
struct CONTROLRIG_API FControlRigObjectVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded,

		// Added execution pins and removed hierarchy ref pins
		RemovalOfHierarchyRefPins,

		// Refactored operators to store FCachedPropertyPath instead of string
		OperatorsStoringPropertyPaths,

		// Introduced new RigVM as a backend
		SwitchedToRigVM,

		// Added a new transform as part of the control
		ControlOffsetTransform,

		// Using a cache data structure for key indices now
		RigElementKeyCache,

		// Full variable support
		BlueprintVariableSupport,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FControlRigObjectVersion() {}
};
