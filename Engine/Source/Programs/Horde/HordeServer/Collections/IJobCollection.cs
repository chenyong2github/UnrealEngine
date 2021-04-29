using Datadog.Trace;
using EpicGames.Core;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;
using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;

namespace HordeServer.Collections
{
	/// <summary>
	/// Interface for a collection of job documents
	/// </summary>
	[SuppressMessage("Compiler", "CA1054:URI properties should not be strings")]
	public interface IJobCollection
	{
		/// <summary>
		/// Creates a new job
		/// </summary>
		/// <param name="JobId">A requested job id</param>
		/// <param name="StreamId">Unique id of the stream that this job belongs to</param>
		/// <param name="TemplateRefId">Name of the template ref</param>
		/// <param name="TemplateHash">Template for this job</param>
		/// <param name="Graph">The graph for the new job</param>
		/// <param name="Name">Name of the job</param>
		/// <param name="Change">The change to build</param>
		/// <param name="CodeChange">The corresponding code changelist number</param>
		/// <param name="PreflightChange">Optional changelist to preflight</param>
		/// <param name="ClonedPreflightChange">Optional cloned preflight changelist</param>
		/// <param name="StartedByUserId">User that started the job</param>
		/// <param name="StartedByUserName">User that started the job</param>
		/// <param name="Priority">Priority of the job</param>
		/// <param name="AutoSubmit">Whether to automatically submit the preflighted change on completion</param>
		/// <param name="JobTriggers">List of downstream job triggers</param>
		/// <param name="ShowUgsBadges">Whether to show badges in UGS for this job</param>
		/// <param name="ShowUgsAlerts">Whether to show alerts in UGS for this job</param>
		/// <param name="NotificationChannel">Notification channel for this job</param>
		/// <param name="NotificationChannelFilter">Notification channel filter for this job</param>
		/// <param name="HelixSwarmCallbackUrl">Callback URL to a Helix Server review, if any</param>
		/// <param name="Arguments">Arguments for the job</param>
		/// <returns>The new job document</returns>
		Task<IJob> AddAsync(ObjectId JobId, StreamId StreamId, TemplateRefId TemplateRefId, ContentHash TemplateHash, IGraph Graph, string Name, int Change, int CodeChange, int? PreflightChange, int? ClonedPreflightChange, ObjectId? StartedByUserId, string? StartedByUserName, Priority? Priority, bool? AutoSubmit, List<ChainedJobTemplate>? JobTriggers, bool ShowUgsBadges, bool ShowUgsAlerts, string? NotificationChannel, string? NotificationChannelFilter, string? HelixSwarmCallbackUrl, List<string>? Arguments);

		/// <summary>
		/// Gets a job with the given unique id
		/// </summary>
		/// <param name="JobId">Job id to search for</param>
		/// <returns>Information about the given job</returns>
		Task<IJob?> GetAsync(ObjectId JobId);

		/// <summary>
		/// Deletes a job
		/// </summary>
		/// <param name="Job">The job to remove</param>
		Task<bool> RemoveAsync(IJob Job);

		/// <summary>
		/// Delete all the jobs for a stream
		/// </summary>
		/// <param name="StreamId">Unique id of the stream</param>
		/// <returns>Async task</returns>
		Task RemoveStreamAsync(StreamId StreamId);

		/// <summary>
		/// Gets a job's permissions info by ID
		/// </summary>
		/// <param name="JobId">Unique id of the job</param>
		/// <returns>The job document</returns>
		Task<IJobPermissions?> GetPermissionsAsync(ObjectId JobId);

		/// <summary>
		/// Searches for jobs matching the given criteria
		/// </summary>
		/// <param name="JobIds">List of job ids to return</param>
		/// <param name="StreamId">The stream containing the job</param>
		/// <param name="Name">Name of the job</param>
		/// <param name="Templates">Templates to look for</param>
		/// <param name="MinChange">The minimum changelist number</param>
		/// <param name="MaxChange">The maximum changelist number</param>
		/// <param name="PreflightChange">Preflight change to find</param>
		/// <param name="PreflightStartedByUser">User for which to include preflight jobs</param>
		/// <param name="MinCreateTime">The minimum creation time</param>
		/// <param name="MaxCreateTime">The maximum creation time</param>
		/// <param name="ModifiedBefore">Filter the results by modified time</param>
		/// <param name="ModifiedAfter">Filter the results by modified time</param>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of jobs matching the given criteria</returns>
		Task<List<IJob>> FindAsync(ObjectId[]? JobIds = null, StreamId? StreamId = null, string? Name = null, TemplateRefId[]? Templates = null, int? MinChange = null, int? MaxChange = null, int? PreflightChange = null, string? PreflightStartedByUser = null, DateTimeOffset? MinCreateTime = null, DateTimeOffset? MaxCreateTime = null, DateTimeOffset? ModifiedBefore = null, DateTimeOffset? ModifiedAfter = null, int? Index = null, int? Count = null);

		/// <summary>
		/// Updates a new job
		/// </summary>
		/// <param name="Job">The job document to update</param>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="Name">Name of the job</param>
		/// <param name="Priority">Priority of the job</param>
		/// <param name="AutoSubmit">Automatically submit the job on completion</param>
		/// <param name="AutoSubmitChange">Changelist that was automatically submitted</param>
		/// <param name="AutoSubmitMessage"></param>
		/// <param name="AbortedByUser">Name of the user that aborted the job</param>
		/// <param name="NotificationTriggerId">Id for a notification trigger</param>
		/// <param name="Reports">New reports</param>
		/// <param name="Arguments">New arguments for the job</param>
		/// <param name="LabelIdxToTriggerId">New trigger ID for a label in the job</param>
		/// <param name="JobTrigger">New downstream job id</param>
		Task<bool> TryUpdateJobAsync(IJob Job, IGraph Graph, string? Name = null, Priority? Priority = null, bool? AutoSubmit = null, int? AutoSubmitChange = null, string? AutoSubmitMessage = null, string? AbortedByUser = null, ObjectId? NotificationTriggerId = null, List<Report>? Reports = null, List<string>? Arguments = null, KeyValuePair<int, ObjectId>? LabelIdxToTriggerId = null, KeyValuePair<TemplateRefId, ObjectId>? JobTrigger = null);

		/// <summary>
		/// Updates the state of a batch
		/// </summary>
		/// <param name="Job">Job to update</param>
		/// <param name="Graph">The graph for this job</param>
		/// <param name="BatchId">Unique id of the batch to update</param>
		/// <param name="NewLogId">The new log file id</param>
		/// <param name="NewState">New state of the jobstep</param>
		/// <param name="NewError">Error code for the batch</param>
		/// <returns>True if the job was updated, false if it was deleted</returns>
		Task<bool> TryUpdateBatchAsync(IJob Job, IGraph Graph, SubResourceId BatchId, ObjectId? NewLogId, JobStepBatchState? NewState, JobStepBatchError? NewError);

		/// <summary>
		/// Update a jobstep state
		/// </summary>
		/// <param name="Job">Job to update</param>
		/// <param name="Graph">The graph for this job</param>
		/// <param name="BatchId">Unique id of the batch containing the step</param>
		/// <param name="StepId">Unique id of the step to update</param>
		/// <param name="NewState">New state of the jobstep</param>
		/// <param name="NewOutcome">New outcome of the jobstep</param>
		/// <param name="NewAbortRequested">New state of request abort</param>
		/// <param name="NewAbortByUser">New name of user that requested the abort</param>
		/// <param name="NewLogId">New log id for the jobstep</param>
		/// <param name="NewNotificationTriggerId">New id for a notification trigger</param>
		/// <param name="NewRetryByUser">Whether the step should be retried</param>
		/// <param name="NewPriority">New priority for this step</param>
		/// <param name="NewReports">New report documents</param>
		/// <param name="NewProperties">Property changes. Any properties with a null value will be removed.</param>
		/// <returns>True if the job was updated, false if it was deleted in the meantime</returns>
		Task<bool> TryUpdateStepAsync(IJob Job, IGraph Graph, SubResourceId BatchId, SubResourceId StepId, JobStepState NewState = default, JobStepOutcome NewOutcome = default, bool? NewAbortRequested = null, string? NewAbortByUser = null, ObjectId? NewLogId = null, ObjectId? NewNotificationTriggerId = null, string? NewRetryByUser = null, Priority? NewPriority = null, List<Report>? NewReports = null, Dictionary<string, string?>? NewProperties = null);

		/// <summary>
		/// Attempts to update the node groups to be executed for a job. Fails if another write happens in the meantime.
		/// </summary>
		/// <param name="Job">The job to update</param>
		/// <param name="NewGraph">New graph for this job</param>
		/// <returns>True if the groups were updated to the given list. False if another write happened first.</returns>
		Task<bool> TryUpdateGraphAsync(IJob Job, IGraph NewGraph);

		/// <summary>
		/// Adds an issue to a job
		/// </summary>
		/// <param name="JobId">The job id</param>
		/// <param name="IssueId">The issue to add</param>
		/// <returns>Async task</returns>
		Task AddIssueToJobAsync(ObjectId JobId, int IssueId);

		/// <summary>
		/// Gets a queue of jobs to consider for execution
		/// </summary>
		/// <returns>Sorted list of jobs to execute</returns>
		Task<List<IJob>> GetDispatchQueueAsync();

		/// <summary>
		/// Marks a job as skipped
		/// </summary>
		/// <param name="Job">The job to update</param>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="Reason">Reason for this batch being failed</param>
		/// <returns>Updated version of the job</returns>
		Task<IJob?> SkipAllBatchesAsync(IJob? Job, IGraph Graph, JobStepBatchError Reason);

		/// <summary>
		/// Marks a batch as skipped
		/// </summary>
		/// <param name="Job">The job to update</param>
		/// <param name="BatchIdx">The batch to mark as skipped</param>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="Reason">Reason for this batch being failed</param>
		/// <returns>Updated version of the job</returns>
		Task<IJob?> SkipBatchAsync(IJob? Job, int BatchIdx, IGraph Graph, JobStepBatchError Reason);

		/// <summary>
		/// Abort an agent's lease, and update the payload accordingly
		/// </summary>
		/// <param name="Job">The job to update</param>
		/// <param name="BatchIdx">Index of the batch to cancel</param>
		/// <param name="Graph">Graph for the job</param>
		/// <returns>True if the job is updated</returns>
		Task<bool> TryFailBatchAsync(IJob Job, int BatchIdx, IGraph Graph);

		/// <summary>
		/// Attempt to assign a lease to execute a batch
		/// </summary>
		/// <param name="Job">The job containing the batch</param>
		/// <param name="BatchIdx">Index of the batch</param>
		/// <param name="PoolId">The pool id</param>
		/// <param name="AgentId">New agent to execute the batch</param>
		/// <param name="SessionId">Session of the agent that is to execute the batch</param>
		/// <param name="LeaseId">The lease unique id</param>
		/// <param name="LogId">Unique id of the log for the batch</param>
		/// <returns>True if the batch is updated</returns>
		Task<bool> TryAssignLeaseAsync(IJob Job, int BatchIdx, PoolId PoolId, AgentId AgentId, ObjectId SessionId, ObjectId LeaseId, ObjectId LogId);

		/// <summary>
		/// Cancel a lease reservation on a batch (before it has started)
		/// </summary>
		/// <param name="Job">The job containing the lease</param>
		/// <param name="BatchIdx">Index of the batch to cancel</param>
		/// <returns>True if the job is updated</returns>
		Task<bool> TryCancelLeaseAsync(IJob Job, int BatchIdx);

		/// <summary>
		/// Upgrade all documents in the collection
		/// </summary>
		/// <returns>Async task</returns>
		Task UpgradeDocumentsAsync();
	}
}
