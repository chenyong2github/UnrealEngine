// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"

class FCbObject;
struct FGuid;

namespace UE::DerivedData { class FBuildActionBuilder; }
namespace UE::DerivedData { class FBuildDefinitionBuilder; }
namespace UE::DerivedData { class FBuildInputsBuilder; }
namespace UE::DerivedData { class FBuildOutputBuilder; }
namespace UE::DerivedData { class FBuildSession; }
namespace UE::DerivedData { class FCacheRecord; }
namespace UE::DerivedData { class FOptionalBuildAction; }
namespace UE::DerivedData { class FOptionalBuildDefinition; }
namespace UE::DerivedData { class FOptionalBuildOutput; }
namespace UE::DerivedData { class IBuildFunctionRegistry; }
namespace UE::DerivedData { class IBuildInputResolver; }
namespace UE::DerivedData { class IBuildScheduler; }
namespace UE::DerivedData { class IBuildWorkerRegistry; }

namespace UE::DerivedData
{

/**
 * Interface to the build system.
 *
 * Executing a build typically requires a definition, input resolver, session, and function.
 *
 * Use IBuild::CreateDefinition() to make a new build definition, or use IBuild::LoadDefinition()
 * to load a build definition that was previously saved. This references the function to execute,
 * and the inputs needed by the function.
 *
 * Use IBuild::CreateSession() to make a new build session with a build input resolver to resolve
 * input references into the referenced data. Use FBuildSession::Build() to schedule a definition
 * to build, along with any of its transitive build dependencies.
 *
 * Implement IBuildFunction, with a unique name and version, to add payloads to the build context
 * based on constants and inputs in the context. Use TBuildFunctionFactory to add the function to
 * the registry at IBuild::GetFunctionRegistry() to allow the build job to find it.
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
	 * @return A valid build definition, or null on error.
	 */
	virtual FOptionalBuildDefinition LoadDefinition(FStringView Name, FCbObject&& Definition) = 0;

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
	 * @param Name     The name by which to identify this action for logging and profiling.
	 * @param Action   The saved action to load.
	 * @return A valid build action, or null on error.
	 */
	virtual FOptionalBuildAction LoadAction(FStringView Name, FCbObject&& Action) = 0;

	/**
	 * Create a build inputs builder.
	 *
	 * @param Name   The name by which to identify the inputs for logging and profiling.
	 */
	virtual FBuildInputsBuilder CreateInputs(FStringView Name) = 0;

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
	 * @return A valid build output, or null on error.
	 */
	virtual FOptionalBuildOutput LoadOutput(FStringView Name, FStringView Function, const FCbObject& Output) = 0;
	virtual FOptionalBuildOutput LoadOutput(FStringView Name, FStringView Function, const FCacheRecord& Output) = 0;

	/**
	 * Create a build session.
	 *
	 * @param Name            The name by which to identify this session for logging and profiling.
	 * @param InputResolver   The input resolver to resolve definitions and inputs for requested builds.
	 * @param Scheduler       The scheduler for builds created through the session. Optional.
	 */
	virtual FBuildSession CreateSession(FStringView Name, IBuildInputResolver* InputResolver, IBuildScheduler* Scheduler = nullptr) = 0;

	/**
	 * Returns the version of the build system.
	 *
	 * This version is expected to change very infrequently, only when formats and protocols used by
	 * the build system are changed in a way that breaks compatibility. This version is incorporated
	 * into build actions to keep the build output separate for different build versions.
	 */
	virtual const FGuid& GetVersion() const = 0;

	/**
	 * Returns the build function registry used by the build system.
	 */
	virtual IBuildFunctionRegistry& GetFunctionRegistry() const = 0;

	/**
	 * Returns the build worker registry used by the build system.
	 */
	virtual IBuildWorkerRegistry& GetWorkerRegistry() const = 0;
};

} // UE::DerivedData
