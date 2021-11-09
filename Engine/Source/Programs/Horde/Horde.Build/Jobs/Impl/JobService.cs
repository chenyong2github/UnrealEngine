// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Collections;
using HordeCommon;
using HordeServer.Models;
using HordeCommon.Rpc;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Serializers;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Security.Claims;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Globalization;
using Horde.Build.Utilities;
using HordeServer.Tasks.Impl;
using OpenTracing.Util;
using OpenTracing;

namespace HordeServer.Services
{
	using JobId = ObjectId<IJob>;
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Exception thrown when attempting to retry executing a node that does not allow retries
	/// </summary>
	public class RetryNotAllowedException : Exception
	{
		/// <summary>
		/// The node name
		/// </summary>
		public string NodeName { get; private set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="NodeName">Name of the node that does not permit retries</param>
		public RetryNotAllowedException(string NodeName)
			: base($"Node '{NodeName}' does not permit retries")
		{
			this.NodeName = NodeName;
		}
	}

	/// <summary>
	/// Cache of information about job ACLs
	/// </summary>
	public class JobPermissionsCache : StreamPermissionsCache
	{
		/// <summary>
		/// Map of job id to permissions for that job
		/// </summary>
		public Dictionary<JobId, IJobPermissions?> Jobs { get; set; } = new Dictionary<JobId, IJobPermissions?>();
	}

	/// <summary>
	/// Wraps funtionality for manipulating jobs, jobsteps, and jobstep runs
	/// </summary>
	[SuppressMessage("Compiler", "CA1054:URI parameters should not be strings")]
	public class JobService
	{
		/// <summary>
		/// Collection of job documents
		/// </summary>
		IJobCollection Jobs;

		/// <summary>
		/// Collection of graph documents
		/// </summary>
		IGraphCollection Graphs;

		/// <summary>
		/// Collection of agent documents
		/// </summary>
		IAgentCollection Agents;

		/// <summary>
		/// Collection of jobstepref documents
		/// </summary>
		IJobStepRefCollection JobStepRefs;

		/// <summary>
		/// Collection of job timing documents
		/// </summary>
		IJobTimingCollection JobTimings;

		/// <summary>
		/// Collection of users
		/// </summary>
		IUserCollection UserCollection;

		/// <summary>
		/// Collection of notification triggers
		/// </summary>
		INotificationTriggerCollection TriggerCollection;

		/// <summary>
		/// The singleton instance of the queue service
		/// </summary>
		JobTaskSource JobTaskSource;

		/// <summary>
		/// Singleton instance of the stream service
		/// </summary>
		StreamService StreamService;

		/// <summary>
		/// Singleton instance of the template service
		/// </summary>
		ITemplateCollection TemplateCollection;

		/// <summary>
		/// Singleton instance of the issue service
		/// </summary>
		IIssueService? IssueService;

		/// <summary>
		/// The perforce service
		/// </summary>
		IPerforceService PerforceService;

		/// <summary>
		/// The server settings
		/// </summary>
		IOptionsMonitor<ServerSettings> Settings;

		/// <summary>
		/// Log output device
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Delegate for label update events
		/// </summary>
		public delegate void LabelUpdateEvent(IJob Job, IReadOnlyList<(LabelState, LabelOutcome)> OldLabelStates, IReadOnlyList<(LabelState, LabelOutcome)> NewLabelStates);

		/// <summary>
		/// Event triggered when a label state changes
		/// </summary>
		public event LabelUpdateEvent? OnLabelUpdate;

		/// <summary>
		/// Delegate for job step complete events
		/// </summary>
		public delegate void JobStepCompleteEvent(IJob Job, IGraph Graph, SubResourceId BatchId, SubResourceId StepId);

		/// <summary>
		/// Event triggered when a job step completes
		/// </summary>
		public event JobStepCompleteEvent? OnJobStepComplete;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Jobs">The jobs collection</param>
		/// <param name="Graphs">The graphs collection</param>
		/// <param name="Agents">The agents collection</param>
		/// <param name="JobStepRefs">The jobsteprefs collection</param>
		/// <param name="JobTimings">The job timing document collection</param>
		/// <param name="UserCollection">User profiles</param>
		/// <param name="TriggerCollection">Trigger collection</param>
		/// <param name="JobTaskSource">The queue service</param>
		/// <param name="StreamService">The stream service</param>
		/// <param name="TemplateCollection">The template service</param>
		/// <param name="IssueService">The issue service</param>
		/// <param name="PerforceService">The perforce service</param>
		/// <param name="Settings">Settings instance</param>
		/// <param name="Logger">Log output</param>
		public JobService(IJobCollection Jobs, IGraphCollection Graphs, IAgentCollection Agents, IJobStepRefCollection JobStepRefs, IJobTimingCollection JobTimings, IUserCollection UserCollection, INotificationTriggerCollection TriggerCollection, JobTaskSource JobTaskSource, StreamService StreamService, ITemplateCollection TemplateCollection, IIssueService IssueService, IPerforceService PerforceService, IOptionsMonitor<ServerSettings> Settings, ILogger<JobService> Logger)
		{
			this.Jobs = Jobs;
			this.Graphs = Graphs;
			this.Agents = Agents;
			this.JobStepRefs = JobStepRefs;
			this.JobTimings = JobTimings;
			this.UserCollection = UserCollection;
			this.TriggerCollection = TriggerCollection;
			this.JobTaskSource = JobTaskSource;
			this.StreamService = StreamService;
			this.TemplateCollection = TemplateCollection;
			this.IssueService = IssueService;
			this.PerforceService = PerforceService;
			this.Settings = Settings;
			this.Logger = Logger;
		}

#pragma warning disable CA1801
		static bool ShouldClonePreflightChange(StreamId StreamId)
		{
			return false; //			return StreamId == new StreamId("ue5-main");
		}
#pragma warning restore CA1801

		/// <summary>
		/// Creates a new job
		/// </summary>
		/// <param name="JobId">A requested job id</param>
		/// <param name="Stream">The stream that this job belongs to</param>
		/// <param name="TemplateRefId">Name of the template ref</param>
		/// <param name="TemplateHash">Template for this job</param>
		/// <param name="Graph">The graph for the new job</param>
		/// <param name="Name">Name of the job</param>
		/// <param name="Change">The change to build</param>
		/// <param name="CodeChange">The corresponding code changelist</param>
		/// <param name="PreflightChange">Optional changelist to preflight</param>
		/// <param name="ClonedPreflightChange">Duplicated preflight change</param>
		/// <param name="StartedByUserId">Id of the user that started the job</param>
		/// <param name="Priority">Priority of the job</param>
		/// <param name="AutoSubmit">Whether to automatically submit the preflighted change on completion</param>
		/// <param name="UpdateIssues">Whether to update issues when this job completes</param>
		/// <param name="JobTriggers">List of downstream job triggers</param>
		/// <param name="ShowUgsBadges">Whether to show badges in UGS for this job</param>
		/// <param name="ShowUgsAlerts">Whether to show alerts in UGS for this job</param>
		/// <param name="NotificationChannel">Notification Channel for this job</param>
		/// <param name="NotificationChannelFilter">Notification Channel filter for this job</param>
		/// <param name="Arguments">Arguments for the job</param>
		/// <returns>Unique id representing the job</returns>
		public async Task<IJob> CreateJobAsync(JobId? JobId, IStream Stream, TemplateRefId TemplateRefId, ContentHash TemplateHash, IGraph Graph, string Name, int Change, int CodeChange, int? PreflightChange, int? ClonedPreflightChange, UserId? StartedByUserId, Priority? Priority, bool? AutoSubmit, bool? UpdateIssues, List<ChainedJobTemplate>? JobTriggers, bool ShowUgsBadges, bool ShowUgsAlerts, string? NotificationChannel, string? NotificationChannelFilter, IReadOnlyList<string> Arguments)
		{
			using IScope TraceScope = GlobalTracer.Instance.BuildSpan("JobService.CreateJobAsync").StartActive();
			TraceScope.Span.SetTag("JobId", JobId);
			TraceScope.Span.SetTag("Stream", Stream.Name);
			TraceScope.Span.SetTag("TemplateRefId", TemplateRefId);
			TraceScope.Span.SetTag("TemplateHash", TemplateHash);
			TraceScope.Span.SetTag("GraphId", Graph.Id);
			TraceScope.Span.SetTag("Name", Name);
			TraceScope.Span.SetTag("Change", Change);
			TraceScope.Span.SetTag("CodeChange", CodeChange);
			TraceScope.Span.SetTag("PreflightChange", PreflightChange);
			TraceScope.Span.SetTag("ClonedPreflightChange", ClonedPreflightChange);
			TraceScope.Span.SetTag("StartedByUserId", StartedByUserId.ToString());
			TraceScope.Span.SetTag("Priority", Priority.ToString());
			TraceScope.Span.SetTag("ShowUgsBadges", ShowUgsBadges);
			TraceScope.Span.SetTag("NotificationChannel", NotificationChannel ?? "null");
			TraceScope.Span.SetTag("NotificationChannelFilter", NotificationChannelFilter ?? "null");
			TraceScope.Span.SetTag("Arguments", string.Join(',', Arguments));
			
			if (AutoSubmit != null) TraceScope.Span.SetTag("AutoSubmit", AutoSubmit.Value);
			if (UpdateIssues != null) TraceScope.Span.SetTag("UpdateIssues", UpdateIssues.Value);
			if (JobTriggers != null) TraceScope.Span.SetTag("JobTriggers.Count", JobTriggers.Count);

			JobId JobIdValue = JobId ?? HordeServer.Utilities.ObjectId<IJob>.GenerateNewId();
			using IDisposable Scope = Logger.BeginScope("CreateJobAsync({JobId})", JobIdValue);

			if (PreflightChange != null && ShouldClonePreflightChange(Stream.Id))
			{
				ClonedPreflightChange = await CloneShelvedChangeAsync(Stream.ClusterName, ClonedPreflightChange ?? PreflightChange.Value);
			}

			Logger.LogInformation("Creating job at CL {Change}, code CL {CodeChange}, preflight CL {PreflightChange}, cloned CL {ClonedPreflightChange}", Change, CodeChange, PreflightChange, ClonedPreflightChange);

			Dictionary<string, string> Properties = new Dictionary<string, string>();
			Properties["Change"] = Change.ToString(CultureInfo.InvariantCulture);
			Properties["CodeChange"] = CodeChange.ToString(CultureInfo.InvariantCulture);
			Properties["PreflightChange"] = PreflightChange?.ToString(CultureInfo.InvariantCulture) ?? String.Empty;
			Properties["ClonedPreflightChange"] = ClonedPreflightChange?.ToString(CultureInfo.InvariantCulture) ?? String.Empty;
			Properties["StreamId"] = Stream.Id.ToString();
			Properties["TemplateId"] = TemplateRefId.ToString();
			Properties["JobId"] = JobIdValue.ToString();

			List<string> ExpandedArguments = new List<string>();
			if (Arguments != null)
			{
				foreach (string Argument in Arguments)
				{
					string ExpandedArgument = StringUtils.ExpandProperties(Argument, Properties);
					ExpandedArguments.Add(ExpandedArgument);
				}
			}

			Name = StringUtils.ExpandProperties(Name, Properties);

			IJob NewJob = await Jobs.AddAsync(JobIdValue, Stream.Id, TemplateRefId, TemplateHash, Graph, Name, Change, CodeChange, PreflightChange, ClonedPreflightChange, StartedByUserId, Priority, AutoSubmit, UpdateIssues, JobTriggers, ShowUgsBadges, ShowUgsAlerts, NotificationChannel, NotificationChannelFilter, ExpandedArguments);
			JobTaskSource.UpdateQueuedJob(NewJob, Graph);

			await JobTaskSource.UpdateUgsBadges(NewJob, Graph, new List<(LabelState, LabelOutcome)>());

			if (StartedByUserId != null)
			{
				await UserCollection.UpdateSettingsAsync(StartedByUserId.Value, AddPinnedJobIds: new[] { NewJob.Id });
			}

			await AbortAnyDuplicateJobs(NewJob);

			return NewJob;
		}

		private async Task AbortAnyDuplicateJobs(IJob NewJob)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("JobService.AbortAnyDuplicateJobs").StartActive();
			Scope.Span.SetTag("JobId", NewJob.Id.ToString());
			Scope.Span.SetTag("JobName", NewJob.Name);
			
			List<IJob> JobsToAbort = new List<IJob>();
			if (NewJob.PreflightChange > 0)
			{
				JobsToAbort = await Jobs.FindAsync(PreflightChange: NewJob.PreflightChange);
			}

			foreach (IJob Job in JobsToAbort)
			{
				if (Job.GetState() == JobState.Complete) continue;
				if (Job.Id == NewJob.Id) continue; // Don't remove the new job
				if (Job.TemplateId != NewJob.TemplateId) continue;
				if (Job.TemplateHash != NewJob.TemplateHash) continue;
				if (Job.StartedByUserId != NewJob.StartedByUserId) continue;
				if (string.Join(",", Job.Arguments) != string.Join(",", NewJob.Arguments)) continue;

				IJob? UpdatedJob = await UpdateJobAsync(Job, null, null, null, KnownUsers.System, null, null, null);
				if (UpdatedJob == null)
				{
					Logger.LogError("Failed marking duplicate job as aborted! Job ID: {JobId}", Job.Id);
				}

				IJob? UpdatedJob2 = await GetJobAsync(Job.Id);
				if (UpdatedJob2?.AbortedByUserId != UpdatedJob?.AbortedByUserId)
				{
					throw new NotImplementedException();
				}
			}
		}

		/// <summary>
		/// Deletes a job
		/// </summary>
		/// <param name="Job">The job to delete</param>
		public async Task<bool> DeleteJobAsync(IJob Job)
		{
			using IScope TraceScope = GlobalTracer.Instance.BuildSpan("JobService.DeleteJobAsync").StartActive();
			TraceScope.Span.SetTag("JobId", Job.Id.ToString());
			TraceScope.Span.SetTag("JobName", Job.Name);

			using IDisposable Scope = Logger.BeginScope("DeleteJobAsync({JobId})", Job.Id);

			// Delete the job
			while (!await Jobs.RemoveAsync(Job))
			{
				IJob? NewJob = await Jobs.GetAsync(Job.Id);
				if (NewJob == null)
				{
					return false;
				}
				Job = NewJob;
			}

			// Remove all the triggers from it
			List<ObjectId> TriggerIds = new List<ObjectId>();
			if (Job.NotificationTriggerId != null)
			{
				TriggerIds.Add(Job.NotificationTriggerId.Value);
			}
			foreach (IJobStep Step in Job.Batches.SelectMany(x => x.Steps))
			{
				if (Step.NotificationTriggerId != null)
				{
					TriggerIds.Add(Step.NotificationTriggerId.Value);
				}
			}
			await TriggerCollection.DeleteAsync(TriggerIds);

			return true;
		}

		/// <summary>
		/// Delete all the jobs for a stream
		/// </summary>
		/// <param name="StreamId">Unique id of the stream</param>
		/// <returns>Async task</returns>
		public async Task DeleteJobsForStreamAsync(StreamId StreamId)
		{
			await Jobs.RemoveStreamAsync(StreamId);
		}

		/// <summary>
		/// Updates a new job
		/// </summary>
		/// <param name="Job">The job document to update</param>
		/// <param name="Name">Name of the job</param>
		/// <param name="Priority">Priority of the job</param>
		/// <param name="AutoSubmit">Whether to automatically submit the preflighted change on completion</param>
		/// <param name="AbortedByUserId">Name of the user that aborted this job</param>
		/// <param name="OnCompleteTriggerId">Object id for a notification trigger</param>
		/// <param name="Reports">New reports to add</param>
		/// <param name="Arguments">New arguments for the job</param>
		/// <param name="LabelIdxToTriggerId">New trigger ID for a label in the job</param>
		public async Task<IJob?> UpdateJobAsync(IJob Job, string? Name = null, Priority? Priority = null, bool? AutoSubmit = null, UserId? AbortedByUserId = null, ObjectId? OnCompleteTriggerId = null, List<Report>? Reports = null, List<string>? Arguments = null, KeyValuePair<int, ObjectId>? LabelIdxToTriggerId = null)
		{
			using IScope TraceScope = GlobalTracer.Instance.BuildSpan("JobService.UpdateJobAsync").StartActive();
			TraceScope.Span.SetTag("JobId", Job.Id.ToString());
			TraceScope.Span.SetTag("Name", Name);
			
			using IDisposable Scope = Logger.BeginScope("UpdateJobAsync({JobId})", Job.Id);
			for(IJob? NewJob = Job; NewJob != null; NewJob = await GetJobAsync(Job.Id))
			{
				IGraph Graph = await GetGraphAsync(NewJob);

				// Capture the previous label states
				IReadOnlyList<(LabelState, LabelOutcome)> OldLabelStates = NewJob.GetLabelStates(Graph);

				// Update the new list of job steps
				NewJob = await Jobs.TryUpdateJobAsync(NewJob, Graph, Name, Priority, AutoSubmit, null, null, AbortedByUserId, OnCompleteTriggerId, Reports, Arguments, LabelIdxToTriggerId);
				if (NewJob != null)
				{
					// Update any badges that have been modified
					await JobTaskSource.UpdateUgsBadges(NewJob, Graph, OldLabelStates);

					// Cancel any leases which are no longer required
					foreach (IJobStepBatch Batch in NewJob.Batches)
					{
						if (Batch.Error == JobStepBatchError.Cancelled && (Batch.State == JobStepBatchState.Starting || Batch.State == JobStepBatchState.Running) && Batch.AgentId != null && Batch.LeaseId != null)
						{
							await CancelLeaseAsync(Batch.AgentId.Value, Batch.LeaseId.Value);
						}
					}
					return NewJob;
				}
			}
			return null;
		}

		/// <summary>
		/// Cancel an active lease
		/// </summary>
		/// <param name="AgentId">The agent to retreive</param>
		/// <param name="LeaseId">The lease id to update</param>
		/// <returns></returns>
		async Task CancelLeaseAsync(AgentId AgentId, LeaseId LeaseId)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("JobService.CancelLeaseAsync").StartActive();
			Scope.Span.SetTag("AgentId", AgentId.ToString());
			Scope.Span.SetTag("LeaseId", LeaseId.ToString());
			
			for (; ; )
			{
				IAgent? Agent = await Agents.GetAsync(AgentId);
				if (Agent == null)
				{
					break;
				}

				int Index = 0;
				while (Index < Agent.Leases.Count && Agent.Leases[Index].Id != LeaseId)
				{
					Index++;
				}
				if (Index == Agent.Leases.Count)
				{
					break;
				}

				AgentLease Lease = Agent.Leases[Index];
				if (Lease.State != LeaseState.Active)
				{
					break;
				}

				IAgent? NewAgent = await Agents.TryCancelLeaseAsync(Agent, Index);
				if (NewAgent != null)
				{
					JobTaskSource.CancelLongPollForAgent(Agent.Id);
					break;
				}
			}
		}

		/// <summary>
		/// Gets a job with the given unique id
		/// </summary>
		/// <param name="JobId">Job id to search for</param>
		/// <returns>Information about the given job</returns>
		public Task<IJob?> GetJobAsync(JobId JobId)
		{
			return Jobs.GetAsync(JobId);
		}

		/// <summary>
		/// Gets the graph for a job
		/// </summary>
		/// <param name="Job">Job to retrieve the graph for</param>
		/// <returns>The graph for this job</returns>
		public Task<IGraph> GetGraphAsync(IJob Job)
		{
			return Graphs.GetAsync(Job.GraphHash);
		}

		/// <summary>
		/// Gets a job's permissions info by ID
		/// </summary>
		/// <param name="JobId">Unique id of the job</param>
		/// <returns>The job document</returns>
		public Task<IJobPermissions?> GetJobPermissionsAsync(JobId JobId)
		{
			return Jobs.GetPermissionsAsync(JobId);
		}

		/// <summary>
		/// Searches for jobs matching the given criteria
		/// </summary>
		/// <param name="JobIds">List of job ids to return</param>
		/// <param name="StreamId">The stream containing the job</param>
		/// <param name="Name">Name of the job</param>
		/// <param name="Templates">Templates to look for</param>
		/// <param name="MinChange">The minimum changelist number</param>
		/// <param name="MaxChange">The maximum changelist number</param>		
		/// <param name="PreflightChange">The preflight change to look for</param>
		/// <param name="PreflightStartedByUser">User for which to include preflight jobs</param>		
		/// <param name="StartedByUser">User for which to include jobs</param>
		/// <param name="MinCreateTime">The minimum creation time</param>
		/// <param name="MaxCreateTime">The maximum creation time</param>
		/// <param name="Target">The target to query</param>
		/// <param name="State">State to query</param>
		/// <param name="Outcome">Outcomes to return</param>
		/// <param name="ModifiedBefore">Filter the results by last modified time</param>
		/// <param name="ModifiedAfter">Filter the results by last modified time</param>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of jobs matching the given criteria</returns>
		public async Task<List<IJob>> FindJobsAsync(JobId[]? JobIds = null, StreamId? StreamId = null, string? Name = null, TemplateRefId[]? Templates = null, int? MinChange = null, int? MaxChange = null, int? PreflightChange = null, UserId? PreflightStartedByUser = null, UserId? StartedByUser = null, DateTimeOffset ? MinCreateTime = null, DateTimeOffset? MaxCreateTime = null, string? Target = null, JobStepState[]? State = null, JobStepOutcome[]? Outcome = null, DateTimeOffset? ModifiedBefore = null, DateTimeOffset? ModifiedAfter = null, int? Index = null, int? Count = null)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("JobService.FindJobsAsync").StartActive();
			Scope.Span.SetTag("JobIds", JobIds);
			Scope.Span.SetTag("StreamId", StreamId);
			Scope.Span.SetTag("Name", Name);
			Scope.Span.SetTag("Templates", Templates);
			Scope.Span.SetTag("MinChange", MinChange);
			Scope.Span.SetTag("MaxChange", MaxChange);
			Scope.Span.SetTag("PreflightChange", PreflightChange);
			Scope.Span.SetTag("PreflightStartedByUser", PreflightStartedByUser);
			Scope.Span.SetTag("StartedByUser", StartedByUser);
			Scope.Span.SetTag("MinCreateTime", MinCreateTime);
			Scope.Span.SetTag("MaxCreateTime", MaxCreateTime);
			Scope.Span.SetTag("Target", Target);
			Scope.Span.SetTag("State", State?.ToString());
			Scope.Span.SetTag("Outcome", Outcome?.ToString());
			Scope.Span.SetTag("ModifiedBefore", ModifiedBefore);
			Scope.Span.SetTag("ModifiedAfter", ModifiedAfter);
			Scope.Span.SetTag("Index", Index);
			Scope.Span.SetTag("Count", Count);
			
			if (Target == null && (State == null || State.Length == 0) && (Outcome == null || Outcome.Length == 0))
			{
				return await Jobs.FindAsync(JobIds, StreamId, Name, Templates, MinChange, MaxChange, PreflightChange, PreflightStartedByUser, StartedByUser, MinCreateTime, MaxCreateTime, ModifiedBefore, ModifiedAfter, Index, Count);
			}
			else
			{
				List<IJob> Results = new List<IJob>();
				Logger.LogInformation("Performing scan for job with ");

				int MaxCount = (Count ?? 1);
				while (Results.Count < MaxCount)
				{
					List<IJob> ScanJobs = await Jobs.FindAsync(JobIds, StreamId, Name, Templates, MinChange, MaxChange, PreflightChange, PreflightStartedByUser, StartedByUser, MinCreateTime, MaxCreateTime, ModifiedBefore, ModifiedAfter, 0, 5);
					if (ScanJobs.Count == 0)
					{
						break;
					}

					foreach (IJob Job in ScanJobs.OrderByDescending(x => x.Change))
					{
						(JobStepState, JobStepOutcome)? Result;
						if (Target == null)
						{
							Result = Job.GetTargetState();
						}
						else
						{
							Result = Job.GetTargetState(await GetGraphAsync(Job), Target);
						}

						if (Result != null)
						{
							(JobStepState JobState, JobStepOutcome JobOutcome) = Result.Value;
							if ((State == null || State.Length == 0 || State.Contains(JobState)) && (Outcome == null || Outcome.Length == 0 || Outcome.Contains(JobOutcome)))
							{
								Results.Add(Job);
								if (Results.Count == MaxCount)
								{
									break;
								}
							}
						}
					}

					MaxChange = ScanJobs.Min(x => x.Change) - 1;
				}

				return Results;
			}
		}

		/// <summary>
		/// Attempts to update the node groups to be executed for a job. Fails if another write happens in the meantime.
		/// </summary>
		/// <param name="Job">The job to update</param>
		/// <param name="NewGraph">New graph for this job</param>
		/// <returns>True if the groups were updated to the given list. False if another write happened first.</returns>
		public async Task<IJob?> TryUpdateGraphAsync(IJob Job, IGraph NewGraph)
		{
			using IScope TraceScope = GlobalTracer.Instance.BuildSpan("JobService.TryUpdateGraphAsync").StartActive();
			TraceScope.Span.SetTag("Job", Job.Id);
			TraceScope.Span.SetTag("NewGraph", NewGraph.Id);

			using IDisposable Scope = Logger.BeginScope("TryUpdateGraphAsync({JobId})", Job.Id);

			IReadOnlyList<(LabelState, LabelOutcome)> OldLabelStates = Job.GetLabelStates(NewGraph);

			IJob? NewJob = await Jobs.TryUpdateGraphAsync(Job, NewGraph);
			if(NewJob != null)
			{
				await JobTaskSource.UpdateUgsBadges(NewJob, NewGraph, OldLabelStates);
				JobTaskSource.UpdateQueuedJob(NewJob, NewGraph);
			}
			return NewJob;
		}

		/// <summary>
		/// Gets the timing info for a particular job.
		/// </summary>
		/// <param name="Job">The job to get timing info for</param>
		/// <returns>Timing info for the given job</returns>
		public async Task<IJobTiming> GetJobTimingAsync(IJob Job)
		{
			using IScope TraceScope = GlobalTracer.Instance.BuildSpan("JobService.GetJobTimingAsync").StartActive();
			TraceScope.Span.SetTag("Job", Job.Id);

			using IDisposable Scope = Logger.BeginScope("GetJobTimingAsync({JobId})", Job.Id);

			IGraph Graph = await Graphs.GetAsync(Job.GraphHash);

			Dictionary<string, JobStepTimingData> CachedNewSteps = new Dictionary<string, JobStepTimingData>();
			for (; ; )
			{
				// Try to get the current timing document
				IJobTiming? JobTiming = await JobTimings.TryGetAsync(Job.Id);

				// Calculate timing info for any missing steps
				List<JobStepTimingData> NewSteps = new List<JobStepTimingData>();
				foreach (IJobStepBatch Batch in Job.Batches)
				{
					INodeGroup Group = Graph.Groups[Batch.GroupIdx];
					foreach (IJobStep Step in Batch.Steps)
					{
						INode Node = Group.Nodes[Step.NodeIdx];
						if (JobTiming == null || !JobTiming.TryGetStepTiming(Node.Name, out _))
						{
							JobStepTimingData? StepTimingData;
							if (!CachedNewSteps.TryGetValue(Node.Name, out StepTimingData))
							{
								StepTimingData = await GetStepTimingInfo(Job.StreamId, Job.TemplateId, Node.Name, Job.Change);
							}
							NewSteps.Add(StepTimingData);
						}
					}
				}

				// Try to add or update the timing document
				if (JobTiming == null)
				{
					IJobTiming? NewJobTiming = await JobTimings.TryAddAsync(Job.Id, NewSteps);
					if (NewJobTiming != null)
					{
						return NewJobTiming;
					}
				}
				else if (NewSteps.Count == 0)
				{
					return JobTiming;
				}
				else
				{
					IJobTiming? NewJobTiming = await JobTimings.TryAddStepsAsync(JobTiming, NewSteps);
					if (NewJobTiming != null)
					{
						return NewJobTiming;
					}
				}

				// Update the cached list of steps for the next iteration
				foreach (JobStepTimingData NewStep in NewSteps)
				{
					CachedNewSteps[NewStep.Name] = NewStep;
				}
			}
		}

		/// <summary>
		/// Finds the average duration for the given step
		/// </summary>
		/// <param name="StreamId">The stream to search</param>
		/// <param name="TemplateId">The template id</param>
		/// <param name="NodeName">Name of the node</param>
		/// <param name="Change">Maximum changelist to consider</param>
		/// <returns>Expected duration for the given step</returns>
		async Task<JobStepTimingData> GetStepTimingInfo(StreamId StreamId, TemplateRefId TemplateId, string NodeName, int? Change)
		{
			using IScope TraceScope = GlobalTracer.Instance.BuildSpan("JobService.GetStepTimingInfo").StartActive();
			TraceScope.Span.SetTag("StreamId", StreamId);
			TraceScope.Span.SetTag("TemplateId", TemplateId);
			TraceScope.Span.SetTag("NodeName", NodeName);
			TraceScope.Span.SetTag("Change", Change);
			
			// Find all the steps matching the given criteria
			List<IJobStepRef> Steps = await JobStepRefs.GetStepsForNodeAsync(StreamId, TemplateId, NodeName, Change, false, 10);

			// Sum up all the durations and wait times
			int Count = 0;
			float WaitTimeSum = 0;
			float InitTimeSum = 0;
			float DurationSum = 0;

			foreach (IJobStepRef Step in Steps)
			{
				if (Step.FinishTimeUtc != null)
				{
					WaitTimeSum += Step.BatchWaitTime;
					InitTimeSum += Step.BatchInitTime;
					DurationSum += (float)(Step.FinishTimeUtc.Value - Step.StartTimeUtc).TotalSeconds;
					Count++;
				}
			}

			// Compute the averages
			if (Count == 0)
			{
				return new JobStepTimingData(NodeName, null, null, null);
			}
			else
			{
				return new JobStepTimingData(NodeName, WaitTimeSum / Count, InitTimeSum / Count, DurationSum / Count);
			}
		}

		/// <summary>
		/// Updates the state of a batch
		/// </summary>
		/// <param name="Job">Job to update</param>
		/// <param name="BatchId">Unique id of the batch to update</param>
		/// <param name="NewLogId">The new log file id</param>
		/// <param name="NewState">New state of the jobstep</param>
		/// <returns>True if the job was updated, false if it was deleted</returns>
		public async Task<IJob?> UpdateBatchAsync(IJob Job, SubResourceId BatchId, LogId? NewLogId = null, JobStepBatchState? NewState = null)
		{
			using IScope TraceScope = GlobalTracer.Instance.BuildSpan("JobService.UpdateBatchAsync").StartActive();
			TraceScope.Span.SetTag("Job", Job.Id);
			TraceScope.Span.SetTag("BatchId", BatchId);
			TraceScope.Span.SetTag("NewLogId", NewLogId);
			TraceScope.Span.SetTag("NewState", NewState.ToString());

			using IDisposable Scope = Logger.BeginScope("UpdateBatchAsync({JobId})", Job.Id);

			bool bCheckForBadAgent = true;
			for (; ; )
			{
				// Find the index of the appropriate batch
				int BatchIdx = Job.Batches.FindIndex(x => x.Id == BatchId);
				if (BatchIdx == -1)
				{
					return null;
				}

				IJobStepBatch Batch = Job.Batches[BatchIdx];

				// If the batch has already been marked as complete, error out
				if (Batch.State == JobStepBatchState.Complete)
				{
					return null;
				}

				// If we're marking the batch as complete before the agent has run everything, mark it to conform
				JobStepBatchError? NewError = null;
				if (NewState == JobStepBatchState.Complete && Batch.AgentId != null)
				{
					if (Batch.Steps.Any(x => x.State == JobStepState.Waiting || x.State == JobStepState.Ready))
					{
						// Mark the batch as incomplete
						NewError = JobStepBatchError.Incomplete;

						// Find the agent and set the conform flag
						if (bCheckForBadAgent)
						{
							for (; ; )
							{
								IAgent? Agent = await Agents.GetAsync(Batch.AgentId.Value);
								if (Agent == null || Agent.RequestConform)
								{
									break;
								}
								if (await Agents.TryUpdateSettingsAsync(Agent, bRequestConform: true) != null)
								{
									Logger.LogError("Agent {AgentId} did not complete lease; marking for conform", Agent.Id);
									break;
								}
							}
							bCheckForBadAgent = false;
						}
					}
				}

				// Update the batch state
				IJob? NewJob = await TryUpdateBatchAsync(Job, BatchId, NewLogId, NewState, NewError);
				if (NewJob != null)
				{
					return NewJob;
				}

				// Update the job
				NewJob = await GetJobAsync(Job.Id);
				if (NewJob == null)
				{
					return null;
				}

				Job = NewJob;
			}
		}

		/// <summary>
		/// Attempts to update the state of a batch
		/// </summary>
		/// <param name="Job">Job to update</param>
		/// <param name="BatchId">Unique id of the batch to update</param>
		/// <param name="NewLogId">The new log file id</param>
		/// <param name="NewState">New state of the jobstep</param>
		/// <param name="NewError">New error state</param>
		/// <returns>The updated job, otherwise null</returns>
		public async Task<IJob?> TryUpdateBatchAsync(IJob Job, SubResourceId BatchId, LogId? NewLogId = null, JobStepBatchState? NewState = null, JobStepBatchError? NewError = null)
		{
			IGraph Graph = await GetGraphAsync(Job);
			return await Jobs.TryUpdateBatchAsync(Job, Graph, BatchId, NewLogId, NewState, NewError);
		}

		/// <summary>
		/// Update a jobstep state
		/// </summary>
		/// <param name="Job">Job to update</param>
		/// <param name="BatchId">Unique id of the batch containing the step</param>
		/// <param name="StepId">Unique id of the step to update</param>
		/// <param name="NewState">New state of the jobstep</param>
		/// <param name="NewOutcome">New outcome of the jobstep</param>
		/// <param name="NewAbortRequested">New state of abort request</param>
		/// <param name="NewAbortByUserId">New user that requested the abort</param>
		/// <param name="NewLogId">New log id for the jobstep</param>
		/// <param name="NewNotificationTriggerId">New notification trigger id for the jobstep</param>
		/// <param name="NewRetryByUserId">Whether the step should be retried</param>
		/// <param name="NewPriority">New priority for this step</param>
		/// <param name="NewReports">New list of reports</param>
		/// <param name="NewProperties">Property changes. Any properties with a null value will be removed.</param>
		/// <returns>True if the job was updated, false if it was deleted in the meantime</returns>
		public async Task<IJob?> UpdateStepAsync(IJob Job, SubResourceId BatchId, SubResourceId StepId, JobStepState NewState = JobStepState.Unspecified, JobStepOutcome NewOutcome = JobStepOutcome.Unspecified, bool? NewAbortRequested = null, UserId? NewAbortByUserId = null, LogId? NewLogId = null, ObjectId? NewNotificationTriggerId = null, UserId? NewRetryByUserId = null, Priority? NewPriority = null, List<Report>? NewReports = null, Dictionary<string, string?>? NewProperties = null)
		{
			using IScope TraceScope = GlobalTracer.Instance.BuildSpan("JobService.UpdateStepAsync").StartActive();
			TraceScope.Span.SetTag("Job", Job.Id);
			TraceScope.Span.SetTag("BatchId", BatchId);
			TraceScope.Span.SetTag("StepId", StepId);
			
			using IDisposable Scope = Logger.BeginScope("UpdateStepAsync({JobId})", Job.Id);
			for (; ;)
			{
				IJob? NewJob = await TryUpdateStepAsync(Job, BatchId, StepId, NewState, NewOutcome, NewAbortRequested, NewAbortByUserId, NewLogId, NewNotificationTriggerId, NewRetryByUserId, NewPriority, NewReports, NewProperties);
				if (NewJob != null)
				{
					return NewJob;
				}

				NewJob = await GetJobAsync(Job.Id);
				if(NewJob == null)
				{
					return null;
				}

				Job = NewJob;
			}
		}

		/// <summary>
		/// Update a jobstep state
		/// </summary>
		/// <param name="Job">Job to update</param>
		/// <param name="BatchId">Unique id of the batch containing the step</param>
		/// <param name="StepId">Unique id of the step to update</param>
		/// <param name="NewState">New state of the jobstep</param>
		/// <param name="NewOutcome">New outcome of the jobstep</param>
		/// <param name="NewAbortRequested">New state for request abort</param>
		/// <param name="NewAbortByUserId">New name of user that requested the abort</param>
		/// <param name="NewLogId">New log id for the jobstep</param>
		/// <param name="NewTriggerId">New trigger id for the jobstep</param>
		/// <param name="NewRetryByUserId">Whether the step should be retried</param>
		/// <param name="NewPriority">New priority for this step</param>
		/// <param name="NewReports">New reports</param>
		/// <param name="NewProperties">Property changes. Any properties with a null value will be removed.</param>
		/// <returns>True if the job was updated, false if it was deleted in the meantime</returns>
		public async Task<IJob?> TryUpdateStepAsync(IJob Job, SubResourceId BatchId, SubResourceId StepId, JobStepState NewState = JobStepState.Unspecified, JobStepOutcome NewOutcome = JobStepOutcome.Unspecified, bool? NewAbortRequested = null, UserId? NewAbortByUserId = null, LogId? NewLogId = null, ObjectId? NewTriggerId = null, UserId? NewRetryByUserId = null, Priority? NewPriority = null, List<Report>? NewReports = null, Dictionary<string, string?>? NewProperties = null)
		{
			using IScope TraceScope = GlobalTracer.Instance.BuildSpan("JobService.TryUpdateStepAsync").StartActive();
			TraceScope.Span.SetTag("Job", Job.Id);
			TraceScope.Span.SetTag("BatchId", BatchId);
			TraceScope.Span.SetTag("StepId", StepId);

			using IDisposable Scope = Logger.BeginScope("TryUpdateStepAsync({JobId})", Job.Id);

			// Get the graph for this job
			IGraph Graph = await GetGraphAsync(Job);

			// Make sure the job state is set to unspecified
			JobStepState OldState = JobStepState.Unspecified;
			JobStepOutcome OldOutcome = JobStepOutcome.Unspecified;
			if (NewState != JobStepState.Unspecified)
			{
				if (Job.TryGetStep(BatchId, StepId, out IJobStep? Step))
				{
					OldState = Step.State;
					OldOutcome = Step.Outcome;
				}
			}

			// If we're changing the state of a step, capture the label states beforehand so we can update any that change
			IReadOnlyList<(LabelState, LabelOutcome)>? OldLabelStates = null;
			if (OldState != NewState || OldOutcome != NewOutcome)
			{
				OldLabelStates = Job.GetLabelStates(Graph);
			}

			// Update the step
			IJob? NewJob = await Jobs.TryUpdateStepAsync(Job, Graph, BatchId, StepId, NewState, NewOutcome, NewAbortRequested, NewAbortByUserId, NewLogId, NewTriggerId, NewRetryByUserId, NewPriority, NewReports, NewProperties);
			if (NewJob != null)
			{
				Job = NewJob;

				using IScope DdScope = GlobalTracer.Instance.BuildSpan("TryUpdateStepAsync").StartActive();
				DdScope.Span.SetTag("JobId", Job.Id.ToString());
				DdScope.Span.SetTag("BatchId", BatchId.ToString());
				DdScope.Span.SetTag("StepId", StepId.ToString());
				
				if (OldState != NewState || OldOutcome != NewOutcome)
				{
					Logger.LogDebug("Transitioned job {JobId}, batch {BatchId}, step {StepId} from {OldState} to {NewState}", Job.Id, BatchId, StepId, OldState, NewState);

					// Send any updates for modified badges
					if (OldLabelStates != null)
					{
						using IScope _ = GlobalTracer.Instance.BuildSpan("Send badge updates").StartActive();
						IReadOnlyList<(LabelState, LabelOutcome)> NewLabelStates = Job.GetLabelStates(Graph);
						OnLabelUpdate?.Invoke(Job, OldLabelStates, NewLabelStates);
						await JobTaskSource.UpdateUgsBadges(Job, Graph, OldLabelStates, NewLabelStates);
					}

					// Submit the change if auto-submit is enabled
					if (Job.PreflightChange != 0 && NewState == JobStepState.Completed)
					{
						(JobStepState State, JobStepOutcome Outcome) = Job.GetTargetState();
						if (State == JobStepState.Completed)
						{
							if (Job.AutoSubmit && Outcome == JobStepOutcome.Success && Job.AbortedByUserId == null)
							{
								Job = await AutoSubmitChangeAsync(Job, Graph);
							}
							else if (Job.ClonedPreflightChange != 0)
							{
								IStream? Stream = await StreamService.GetCachedStream(Job.StreamId);
								if (Stream != null)
								{
									await DeleteShelvedChangeAsync(Stream.ClusterName, Job.ClonedPreflightChange);
								}
							}
						}
					}

					// Notify subscribers
					if (NewState == JobStepState.Completed)
					{
						using IScope _ = GlobalTracer.Instance.BuildSpan("Notify subscribers").StartActive();
						OnJobStepComplete?.Invoke(Job, Graph, BatchId, StepId);
					}

					// Create any downstream jobs
					if (NewState == JobStepState.Completed && NewOutcome != JobStepOutcome.Failure)
					{
						using IScope _ = GlobalTracer.Instance.BuildSpan("Create downstream jobs").StartActive();
						for (int Idx = 0; Idx < Job.ChainedJobs.Count; Idx++)
						{
							IChainedJob JobTrigger = Job.ChainedJobs[Idx];
							if (JobTrigger.JobId == null)
							{
								JobStepOutcome JobTriggerOutcome = Job.GetTargetOutcome(Graph, JobTrigger.Target);
								if (JobTriggerOutcome == JobStepOutcome.Success || JobTriggerOutcome == JobStepOutcome.Warnings)
								{
									Job = await FireJobTriggerAsync(Job, Graph, JobTrigger) ?? Job;
								}
							}
						}
					}

					// Update the jobstep ref if it completed
					if (NewState == JobStepState.Running || NewState == JobStepState.Completed || NewState == JobStepState.Aborted)
					{
						using IScope _ = GlobalTracer.Instance.BuildSpan("Update job step ref").StartActive();
						if (Job.TryGetBatch(BatchId, out IJobStepBatch? Batch) && Batch.TryGetStep(StepId, out IJobStep? Step) && Step.StartTimeUtc != null)
						{
							await JobStepRefs.UpdateAsync(Job, Batch, Step, Graph);
						}
					}

					// Update any issues that depend on this step
					if (NewState == JobStepState.Completed && Job.UpdateIssues)
					{
						if (IssueService != null)
						{
							using IScope _ = GlobalTracer.Instance.BuildSpan("Update issues (V2)").StartActive();
							try
							{
								await IssueService.UpdateCompleteStep(Job, Graph, BatchId, StepId);
							}
							catch(Exception Ex)
							{
								Logger.LogError(Ex, "Exception while updating issue service for job {JobId}:{BatchId}:{StepId}: {Message}", Job.Id, BatchId, StepId, Ex.Message);
							}
						}
					}
				}

				using (IScope DispatchScope = GlobalTracer.Instance.BuildSpan("Update queued jobs").StartActive())
				{
					// Notify the dispatch service that the job has changed
					JobTaskSource.UpdateQueuedJob(Job, Graph);
				}

				return Job;
			}

			return null;
		}

		/// <summary>
		/// Submit the given change for a preflight
		/// </summary>
		/// <param name="Job">The job being run</param>
		/// <param name="Graph">Graph for the job</param>
		/// <returns></returns>
		[SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
		private async Task<IJob> AutoSubmitChangeAsync(IJob Job, IGraph Graph)
		{
			using IScope TraceScope = GlobalTracer.Instance.BuildSpan("JobService.AutoSubmitChangeAsync").StartActive();
			TraceScope.Span.SetTag("Job", Job.Id);
			TraceScope.Span.SetTag("Graph", Graph.Id);
			
			int? Change;
			string Message;
			try
			{
				IStream? Stream = await StreamService.GetCachedStream(Job.StreamId);
				if (Stream != null)
				{
					int ClonedPreflightChange = Job.ClonedPreflightChange;
					if (ClonedPreflightChange == 0)
					{
						if (ShouldClonePreflightChange(Job.StreamId))
						{
							ClonedPreflightChange = await CloneShelvedChangeAsync(Stream.ClusterName, Job.PreflightChange);
						}
						else
						{
							ClonedPreflightChange = Job.PreflightChange;
						}
					}

					Logger.LogInformation("Updating description for {ClonedPreflightChange}", ClonedPreflightChange);

					ChangeDetails Details = await PerforceService.GetChangeDetailsAsync(Stream.ClusterName, Stream.Name, ClonedPreflightChange, null);
					await PerforceService.UpdateChangelistDescription(Stream.ClusterName, ClonedPreflightChange, Details.Description.TrimEnd() + $"\n#preflight {Job.Id}");

					Logger.LogInformation("Submitting change {Change} (through {ChangeCopy}) after successful completion of {JobId}", Job.PreflightChange, ClonedPreflightChange, Job.Id);
					(Change, Message) = await PerforceService.SubmitShelvedChangeAsync(Stream.ClusterName, ClonedPreflightChange, Job.PreflightChange);

					Logger.LogInformation("Attempt to submit {Change} (through {ChangeCopy}): {Message}", Job.PreflightChange, ClonedPreflightChange, Message);

					if (ShouldClonePreflightChange(Job.StreamId))
					{
						if (Change != null && Job.ClonedPreflightChange != 0)
						{
							await DeleteShelvedChangeAsync(Stream.ClusterName, Job.PreflightChange);
						}
					}
					else
					{
						if (Change != null && Job.PreflightChange != 0)
						{
							await DeleteShelvedChangeAsync(Stream.ClusterName, Job.PreflightChange);
						}
					}
				}
				else
				{
					(Change, Message) = ((int?)null, "Stream not found");
				}
			}
			catch (Exception Ex)
			{
				(Change, Message) = ((int?)null, "Internal error");
				Logger.LogError(Ex, "Unable to submit shelved change");
			}

			for (; ; )
			{
				IJob? NewJob = await Jobs.TryUpdateJobAsync(Job, Graph, AutoSubmitChange: Change, AutoSubmitMessage: Message);
				if (NewJob != null)
				{
					return NewJob;
				}

				NewJob = await GetJobAsync(Job.Id);
				if (NewJob == null)
				{
					return Job;
				}

				Job = NewJob;
			}
		}

		/// <summary>
		/// Clone a shelved changelist for running a preflight
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="Change">The changelist to clone</param>
		/// <returns></returns>
		private async Task<int> CloneShelvedChangeAsync(string ClusterName, int Change)
		{
			int ClonedChange;
			try
			{
				ClonedChange = await PerforceService.DuplicateShelvedChangeAsync(ClusterName, Change);
				Logger.LogInformation("CL {Change} was duplicated into {ClonedChange}", Change, ClonedChange);
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Unable to CL {Change} for preflight: {Message}", Change, Ex.Message);
				throw;
			}
			return ClonedChange;
		}

		/// <summary>
		/// Deletes a shelved changelist, catching and logging any exceptions
		/// </summary>
		/// <param name="ClusterName"></param>
		/// <param name="Change">The changelist to delete</param>
		/// <returns>True if the change was deleted successfully, false otherwise</returns>
		[SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
		private async Task<bool> DeleteShelvedChangeAsync(string ClusterName, int Change)
		{
			Logger.LogInformation("Removing shelf {Change}", Change);
			try
			{
				await PerforceService.DeleteShelvedChangeAsync(ClusterName, Change);
				return true;
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Unable to delete shelved change {Change}", Change);
				return false;
			}
		}

		/// <summary>
		/// Fires a trigger for a chained job
		/// </summary>
		/// <param name="Job">The job containing the trigger</param>
		/// <param name="Graph">Graph for the job containing the trigger</param>
		/// <param name="JobTrigger">The trigger object to fire</param>
		/// <returns>New job instance</returns>
		private async Task<IJob?> FireJobTriggerAsync(IJob Job, IGraph Graph, IChainedJob JobTrigger)
		{
			using IScope TraceScope = GlobalTracer.Instance.BuildSpan("JobService.FireJobTriggerAsync").StartActive();
			TraceScope.Span.SetTag("Job", Job.Id);
			TraceScope.Span.SetTag("Graph", Graph.Id);
			TraceScope.Span.SetTag("JobTrigger.Target", JobTrigger.Target);
			TraceScope.Span.SetTag("JobTrigger.JobId", JobTrigger.JobId);
			TraceScope.Span.SetTag("JobTrigger.TemplateRefId", JobTrigger.TemplateRefId);
			
			for (; ; )
			{
				// Update the job
				JobId ChainedJobId = JobId.GenerateNewId();

				IJob? NewJob = await Jobs.TryUpdateJobAsync(Job, Graph, JobTrigger: new KeyValuePair<TemplateRefId, JobId>(JobTrigger.TemplateRefId, ChainedJobId));
				if(NewJob != null)
				{
					IStream? Stream = await StreamService.GetStreamAsync(NewJob.StreamId);
					if(Stream == null)
					{
						Logger.LogWarning("Cannot find stream {StreamId} for downstream job", NewJob.StreamId);
						break;
					}

					TemplateRef? TemplateRef;
					if (!Stream.Templates.TryGetValue(JobTrigger.TemplateRefId, out TemplateRef))
					{
						Logger.LogWarning("Cannot find template {TemplateRefId} in stream {StreamId}", JobTrigger.TemplateRefId, NewJob.StreamId);
						break;
					}

					ITemplate? Template = await TemplateCollection.GetAsync(TemplateRef.Hash);
					if (Template == null)
					{
						Logger.LogWarning("Cannot find template {TemplateHash}", TemplateRef.Hash);
						break;
					}

					IGraph TriggerGraph = await Graphs.AddAsync(Template);
					Logger.LogInformation("Creating downstream job {ChainedJobId} from job {JobId}", ChainedJobId, NewJob.Id);

					await CreateJobAsync(ChainedJobId, Stream, JobTrigger.TemplateRefId, TemplateRef.Hash, TriggerGraph, TemplateRef.Name, NewJob.Change, NewJob.CodeChange, NewJob.PreflightChange, NewJob.ClonedPreflightChange, NewJob.StartedByUserId, Template.Priority, null, NewJob.UpdateIssues, TemplateRef.ChainedJobs, false, false, TemplateRef.NotificationChannel, TemplateRef.NotificationChannelFilter, Template.Arguments);
					return NewJob;
				}

				// Fetch the job again
				NewJob = await Jobs.GetAsync(Job.Id);
				if(NewJob == null)
				{
					break;
				}

				Job = NewJob;
			}
			return null;
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular stream
		/// </summary>
		/// <param name="Acl">The ACL to check</param>
		/// <param name="StreamId">The stream containing this job</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">Cache of stream permissions</param>
		/// <returns>True if the action is authorized</returns>
		private Task<bool> AuthorizeAsync(Acl? Acl, StreamId StreamId, AclAction Action, ClaimsPrincipal User, StreamPermissionsCache? Cache)
		{
			// Do the regular authorization
			bool? Result = Acl?.Authorize(Action, User);
			if (Result == null)
			{
				return StreamService.AuthorizeAsync(StreamId, Action, User, Cache);
			}
			else
			{
				return Task.FromResult(Result.Value);
			}
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular stream
		/// </summary>
		/// <param name="Job">The job to check</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">Cache of stream permissions</param>
		/// <returns>True if the action is authorized</returns>
		public Task<bool> AuthorizeAsync(IJob Job, AclAction Action, ClaimsPrincipal User, StreamPermissionsCache? Cache)
		{
			return AuthorizeAsync(Job.Acl, Job.StreamId, Action, User, Cache);
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular stream
		/// </summary>
		/// <param name="JobId">The job to check</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">Cache of job permissions</param>
		/// <returns>True if the action is authorized</returns>
		public async Task<bool> AuthorizeAsync(JobId JobId, AclAction Action, ClaimsPrincipal User, JobPermissionsCache? Cache)
		{
			IJobPermissions? Permissions;
			if (Cache == null)
			{
				Permissions = await GetJobPermissionsAsync(JobId);
			}
			else if (!Cache.Jobs.TryGetValue(JobId, out Permissions))
			{
				Permissions = await GetJobPermissionsAsync(JobId);
				Cache.Jobs.Add(JobId, Permissions);
			}
			return Permissions != null && await AuthorizeAsync(Permissions.Acl, Permissions.StreamId, Action, User, Cache);
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular stream
		/// </summary>
		/// <param name="Job">The job to check</param>
		/// <param name="User">The principal to authorize</param>
		/// <returns>True if the action is authorized</returns>
		public static bool AuthorizeSession(IJob Job, ClaimsPrincipal User)
		{
			return Job.Batches.Any(x => AuthorizeSession(x, User));
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular stream
		/// </summary>
		/// <param name="Batch">The batch being executed</param>
		/// <param name="User">The principal to authorize</param>
		/// <returns>True if the action is authorized</returns>
		public static bool AuthorizeSession(IJobStepBatch Batch, ClaimsPrincipal User)
		{
			if(Batch.SessionId != null)
			{
				foreach (Claim Claim in User.Claims)
				{
					if(Claim.Type == HordeClaimTypes.AgentSessionId)
					{
						ObjectId SessionId;
						if (ObjectId.TryParse(Claim.Value, out SessionId) && SessionId == Batch.SessionId.Value)
						{
							return true;
						}
					}
				}
			}
			return false;
		}
		
		/// <summary>
		/// Get a list of each batch's session ID formatted as a string for debugging purposes
		/// </summary>
		/// <param name="Job">The job to list</param>
		/// <returns>List of session IDs</returns>
		public static string GetAllBatchSessionIds(IJob Job)
		{
			return string.Join(",", Job.Batches.Select(b => b.SessionId));
		}
	}
}
