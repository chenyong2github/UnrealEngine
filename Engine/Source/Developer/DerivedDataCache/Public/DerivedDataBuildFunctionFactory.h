// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataBuildFunction.h"
#include "Features/IModularFeatures.h"
#include "UObject/NameTypes.h"

namespace UE::DerivedData
{

/** Base for the build function factory. DO NOT USE DIRECTLY. Use TBuildFunctionFactory. */
class IBuildFunctionFactory : public IModularFeature
{
public:
	/** Returns the build function associated with this factory. */
	virtual const IBuildFunction& GetFunction() const = 0;

	/** Returns the name of the build function factory modular feature. */
	static FName GetFeatureName()
	{
		return FName("BuildFunctionFactory");
	}
};

/**
 * Factory that creates and registers a build function.
 *
 * A build function must be registered by a build function factory before it can execute a build.
 * Register a function in the source file that implements it or in the corresponding module.
 *
 * Example: static const TBuildFunctionFactory<FExampleFunction> ExampleFunctionFactory;
 */
template <typename FunctionType>
class TBuildFunctionFactory final : private IBuildFunctionFactory
{
	static_assert(sizeof(FunctionType) == sizeof(IBuildFunction), "IBuildFunction must be pure and maintain no state.");

public:
	TBuildFunctionFactory()
	{
		IModularFeatures::Get().RegisterModularFeature(GetFeatureName(), this);
	}

	~TBuildFunctionFactory()
	{
		IModularFeatures::Get().UnregisterModularFeature(GetFeatureName(), this);
	}

private:
	const IBuildFunction& GetFunction() const final
	{
		return Function;
	}

	const FunctionType Function;
};

} // UE::DerivedData
