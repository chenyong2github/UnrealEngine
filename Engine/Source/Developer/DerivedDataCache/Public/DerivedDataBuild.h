// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"

class FCbObject;

namespace UE::DerivedData { class FBuildDefinition; }
namespace UE::DerivedData { class FBuildDefinitionBuilder; }

namespace UE::DerivedData
{

/**
 * Interface to the build system.
 *
 * This is only a preview of a portion of the interface and does not support build execution.
 */
class IBuild
{
public:
	virtual ~IBuild() = default;

	/**
	 * Create a build definition builder.
	 *
	 * @param Name       The name by which to identify this definition for logging and profiling.
	 * @param Function   The name of the build function with which to build this definition.
	 */
	virtual FBuildDefinitionBuilder CreateDefinition(FStringView Name, FStringView Function) = 0;

	/**
	 * Load a build definition from compact binary.
	 *
	 * @param Name         The name by which to identify this definition for logging and profiling.
	 * @param Definition   An object saved from a build definition. Holds a reference and is cloned if not owned.
	 * @return A valid build definition, or a build definition with an empty key on error.
	 */
	virtual FBuildDefinition LoadDefinition(FStringView Name, FCbObject&& Definition) = 0;
};

} // UE::DerivedData
