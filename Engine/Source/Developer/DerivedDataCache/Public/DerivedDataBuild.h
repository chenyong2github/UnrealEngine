// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"

class FCbObject;
struct FGuid;

namespace UE::DerivedData { class FBuildAction; }
namespace UE::DerivedData { class FBuildActionBuilder; }
namespace UE::DerivedData { class FBuildDefinition; }
namespace UE::DerivedData { class FBuildDefinitionBuilder; }
namespace UE::DerivedData { class FBuildOutput; }
namespace UE::DerivedData { class FBuildOutputBuilder; }
namespace UE::DerivedData { class FCacheRecord; }

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

	/**
	 * Create a build action builder.
	 *
	 * @param Name       The name by which to identify this action for logging and profiling.
	 * @param Function   The name of the build function that produced this action.
	 */
	virtual FBuildActionBuilder CreateAction(FStringView Name, FStringView Function) = 0;

	/**
	 * Load a build action from compact binary.
	 *
	 * @param Name       The name by which to identify this action for logging and profiling.
	 * @param Action     The saved action to load.
	 * @return A valid build action, or a build action with an empty key on error.
	 */
	virtual FBuildAction LoadAction(FStringView Name, FCbObject&& Action) = 0;

	/**
	 * Create a build output builder.
	 *
	 * @param Name       The name by which to identify this output for logging and profiling.
	 * @param Function   The name of the build function that produced this output.
	 */
	virtual FBuildOutputBuilder CreateOutput(FStringView Name, FStringView Function) = 0;

	/**
	 * Load a build output.
	 *
	 * @param Name       The name by which to identify this output for logging and profiling.
	 * @param Function   The name of the build function that produced this output.
	 * @param Output     The saved output to load.
	 */
	virtual FBuildOutput LoadOutput(FStringView Name, FStringView Function, const FCbObject& Output) = 0;
	virtual FBuildOutput LoadOutput(FStringView Name, FStringView Function, const FCacheRecord& Output) = 0;

	/**
	 * Returns the version of the build system.
	 *
	 * This version is expected to change very infrequently, only when formats and protocols used by
	 * the build system are changed in a way that breaks compatibility. This version is incorporated
	 * into build actions to keep the build output separate for different build versions.
	 */
	virtual const FGuid& GetVersion() const = 0;
};

} // UE::DerivedData
