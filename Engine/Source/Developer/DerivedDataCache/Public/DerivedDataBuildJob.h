// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "DerivedDataRequest.h"

class FEvent;

template <typename FuncType> class TUniqueFunction;

namespace UE::DerivedData { class FBuildOutput; }
namespace UE::DerivedData { class IBuild; }
namespace UE::DerivedData { class IBuildScheduler; }
namespace UE::DerivedData { class ICache; }
namespace UE::DerivedData { struct FBuildJobCompleteParams; }
namespace UE::DerivedData { struct FBuildActionKey; }
namespace UE::DerivedData { struct FBuildKey; }
namespace UE::DerivedData { enum class EBuildPolicy : uint8; }

namespace UE::DerivedData
{

using FOnBuildJobComplete = TUniqueFunction<void (FBuildJobCompleteParams&& Params)>;

/**
 * A build job is responsible for the execution of one build.
 *
 * Jobs typically proceed through each one of a sequence of states, though a state may be skipped
 * if the action was found in the cache or if the scheduler finds duplicate jobs for a definition
 * or an action.
 *
 * The job depends on the build scheduler to move it through its states. That relationship allows
 * the scheduler more control over resources such as: memory, compute, storage, network.
 */
class IBuildJob : public IRequest
{
public:
	/** Returns the name by which to identify this job for logging and profiling. */
	virtual FStringView GetName() const = 0;
	/** Returns the name of the function to build with, or "Unknown" if not resolved yet. */
	virtual FStringView GetFunction() const = 0;
	/** Returns the build policy of this job. */
	virtual EBuildPolicy GetPolicy() const = 0;
	/** Returns the priority of this job. */
	virtual EPriority GetPriority() const = 0;

	/** Returns the key, or null if the job was created directly from an action. */
	virtual const FBuildKey& GetKey() const = 0;
	/** Returns the action key, or null if the action has not been resolved yet. */
	virtual const FBuildActionKey& GetActionKey() const = 0;

	/** Returns the cache associated with this job. */
	virtual ICache& GetCache() const = 0;
	/** Returns the build system associated with this job. */
	virtual IBuild& GetBuild() const = 0;

	/** Start the build by dispatching it to the scheduler. */
	virtual void Schedule(IBuildScheduler& Scheduler, EBuildPolicy Policy, EPriority Priority, FOnBuildJobComplete&& OnComplete) = 0;

	/** Called by the scheduler to begin execution for the corresponding state. */
	virtual void BeginCacheQuery() = 0;
	virtual void BeginCacheStore() = 0;
	virtual void BeginResolveKey() = 0;
	virtual void BeginResolveInputMeta() = 0;
	virtual void BeginResolveInputData() = 0;
	virtual void BeginExecuteRemote() = 0;
	virtual void BeginExecuteLocal() = 0;

	/** Called by the scheduler if it has cached output compatible with the build policy. */
	virtual void SetOutput(const FBuildOutput& Output) = 0;
};

/** Parameters for the completion callback for build jobs. */
struct FBuildJobCompleteParams
{
	/** Job that is complete. */
	const IBuildJob& Job;
	/** Output for the job that completed or was canceled. */
	FBuildOutput&& Output;
	/** Status of the job. */
	EStatus Status = EStatus::Error;
};

} // UE::DerivedData
