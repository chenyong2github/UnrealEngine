// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Security.Claims;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// State of the job
	/// </summary>
	public enum JobState
	{
		/// <summary>
		/// Waiting for resources
		/// </summary>
		Waiting,

		/// <summary>
		/// Currently running one or more steps
		/// </summary>
		Running,

		/// <summary>
		/// All steps have completed
		/// </summary>
		Complete,
	}

	/// <summary>
	/// Parameters required to create a job
	/// </summary>
	public class CreateJobRequest
	{
		/// <summary>
		/// The stream that this job belongs to
		/// </summary>
		[Required]
		public string StreamId { get; set; }

		/// <summary>
		/// The template for this job
		/// </summary>
		[Required]
		public string TemplateId { get; set; }

		/// <summary>
		/// Name of the job
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// The changelist number to build. Can be null for latest.
		/// </summary>
		public int? Change { get; set; }

		/// <summary>
		/// Parameters to use when selecting the change to execute at.
		/// </summary>
		public ChangeQueryRequest? ChangeQuery { get; set; }

		/// <summary>
		/// The preflight changelist number
		/// </summary>
		public int? PreflightChange { get; set; }

		/// <summary>
		/// Priority for the job
		/// </summary>
		public Priority? Priority { get; set; }

		/// <summary>
		/// Whether to automatically submit the preflighted change on completion
		/// </summary>
		public bool? AutoSubmit { get; set; }

		/// <summary>
		/// Whether to update issues based on the outcome of this job
		/// </summary>
		public bool? UpdateIssues { get; set; }

		/// <summary>
		/// Arguments for the job
		/// </summary>
		public List<string>? Arguments { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		public CreateJobRequest(string StreamId, string TemplateId)
		{
			this.StreamId = StreamId;
			this.TemplateId = TemplateId;
		}
	}

	/// <summary>
	/// Response from creating a new job
	/// </summary>
	public class CreateJobResponse
	{
		/// <summary>
		/// Unique id for the new job
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Unique id for the new job</param>
		public CreateJobResponse(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Updates an existing job
	/// </summary>
	public class UpdateJobRequest
	{
		/// <summary>
		/// New name for the job
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// New priority for the job
		/// </summary>
		public Priority? Priority { get; set; }

		/// <summary>
		/// Set whether the job should be automatically submitted or not
		/// </summary>
		public bool? AutoSubmit { get; set; }

		/// <summary>
		/// Mark this job as aborted
		/// </summary>
		public bool? Aborted { get; set; }

		/// <summary>
		/// New list of arguments for the job. Only -Target= arguments can be modified after the job has started.
		/// </summary>
		public List<string>? Arguments { get; set; }

		/// <summary>
		/// Custom permissions for this object
		/// </summary>
		public UpdateAclRequest? Acl { get; set; }
	}

	/// <summary>
	/// Information about a report associated with a job
	/// </summary>
	public class GetReportResponse
	{
		/// <summary>
		/// Name of the report
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Report placement
		/// </summary>
		public HordeCommon.Rpc.ReportPlacement Placement { get; set; }

		/// <summary>
		/// The artifact id
		/// </summary>
		public string ArtifactId { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Report"></param>
		public GetReportResponse(IReport Report)
		{
			Name = Report.Name;
			Placement = Report.Placement;
			ArtifactId = Report.ArtifactId.ToString();
		}
	}

	/// <summary>
	/// Information about a job
	/// </summary>
	public class GetJobResponse
	{
		/// <summary>
		/// Unique Id for the job
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Unique id of the stream containing this job
		/// </summary>
		public string StreamId { get; set; }

		/// <summary>
		/// Name of the job
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// The changelist number to build
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// The code changelist
		/// </summary>
		public int? CodeChange { get; set; }

		/// <summary>
		/// The preflight changelist number
		/// </summary>
		public int? PreflightChange { get; set; }

		/// <summary>
		/// The cloned preflight changelist number
		/// </summary>
		public int? ClonedPreflightChange { get; set; }

		/// <summary>
		/// The template type
		/// </summary>
		public string TemplateId { get; set; }

		/// <summary>
		/// Hash of the actual template data
		/// </summary>
		public string TemplateHash { get; set; }

		/// <summary>
		/// Hash of the graph for this job
		/// </summary>
		public string? GraphHash { get; set; }

		/// <summary>
		/// The user that started this job [DEPRECATED]
		/// </summary>
		public string? StartedByUserId { get; set; }

		/// <summary>
		/// The user that started this job [DEPRECATED]
		/// </summary>
		public string? StartedByUser { get; set; }

		/// <summary>
		/// The user that started this job
		/// </summary>
		public GetThinUserInfoResponse? StartedByUserInfo { get; set; }

		/// <summary>
		/// The user that aborted this job [DEPRECATED]
		/// </summary>
		public string? AbortedByUser { get; set; }

		/// <summary>
		/// The user that aborted this job
		/// </summary>
		public GetThinUserInfoResponse? AbortedByUserInfo { get; set; }

		/// <summary>
		/// Priority of the job
		/// </summary>
		public Priority Priority { get; set; }

		/// <summary>
		/// Whether the change will automatically be submitted or not
		/// </summary>
		public bool AutoSubmit { get; set; }

		/// <summary>
		/// The submitted changelist number
		/// </summary>
		public int? AutoSubmitChange { get; }

		/// <summary>
		/// Message produced by trying to auto-submit the change
		/// </summary>
		public string? AutoSubmitMessage { get; }

		/// <summary>
		/// Time that the job was created
		/// </summary>
		public DateTimeOffset CreateTime { get; set; }

		/// <summary>
		/// The global job state
		/// </summary>
		public JobState State { get; set; }

		/// <summary>
		/// Array of jobstep batches
		/// </summary>
		public List<GetBatchResponse>? Batches { get; set; }

		/// <summary>
		/// List of labels
		/// </summary>
		public List<GetLabelStateResponse>? Labels { get; set; }

		/// <summary>
		/// The default label, containing the state of all steps that are otherwise not matched.
		/// </summary>
		public GetDefaultLabelStateResponse? DefaultLabel { get; set; } 

		/// <summary>
		/// List of reports
		/// </summary>
		public List<GetReportResponse>? Reports { get; set; }

		/// <summary>
		/// Parameters for the job
		/// </summary>
		public List<string> Arguments { get; set; }

		/// <summary>
		/// The last update time for this job
		/// </summary>
		public DateTimeOffset UpdateTime { get; set; }

		/// <summary>
		/// Per-object permissions
		/// </summary>
		public GetAclResponse? Acl { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Job">Job to create a response for</param>
		/// <param name="StartedByUserInfo">User that started this job</param>
		/// <param name="AbortedByUserInfo">User that aborted this job</param>
		/// <param name="AclResponse">The ACL response object</param>
		public GetJobResponse(IJob Job, GetThinUserInfoResponse? StartedByUserInfo, GetThinUserInfoResponse? AbortedByUserInfo, GetAclResponse? AclResponse)
		{
			this.Id = Job.Id.ToString();
			this.StreamId = Job.StreamId.ToString();
			this.Name = Job.Name;
			this.Change = Job.Change;
			this.CodeChange = (Job.CodeChange != 0) ? (int?)Job.CodeChange : null;
			this.PreflightChange = (Job.PreflightChange != 0) ? (int?)Job.PreflightChange : null;
			this.ClonedPreflightChange = (Job.ClonedPreflightChange != 0) ? (int?)Job.ClonedPreflightChange : null;
			this.TemplateId = Job.TemplateId.ToString();
			this.TemplateHash = Job.TemplateHash?.ToString() ?? String.Empty;
			this.GraphHash = Job.GraphHash.ToString();
			this.StartedByUserId = Job.StartedByUserId?.ToString();
			this.StartedByUser = StartedByUserInfo?.Login;
			this.StartedByUserInfo = StartedByUserInfo;
			this.AbortedByUser = AbortedByUserInfo?.Login;
			this.AbortedByUserInfo = AbortedByUserInfo;
			this.CreateTime = new DateTimeOffset(Job.CreateTimeUtc);
			this.State = Job.GetState();
			this.Priority = Job.Priority;
			this.AutoSubmit = Job.AutoSubmit;
			this.AutoSubmitChange = Job.AutoSubmitChange;
			this.AutoSubmitMessage = Job.AutoSubmitMessage;
			this.Reports = Job.Reports?.ConvertAll(x => new GetReportResponse(x));
			this.Arguments = Job.Arguments.ToList();
			this.UpdateTime = new DateTimeOffset(Job.UpdateTimeUtc);
			this.Acl = AclResponse;
		}
	}

	/// <summary>
	/// The timing info for 
	/// </summary>
	public class GetJobTimingResponse
	{
		/// <summary>
		/// Timing info for each step
		/// </summary>
		public Dictionary<string, GetStepTimingInfoResponse> Steps { get; set; }

		/// <summary>
		/// Timing information for each label
		/// </summary>
		public List<GetLabelTimingInfoResponse> Labels { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Steps">Timing info for each steps</param>
		/// <param name="Labels">Timing info for each label</param>
		public GetJobTimingResponse(Dictionary<string, GetStepTimingInfoResponse> Steps, List<GetLabelTimingInfoResponse> Labels)
		{
			this.Steps = Steps;
			this.Labels = Labels;
		}
	}

	/// <summary>
	/// Request used to update a jobstep
	/// </summary>
	public class UpdateStepRequest
	{
		/// <summary>
		/// The new jobstep state
		/// </summary>
		public JobStepState State { get; set; } = JobStepState.Unspecified;

		/// <summary>
		/// Outcome from the jobstep
		/// </summary>
		public JobStepOutcome Outcome { get; set; } = JobStepOutcome.Unspecified;
		
		/// <summary>
		/// If the step has been requested to abort
		/// </summary>
		public bool? AbortRequested { get; set; }

		/// <summary>
		/// Specifies the log file id for this step
		/// </summary>
		public string? LogId { get; set; }

		/// <summary>
		/// Whether the step should be re-run
		/// </summary>
		public bool? Retry { get; set; }

		/// <summary>
		/// New priority for this step
		/// </summary>
		public Priority? Priority { get; set; }

		/// <summary>
		/// Properties to set. Any entries with a null value will be removed.
		/// </summary>
		public Dictionary<string, string?>? Properties { get; set; }
	}

	/// <summary>
	/// Response object when updating a jobstep
	/// </summary>
	public class UpdateStepResponse
	{
		/// <summary>
		/// If a new step is created (due to specifying the retry flag), specifies the batch id
		/// </summary>
		public string? BatchId { get; set; }

		/// <summary>
		/// If a step is retried, includes the new step id
		/// </summary>
		public string? StepId { get; set; }
	}

	/// <summary>
	/// Returns information about a jobstep
	/// </summary>
	public class GetStepResponse
	{
		/// <summary>
		/// The unique id of the step
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Index of the node which this jobstep is to execute
		/// </summary>
		public int NodeIdx { get; set; }

		/// <summary>
		/// Current state of the job step. This is updated automatically when runs complete.
		/// </summary>
		public JobStepState State { get; set; }

		/// <summary>
		/// Current outcome of the jobstep
		/// </summary>
		public JobStepOutcome Outcome { get; set; }
		
		/// <summary>
		/// If the step has been requested to abort
		/// </summary>
		public bool AbortRequested { get; set; }
		
		/// <summary>
		/// Name of the user that requested the abort of this step [DEPRECATED]
		/// </summary>
		public string? AbortByUser { get; set; }

		/// <summary>
		/// The user that requested this step be run again 
		/// </summary>
		public GetThinUserInfoResponse? AbortedByUserInfo { get; set; }

		/// <summary>
		/// Name of the user that requested this step be run again [DEPRECATED]
		/// </summary>
		public string? RetryByUser { get; set; }

		/// <summary>
		/// The user that requested this step be run again 
		/// </summary>
		public GetThinUserInfoResponse? RetriedByUserInfo { get; set; }

		/// <summary>
		/// The log id for this step
		/// </summary>
		public string? LogId { get; set; }

		/// <summary>
		/// Time at which the batch was ready (UTC).
		/// </summary>
		public DateTimeOffset? ReadyTime { get; set; }

		/// <summary>
		/// Time at which the batch started (UTC).
		/// </summary>
		public DateTimeOffset? StartTime { get; set; }

		/// <summary>
		/// Time at which the batch finished (UTC)
		/// </summary>
		public DateTimeOffset? FinishTime { get; set; }

		/// <summary>
		/// List of reports
		/// </summary>
		public List<GetReportResponse>? Reports { get; set; }

		/// <summary>
		/// User-defined properties for this jobstep.
		/// </summary>
		public Dictionary<string, string>? Properties { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Step">The step to construct from</param>
		/// <param name="AbortedByUserInfo">User that aborted this step</param>
		/// <param name="RetriedByUserInfo">User that retried this step</param>
		public GetStepResponse(IJobStep Step, GetThinUserInfoResponse? AbortedByUserInfo, GetThinUserInfoResponse? RetriedByUserInfo)
		{
			this.Id = Step.Id.ToString();
			this.NodeIdx = Step.NodeIdx;
			this.State = Step.State;
			this.Outcome = Step.Outcome;
			this.AbortRequested = Step.AbortRequested;
			this.AbortByUser = AbortedByUserInfo?.Login;
			this.AbortedByUserInfo = AbortedByUserInfo;
			this.RetryByUser = RetriedByUserInfo?.Login;
			this.RetriedByUserInfo = RetriedByUserInfo;
			this.LogId = Step.LogId?.ToString();
			this.ReadyTime = Step.ReadyTimeUtc;
			this.StartTime = Step.StartTimeUtc;
			this.FinishTime = Step.FinishTimeUtc;
			this.Reports = Step.Reports?.ConvertAll(x => new GetReportResponse(x));

			if (Step.Properties != null && Step.Properties.Count > 0)
			{
				this.Properties = Step.Properties;
			}
		}
	}

	/// <summary>
	/// The state of a particular run
	/// </summary>
	[JsonConverter(typeof(JsonStringEnumConverter))]
	public enum JobStepBatchState
	{
		/// <summary>
		/// Waiting for dependencies of at least one jobstep to complete
		/// </summary>
		Waiting = 0,

		/// <summary>
		/// Ready to execute
		/// </summary>
		Ready = 1,

		/// <summary>
		/// Preparing to execute work
		/// </summary>
		Starting = 2,

		/// <summary>
		/// Executing work
		/// </summary>
		Running = 3,

		/// <summary>
		/// Preparing to stop
		/// </summary>
		Stopping = 4,

		/// <summary>
		/// All steps have finished executing
		/// </summary>
		Complete = 5
	}

	/// <summary>
	/// Error code for a batch not being executed
	/// </summary>
	[JsonConverter(typeof(JsonStringEnumConverter))]
	public enum JobStepBatchError
	{
		/// <summary>
		/// No error
		/// </summary>
		None = 0,

		/// <summary>
		/// The stream for this job is unknown
		/// </summary>
		UnknownStream = 1,

		/// <summary>
		/// The given agent type for this batch was not valid for this stream
		/// </summary>
		UnknownAgentType = 2,

		/// <summary>
		/// The pool id referenced by the agent type was not found
		/// </summary>
		UnknownPool = 3,

		/// <summary>
		/// There are no agents in the given pool currently online
		/// </summary>
		NoAgentsInPool = 4,

		/// <summary>
		/// There are no agents in this pool that are onlinbe
		/// </summary>
		NoAgentsOnline = 5,

		/// <summary>
		/// Unknown workspace referenced by the agent type
		/// </summary>
		UnknownWorkspace = 6,

		/// <summary>
		/// Cancelled
		/// </summary>
		Cancelled = 7,

		/// <summary>
		/// Lost connection with the agent machine
		/// </summary>
		LostConnection = 8,

		/// <summary>
		/// Lease terminated prematurely
		/// </summary>
		Incomplete = 9,
	}

	/// <summary>
	/// Request to update a jobstep batch
	/// </summary>
	public class UpdateBatchRequest
	{
		/// <summary>
		/// The new log file id
		/// </summary>
		public string? LogId { get; set; }

		/// <summary>
		/// The state of this batch
		/// </summary>
		public JobStepBatchState? State { get; set; }
	}

	/// <summary>
	/// Information about a jobstep batch
	/// </summary>
	public class GetBatchResponse
	{
		/// <summary>
		/// Unique id for this batch
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// The unique log file id
		/// </summary>
		public string? LogId { get; set; }

		/// <summary>
		/// Index of the group being executed
		/// </summary>
		public int GroupIdx { get; set; }

		/// <summary>
		/// The state of this batch
		/// </summary>
		public JobStepBatchState State { get; set; }

		/// <summary>
		/// Error code for this batch
		/// </summary>
		public JobStepBatchError Error { get; set; }

		/// <summary>
		/// Steps within this run
		/// </summary>
		public List<GetStepResponse> Steps { get; set; }

		/// <summary>
		/// The agent assigned to execute this group
		/// </summary>
		public string? AgentId { get; set; }

		/// <summary>
		/// The agent session holding this lease
		/// </summary>
		public string? SessionId { get; set; }

		/// <summary>
		/// The lease that's executing this group
		/// </summary>
		public string? LeaseId { get; set; }

		/// <summary>
		/// The priority of this batch
		/// </summary>
		public int WeightedPriority { get; set; }

		/// <summary>
		/// Time at which the group started (UTC).
		/// </summary>
		public DateTimeOffset? StartTime { get; set; }

		/// <summary>
		/// Time at which the group finished (UTC)
		/// </summary>
		public DateTimeOffset? FinishTime { get; set; }

		/// <summary>
		/// Converts this batch into a public response object
		/// </summary>
		/// <param name="Batch">The batch to construct from</param>
		/// <param name="Steps">Steps in this batch</param>
		/// <returns>Response instance</returns>
		public GetBatchResponse(IJobStepBatch Batch, List<GetStepResponse> Steps)
		{
			this.Id = Batch.Id.ToString();
			this.LogId = Batch.LogId?.ToString();
			this.GroupIdx = Batch.GroupIdx;
			this.State = Batch.State;
			this.Error = Batch.Error;
			this.Steps = Steps;
			this.AgentId = Batch.AgentId?.ToString();
			this.SessionId = Batch.SessionId?.ToString();
			this.LeaseId = Batch.LeaseId?.ToString();
			this.WeightedPriority = Batch.SchedulePriority;
			this.StartTime = Batch.StartTimeUtc;
			this.FinishTime = Batch.FinishTimeUtc;
		}
	}

	/// <summary>
	/// State of an aggregate
	/// </summary>
	public enum LabelState
	{
		/// <summary>
		/// Aggregate is not currently being built (no required nodes are present)
		/// </summary>
		Unspecified,

		/// <summary>
		/// Steps are still running
		/// </summary>
		Running,

		/// <summary>
		/// All steps are complete
		/// </summary>
		Complete
	}

	/// <summary>
	/// Outcome of an aggregate
	/// </summary>
	public enum LabelOutcome
	{
		/// <summary>
		/// Aggregate is not currently being built
		/// </summary>
		Unspecified,

		/// <summary>
		/// A step dependency failed
		/// </summary>
		Failure,

		/// <summary>
		/// A dependency finished with warnings
		/// </summary>
		Warnings,

		/// <summary>
		/// Successful
		/// </summary>
		Success,
	}

	/// <summary>
	/// State of a label within a job
	/// </summary>
	public class GetLabelStateResponse
	{
		/// <summary>
		/// State of the label
		/// </summary>
		public LabelState? State { get; set; }

		/// <summary>
		/// Outcome of the label
		/// </summary>
		public LabelOutcome? Outcome { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="State">State of the label</param>
		/// <param name="Outcome">Outcome of the label</param>
		public GetLabelStateResponse(LabelState State, LabelOutcome Outcome)
		{
			this.State = State;
			this.Outcome = Outcome;
		}
	}

	/// <summary>
	/// Information about the default label (ie. with inlined list of nodes)
	/// </summary>
	public class GetDefaultLabelStateResponse : GetLabelStateResponse
	{
		/// <summary>
		/// List of nodes covered by default label
		/// </summary>
		public List<string> Nodes { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="State">State of the label</param>
		/// <param name="Outcome">Outcome of the label</param>
		/// <param name="Nodes">List of nodes that are covered by the default label</param>
		public GetDefaultLabelStateResponse(LabelState State, LabelOutcome Outcome, List<string> Nodes)
			: base(State, Outcome)
		{
			this.Nodes = Nodes;
		}
	}

	/// <summary>
	/// Information about the timing info for a particular target
	/// </summary>
	public class GetTimingInfoResponse
	{
		/// <summary>
		/// Wait time on the critical path
		/// </summary>
		public float? TotalWaitTime { get; set; }

		/// <summary>
		/// Sync time on the critical path
		/// </summary>
		public float? TotalInitTime { get; set; }

		/// <summary>
		/// Duration to this point
		/// </summary>
		public float? TotalTimeToComplete { get; set; }

		/// <summary>
		/// Average wait time by the time the job reaches this point
		/// </summary>
		public float? AverageTotalWaitTime { get; set; }

		/// <summary>
		/// Average sync time to this point
		/// </summary>
		public float? AverageTotalInitTime { get; set; }

		/// <summary>
		/// Average duration to this point
		/// </summary>
		public float? AverageTotalTimeToComplete { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="TimingInfo">Timing info to construct from</param>
		public GetTimingInfoResponse(TimingInfo TimingInfo)
		{
			this.TotalWaitTime = (float?)TimingInfo.TotalWaitTime?.TotalSeconds;
			this.TotalInitTime = (float?)TimingInfo.TotalInitTime?.TotalSeconds;
			this.TotalTimeToComplete = (float?)TimingInfo.TotalTimeToComplete?.TotalSeconds;

			this.AverageTotalWaitTime = (float?)TimingInfo.AverageTotalWaitTime?.TotalSeconds;
			this.AverageTotalInitTime = (float?)TimingInfo.AverageTotalInitTime?.TotalSeconds;
			this.AverageTotalTimeToComplete = (float?)TimingInfo.AverageTotalTimeToComplete?.TotalSeconds;
		}
	}

	/// <summary>
	/// Information about the timing info for a particular target
	/// </summary>
	public class GetStepTimingInfoResponse : GetTimingInfoResponse
	{
		/// <summary>
		/// Name of this node
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// Average wait time for this step
		/// </summary>
		public float? AverageStepWaitTime { get; set; }

		/// <summary>
		/// Average init time for this step
		/// </summary>
		public float? AverageStepInitTime { get; set; }

		/// <summary>
		/// Average duration for this step
		/// </summary>
		public float? AverageStepDuration { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of the node</param>
		/// <param name="JobTimingInfo">Timing info to construct from</param>
		public GetStepTimingInfoResponse(string? Name, TimingInfo JobTimingInfo)
			: base(JobTimingInfo)
		{
			this.Name = Name;
			this.AverageStepWaitTime = JobTimingInfo.StepTiming?.AverageWaitTime;
			this.AverageStepInitTime = JobTimingInfo.StepTiming?.AverageInitTime;
			this.AverageStepDuration = JobTimingInfo.StepTiming?.AverageDuration;
		}
	}

	/// <summary>
	/// Information about the timing info for a label
	/// </summary>
	public class GetLabelTimingInfoResponse : GetTimingInfoResponse
	{
		/// <summary>
		/// Name of the label
		/// </summary>
		[Obsolete("Use DashboardName instead")]
		public string? Name => DashboardName;

		/// <summary>
		/// Category for the label
		/// </summary>
		[Obsolete("Use DashboardCategory instead")]
		public string? Category => DashboardCategory;

		/// <summary>
		/// Name of the label
		/// </summary>
		public string? DashboardName { get; set; }

		/// <summary>
		/// Category for the label
		/// </summary>
		public string? DashboardCategory { get; set; }

		/// <summary>
		/// Name of the label
		/// </summary>
		public string? UgsName { get; set; }

		/// <summary>
		/// Category for the label
		/// </summary>
		public string? UgsProject { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Label">The label to construct from</param>
		/// <param name="TimingInfo">Timing info to construct from</param>
		public GetLabelTimingInfoResponse(ILabel Label, TimingInfo TimingInfo)
			: base(TimingInfo)
		{
			this.DashboardName = Label.DashboardName;
			this.DashboardCategory = Label.DashboardCategory;
			this.UgsName = Label.UgsName;
			this.UgsProject = Label.UgsProject;
		}
	}

	/// <summary>
	/// Describes the history of a step
	/// </summary>
	public class GetJobStepRefResponse
	{
		/// <summary>
		/// The job id
		/// </summary>
		public string JobId { get; set; }

		/// <summary>
		/// The batch containing the step
		/// </summary>
		public string BatchId { get; set; }

		/// <summary>
		/// The step identifier
		/// </summary>
		public string StepId { get; set; }

		/// <summary>
		/// The change number being built
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// The step log id
		/// </summary>
		public string? LogId { get; set; }

		/// <summary>
		/// The pool id
		/// </summary>
		public string? PoolId { get; set; }

		/// <summary>
		/// The agent id
		/// </summary>
		public string? AgentId { get; set; }

		/// <summary>
		/// Outcome of the step, once complete.
		/// </summary>
		public JobStepOutcome? Outcome { get; set; }

		/// <summary>
		/// Time at which the step started.
		/// </summary>
		public DateTimeOffset StartTime { get; }

		/// <summary>
		/// Time at which the step finished.
		/// </summary>
		public DateTimeOffset? FinishTime { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="JobStepRef">The jobstep ref to construct from</param>
		public GetJobStepRefResponse(IJobStepRef JobStepRef)
		{
			this.JobId = JobStepRef.Id.JobId.ToString();
			this.BatchId = JobStepRef.Id.BatchId.ToString();
			this.StepId = JobStepRef.Id.StepId.ToString();
			this.Change = JobStepRef.Change;
			this.LogId = JobStepRef.LogId.ToString();
			this.PoolId = JobStepRef.PoolId?.ToString();
			this.AgentId = JobStepRef.AgentId?.ToString();
			this.Outcome = JobStepRef.Outcome;
			this.StartTime = JobStepRef.StartTimeUtc;
			this.FinishTime = JobStepRef.FinishTimeUtc;
		}
	}
}
