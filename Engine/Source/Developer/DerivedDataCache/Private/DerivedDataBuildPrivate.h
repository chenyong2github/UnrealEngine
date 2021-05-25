// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"
#include "Logging/LogMacros.h"

class FCbObject;
struct FGuid;

namespace UE::DerivedData { class FBuildAction; }
namespace UE::DerivedData { class FBuildActionBuilder; }
namespace UE::DerivedData { class FBuildDefinition; }
namespace UE::DerivedData { class FBuildDefinitionBuilder; }
namespace UE::DerivedData { class FBuildInputsBuilder; }
namespace UE::DerivedData { class FBuildOutputBuilder; }
namespace UE::DerivedData { class FBuildSession; }
namespace UE::DerivedData { class FCacheRecord; }
namespace UE::DerivedData { class FOptionalBuildAction; }
namespace UE::DerivedData { class FOptionalBuildDefinition; }
namespace UE::DerivedData { class FOptionalBuildInputs; }
namespace UE::DerivedData { class FOptionalBuildOutput; }
namespace UE::DerivedData { class IBuild; }
namespace UE::DerivedData { class IBuildFunctionRegistry; }
namespace UE::DerivedData { class IBuildInputResolver; }
namespace UE::DerivedData { class IBuildJob; }
namespace UE::DerivedData { class IBuildScheduler; }
namespace UE::DerivedData { class ICache; }
namespace UE::DerivedData { struct FBuildKey; }
namespace UE::DerivedData { template <typename RequestType> class TRequest; }

namespace UE::DerivedData::Private
{

DECLARE_LOG_CATEGORY_EXTERN(LogDerivedDataBuild, Log, All);

// Implemented in DerivedDataBuild.cpp
IBuild* CreateBuild(ICache& Cache);

// Implemented in DerivedDataBuildFunctionRegistry.cpp
IBuildFunctionRegistry* CreateBuildFunctionRegistry();

// Implemented in DerivedDataBuildScheduler.cpp
IBuildScheduler* CreateBuildScheduler();

// Implemented in DerivedDataBuildDefinition.cpp
FBuildDefinitionBuilder CreateBuildDefinition(FStringView Name, FStringView Function);
FOptionalBuildDefinition LoadBuildDefinition(FStringView Name, FCbObject&& Definition);

// Implemented in DerivedDataBuildAction.cpp
FBuildActionBuilder CreateBuildAction(FStringView Name, FStringView Function, const FGuid& FunctionVersion, const FGuid& BuildSystemVersion);
FOptionalBuildAction LoadBuildAction(FStringView Name, FCbObject&& Action);

// Implemented in DerivedDataBuildInput.cpp
FBuildInputsBuilder CreateBuildInputs(FStringView Name);

// Implemented in DerivedDataBuildOutput.cpp
FBuildOutputBuilder CreateBuildOutput(FStringView Name, FStringView Function);
FOptionalBuildOutput LoadBuildOutput(FStringView Name, FStringView Function, const FCbObject& Output);
FOptionalBuildOutput LoadBuildOutput(FStringView Name, FStringView Function, const FCacheRecord& Output);

// Implemented in DerivedDataBuildSession.cpp
FBuildSession CreateBuildSession(
	FStringView Name,
	ICache& Cache,
	IBuild& BuildSystem,
	IBuildScheduler& Scheduler,
	IBuildInputResolver* InputResolver);

// Implemented in DerivedDataBuildJob.cpp
TRequest<IBuildJob> CreateBuildJob(ICache& Cache, IBuild& BuildSystem, const FBuildKey& Key, IBuildInputResolver* InputResolver);
TRequest<IBuildJob> CreateBuildJob(ICache& Cache, IBuild& BuildSystem, const FBuildDefinition& Definition, IBuildInputResolver* InputResolver);
TRequest<IBuildJob> CreateBuildJob(ICache& Cache, IBuild& BuildSystem, const FBuildAction& Action, const FOptionalBuildInputs& Inputs);

} // UE::DerivedData::Private
