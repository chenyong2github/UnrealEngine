// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataBuildJob.h"

namespace UE::DerivedData
{

/**
 * A build scheduler is responsible for deciding when and where a job executes in certain states.
 *
 * Jobs dispatch themselves to their scheduler when they are prepared to access limited resources
 * such as: memory, compute, storage, network. A scheduler may allow a job to execute immediately
 * or may queue it to execute later. A scheduler that uses a job queue is expected to execute the
 * jobs in priority order, respecting updates to priority.
 */
class IBuildScheduler
{
public:
	virtual ~IBuildScheduler() = default;

	/** Begin processing of the job by this scheduler. Always paired with EndJob. */
	virtual void BeginJob(IBuildJob* Job) {}

	/** End processing of the job by this scheduler. Always paired with BeginJob. */
	virtual void EndJob(IBuildJob* Job) {}

	/** Dispatch the job immediately if it is queued. May be called multiple times and/or concurrently. */
	virtual void CancelJob(IBuildJob* Job) {}

	/** Update the priority of the job if it is queued. May be called multiple times and/or concurrently. */
	virtual void UpdateJobPriority(IBuildJob* Job) {}

	/** Dispatch the job by executing the requested state now or queuing it for later. */
	virtual void DispatchCacheQuery(IBuildJob* Job) { Job->BeginCacheQuery(); }
	virtual void DispatchCacheStore(IBuildJob* Job) { Job->BeginCacheStore(); }
	virtual void DispatchResolveKey(IBuildJob* Job) { Job->BeginResolveKey(); }
	virtual void DispatchResolveAction(IBuildJob* Job) { Job->BeginResolveAction(); }
	virtual void DispatchResolveInputs(IBuildJob* Job) { Job->BeginResolveInputs(); }
	virtual void DispatchExecuteRemote(IBuildJob* Job) { Job->BeginExecuteRemote(); }
	virtual void DispatchExecuteLocal(IBuildJob* Job) { Job->BeginExecuteLocal(); }

	/** Called when the output of the job is available. Always called once, and always between BeginJob and EndJob. */
	virtual void CompleteJob(const FBuildJobCompleteParams& Params) {}
};

} // UE::DerivedData
