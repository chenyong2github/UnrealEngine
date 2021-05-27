// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuild.h"

#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildFunctionRegistry.h"
#include "DerivedDataBuildInputs.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataBuildScheduler.h"
#include "DerivedDataBuildSession.h"
#include "DerivedDataBuildWorkerRegistry.h"
#include "DerivedDataCache.h"
#include "Misc/Guid.h"

namespace UE::DerivedData::Private
{

DEFINE_LOG_CATEGORY(LogDerivedDataBuild);

/**
 * Derived Data Build System
 *
 * Public Data Types:
 *
 * FBuildDefinition:
 * - Function, Constants, Key->InputId
 * - From FBuildDefinitionBuilder via IBuild::CreateDefinition()
 * - Serializes to/from FCbObject
 * FBuildAction:
 * - Function+Version, BuildSystemVersion, Constants, Key->InputHash+InputSize
 * - From FBuildActionBuilder via IBuild::CreateAction()
 * - Serializes to/from FCbObject
 * FBuildInputs:
 * - Key->InputBuffer
 * - From FBuildInputsBuilder via IBuild::CreateInputs()
 * FBuildOutput:
 * - Metadata, Payloads[], Diagnostics[] (Level, Category, Message)
 * - From FBuildOutputBuilder via IBuild::CreateOutput()
 * - Serializes to/from FCbObject and FCacheRecord
 * FBuildKey:
 * - Unique ID for FBuildDefinition
 * FBuildActionKey:
 * - Unique ID for FBuildAction
 * - Combined with FCacheBucket to create a FCacheKey
 *
 * Public Interface Types:
 *
 * IBuildFunction:
 * - Name (Unique), Version
 * - Represents the steps necessary to convert inputs to the output
 * IBuildFunctionRegistry:
 * - Registry of IBuildFunction used by IBuild
 * IBuildInputResolver:
 * - Resolves FBuildKey to FBuildDefinition
 * - Resolves FBuildDefinition to Key->(RawHash, RawSize, OptionalBuffer)
 * IBuildJob:
 * - From FBuildKey or FBuildDefinition or FBuildAction+FBuildInputs via FBuildSession
 * - Represents an executing build request and implements IRequest
 * IBuildScheduler:
 * - Schedules execution of the operations on IBuildJob
 * FBuildSession:
 * - From ICache+IBuild+IBuildScheduler+IBuildInputResolver via IBuild::CreateSession()
 * - Interface for scheduling builds from a key, definition, or action+inputs
 */
class FBuild final : public IBuild
{
public:
	explicit FBuild(ICache& InCache)
		: Cache(InCache)
	{
	}

	FBuildDefinitionBuilder CreateDefinition(FStringView Name, FStringView Function) final
	{
		return CreateBuildDefinition(Name, Function);
	}

	FOptionalBuildDefinition LoadDefinition(FStringView Name, FCbObject&& Definition) final
	{
		return LoadBuildDefinition(Name, MoveTemp(Definition));
	}

	FBuildActionBuilder CreateAction(FStringView Name, FStringView Function) final
	{
		return CreateBuildAction(Name, Function, FunctionRegistry->FindFunctionVersion(Function), Version);
	}

	FOptionalBuildAction LoadAction(FStringView Name, FCbObject&& Action) final
	{
		return LoadBuildAction(Name, MoveTemp(Action));
	}

	FBuildInputsBuilder CreateInputs(FStringView Name) final
	{
		return CreateBuildInputs(Name);
	}

	FBuildOutputBuilder CreateOutput(FStringView Name, FStringView Function) final
	{
		return CreateBuildOutput(Name, Function);
	}

	FOptionalBuildOutput LoadOutput(FStringView Name, FStringView Function, const FCbObject& Output) final
	{
		return LoadBuildOutput(Name, Function, Output);
	}

	FOptionalBuildOutput LoadOutput(FStringView Name, FStringView Function, const FCacheRecord& Output) final
	{
		return LoadBuildOutput(Name, Function, Output);
	}

	FBuildSession CreateSession(FStringView Name, IBuildInputResolver* InputResolver, IBuildScheduler* Scheduler) final
	{
		return CreateBuildSession(Name, Cache, *this, Scheduler ? *Scheduler : *DefaultScheduler, InputResolver);
	}

	const FGuid& GetVersion() const final
	{
		return Version;
	}

	IBuildFunctionRegistry& GetFunctionRegistry() const final
	{
		return *FunctionRegistry;
	}

	IBuildWorkerRegistry& GetWorkerRegistry() const final
	{
		return *WorkerRegistry;
	}

private:
	ICache& Cache;
	TUniquePtr<IBuildFunctionRegistry> FunctionRegistry{CreateBuildFunctionRegistry()};
	TUniquePtr<IBuildWorkerRegistry> WorkerRegistry{CreateBuildWorkerRegistry()};
	TUniquePtr<IBuildScheduler> DefaultScheduler{CreateBuildScheduler()};
	const FGuid Version{TEXT("ac0574e5-62bd-4c2e-84ec-f2efe48c0fef")};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IBuild* CreateBuild(ICache& Cache)
{
	return new FBuild(Cache);
}

} // UE::DerivedData::Private
