// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

template <typename FuncType> class TUniqueFunction;

namespace UE::DerivedData { class FBuildAction; }
namespace UE::DerivedData { class FOptionalBuildInputs; }
namespace UE::DerivedData { class IRequest; }
namespace UE::DerivedData { struct FBuildWorkerActionCompleteParams; }
namespace UE::DerivedData { enum class EBuildPolicy : uint8; }
namespace UE::DerivedData { enum class EPriority : uint8; }
namespace UE::DerivedData { template <typename RequestType> class TRequest; }
namespace UE::DerivedData { using FRequest = TRequest<IRequest>; }

namespace UE::DerivedData
{

using FOnBuildWorkerActionComplete = TUniqueFunction<void (FBuildWorkerActionCompleteParams&& Params)>;

class IBuildWorkerRegistry
{
public:
	virtual ~IBuildWorkerRegistry() = default;

	virtual FRequest BuildAction(
		const FBuildAction& Action,
		const FOptionalBuildInputs& Inputs,
		EBuildPolicy Policy,
		EPriority Priority,
		FOnBuildWorkerActionComplete&& Params) = 0;
};

} // UE::DerivedData
