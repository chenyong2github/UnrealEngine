// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildScheduler.h"

#include "DerivedDataBuildFunction.h"
#include "DerivedDataBuildFunctionFactory.h"
#include "DerivedDataBuildFunctionRegistry.h"
#include "Misc/Guid.h"

namespace UE::DerivedData::Private
{

class FBuildScheduler final : public IBuildScheduler
{
};

IBuildScheduler* CreateBuildScheduler()
{
	return new FBuildScheduler();
}

} // UE::DerivedData::Private
