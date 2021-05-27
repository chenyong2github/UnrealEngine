// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"

struct FGuid;

namespace UE::DerivedData { class FBuildWorker; }
namespace UE::DerivedData { class IBuildWorkerExecutor; }

namespace UE::DerivedData
{

class IBuildWorkerRegistry
{
public:
	virtual ~IBuildWorkerRegistry() = default;

	virtual IBuildWorkerExecutor* GetWorkerExecutor() const = 0;

	virtual FBuildWorker* FindWorker(FStringView Function, const FGuid& FunctionVersion) const = 0;
};

} // UE::DerivedData
