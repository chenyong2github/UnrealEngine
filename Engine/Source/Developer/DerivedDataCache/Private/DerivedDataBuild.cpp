// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuild.h"

#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildFunctionRegistry.h"
#include "DerivedDataBuildInputs.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataCache.h"
#include "Misc/Guid.h"

namespace UE::DerivedData::Private
{

DEFINE_LOG_CATEGORY(LogDerivedDataBuild);

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

	const FGuid& GetVersion() const final
	{
		return Version;
	}

	IBuildFunctionRegistry& GetFunctionRegistry() const final
	{
		return *FunctionRegistry;
	}

private:
	ICache& Cache;
	TUniquePtr<IBuildFunctionRegistry> FunctionRegistry{CreateBuildFunctionRegistry()};
	const FGuid Version{TEXT("ac0574e5-62bd-4c2e-84ec-f2efe48c0fef")};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IBuild* CreateBuild(ICache& Cache)
{
	return new FBuild(Cache);
}

} // UE::DerivedData::Private
