// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	using ReportPlacement = HordeCommon.Rpc.ReportPlacement;
	using JobId = ObjectId<IJob>;
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;
	using PoolId = StringId<IPool>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Report for a job or jobstep
	/// </summary>
	public interface IReport
	{
		/// <summary>
		/// Name of the report
		/// </summary>
		string Name { get; }

		/// <summary>
		/// Where to render the report
		/// </summary>
		ReportPlacement Placement { get; }

		/// <summary>
		/// The artifact id
		/// </summary>
		ObjectId ArtifactId { get; }
	}

	/// <summary>
	/// Implementation of IReport
	/// </summary>
	public class Report : IReport
	{
		/// <inheritdoc/>
		public string Name { get; set; } = String.Empty;

		/// <inheritdoc/>
		public ReportPlacement Placement { get; set; }

		/// <inheritdoc/>
		public ObjectId ArtifactId { get; set; }
	}

	/// <summary>
	/// Embedded jobstep document
	/// </summary>
	public interface IJobStep
	{
		/// <summary>
		/// Unique ID assigned to this jobstep. A new id is generated whenever a jobstep's order is changed.
		/// </summary>
		public SubResourceId Id { get; }

		/// <summary>
		/// Index of the node which this jobstep is to execute
		/// </summary>
		public int NodeIdx { get; }

		/// <summary>
		/// Current state of the job step. This is updated automatically when runs complete.
		/// </summary>
		public JobStepState State { get; }

		/// <summary>
		/// Current outcome of the jobstep
		/// </summary>
		public JobStepOutcome Outcome { get; }

		/// <summary>
		/// The log id for this step
		/// </summary>
		public LogId? LogId { get; }

		/// <summary>
		/// Unique id for notifications
		/// </summary>
		public ObjectId? NotificationTriggerId { get; }

		/// <summary>
		/// Time at which the batch transitioned to the ready state (UTC).
		/// </summary>
		public DateTime? ReadyTimeUtc { get; }

		/// <summary>
		/// Time at which the batch transitioned to the executing state (UTC).
		/// </summary>
		public DateTime? StartTimeUtc { get; }

		/// <summary>
		/// Time at which the run finished (UTC)
		/// </summary>
		public DateTime? FinishTimeUtc { get; }

		/// <summary>
		/// Override for the priority of this step
		/// </summary>
		public Priority? Priority { get; }

		/// <summary>
		/// If a retry is requested, stores the name of the user that requested it
		/// </summary>
		public UserId? RetriedByUserId { get; }
		
		/// <summary>
		/// Signal if a step should be aborted
		/// </summary>
		public bool AbortRequested { get; }
		
		/// <summary>
		/// If an abort is requested, stores the id of the user that requested it
		/// </summary>
		public UserId? AbortedByUserId { get; }

		/// <summary>
		/// List of reports for this step
		/// </summary>
		public IReadOnlyList<IReport>? Reports { get; }

		/// <summary>
		/// Reports for this jobstep.
		/// </summary>
		public Dictionary<string, string>? Properties { get; }
	}

	/// <summary>
	/// Extension methods for job steps
	/// </summary>
	public static class JobStepExtensions
	{
		/// <summary>
		/// Determines if a jobstep has failed or is skipped. Can be used to determine whether dependent steps will be able to run.
		/// </summary>
		/// <returns>True if the step is failed or skipped</returns>
		public static bool IsFailedOrSkipped(this IJobStep Step)
		{
			return Step.State == JobStepState.Skipped || Step.Outcome == JobStepOutcome.Failure;
		}

		/// <summary>
		/// Determines if a jobstep is done by checking to see if it is completed, skipped, or aborted.
		/// </summary>
		/// <returns>True if the step is completed, skipped, or aborted</returns>
		public static bool IsPending(this IJobStep Step)
		{
			return Step.State != JobStepState.Aborted && Step.State != JobStepState.Completed && Step.State != JobStepState.Skipped;
		}
	}

	/// <summary>
	/// Stores information about a batch of job steps
	/// </summary>
	public interface IJobStepBatch
	{
		/// <summary>
		/// Unique id for this group
		/// </summary>
		public SubResourceId Id { get; }

		/// <summary>
		/// The log file id for this batch
		/// </summary>
		public LogId? LogId { get; }

		/// <summary>
		/// Index of the group being executed
		/// </summary>
		public int GroupIdx { get; }

		/// <summary>
		/// The state of this group
		/// </summary>
		public JobStepBatchState State { get; }

		/// <summary>
		/// Error associated with this group
		/// </summary>
		public JobStepBatchError Error { get; }

		/// <summary>
		/// Steps within this run
		/// </summary>
		public IReadOnlyList<IJobStep> Steps { get; }

		/// <summary>
		/// The pool that this agent was taken from
		/// </summary>
		public PoolId? PoolId { get; }

		/// <summary>
		/// The agent assigned to execute this group
		/// </summary>
		public AgentId? AgentId { get; }

		/// <summary>
		/// The agent session that is executing this group
		/// </summary>
		public ObjectId? SessionId { get; }

		/// <summary>
		/// The lease that's executing this group
		/// </summary>
		public LeaseId? LeaseId { get; }

		/// <summary>
		/// The weighted priority of this batch for the scheduler
		/// </summary>
		public int SchedulePriority { get; }

		/// <summary>
		/// Time at which the group became ready (UTC).
		/// </summary>
		public DateTime? ReadyTimeUtc { get; }

		/// <summary>
		/// Time at which the group started (UTC).
		/// </summary>
		public DateTime? StartTimeUtc { get; }

		/// <summary>
		/// Time at which the group finished (UTC)
		/// </summary>
		public DateTime? FinishTimeUtc { get; }
	}

	/// <summary>
	/// Extension methods for IJobStepBatch
	/// </summary>
	public static class JobStepBatchExtensions
	{
		/// <summary>
		/// Attempts to get a step with the given id
		/// </summary>
		/// <param name="Batch">The batch to search</param>
		/// <param name="StepId">The step id</param>
		/// <param name="Step">On success, receives the step object</param>
		/// <returns>True if the step was found</returns>
		public static bool TryGetStep(this IJobStepBatch Batch, SubResourceId StepId, [NotNullWhen(true)] out IJobStep? Step)
		{
			Step = Batch.Steps.FirstOrDefault(x => x.Id == StepId);
			return Step != null;
		}

		/// <summary>
		/// Determines if new steps can be appended to this batch. We do not allow this after the last step has been completed, because the agent is shutting down.
		/// </summary>
		/// <param name="Batch">The batch to search</param>
		/// <returns>True if new steps can be appended to this batch</returns>
		public static bool CanBeAppendedTo(this IJobStepBatch Batch)
		{
			return Batch.State <= JobStepBatchState.Running;
		}

		/// <summary>
		/// Gets the wait time for this batch
		/// </summary>
		/// <param name="Batch">The batch to search</param>
		/// <returns>Wait time for the batch</returns>
		public static TimeSpan? GetWaitTime(this IJobStepBatch Batch)
		{
			if (Batch.StartTimeUtc == null || Batch.ReadyTimeUtc == null)
			{
				return null;
			}
			else
			{
				return Batch.StartTimeUtc.Value - Batch.ReadyTimeUtc.Value;
			}
		}

		/// <summary>
		/// Gets the initialization time for this batch
		/// </summary>
		/// <param name="Batch">The batch to search</param>
		/// <returns>Initialization time for this batch</returns>
		public static TimeSpan? GetInitTime(this IJobStepBatch Batch)
		{
			if (Batch.StartTimeUtc != null)
			{
				foreach (IJobStep Step in Batch.Steps)
				{
					if (Step.StartTimeUtc != null)
					{
						return Step.StartTimeUtc - Batch.StartTimeUtc.Value;
					}
				}
			}
			return null;
		}

		/// <summary>
		/// Get the dependencies required for this batch to start, taking run-early nodes into account
		/// </summary>
		/// <param name="Batch">The batch to search</param>
		/// <param name="Groups">List of node groups</param>
		/// <returns>Set of nodes that must have completed for this batch to start</returns>
		public static HashSet<INode> GetStartDependencies(this IJobStepBatch Batch, IReadOnlyList<INodeGroup> Groups)
		{
			// Find all the nodes that this group will start with.
			List<INode> Nodes = Batch.Steps.ConvertAll(x => Groups[Batch.GroupIdx].Nodes[x.NodeIdx]);
			if (Nodes.Any(x => x.RunEarly))
			{
				Nodes.RemoveAll(x => !x.RunEarly);
			}

			// Find all their dependencies
			HashSet<INode> Dependencies = new HashSet<INode>();
			foreach (INode Node in Nodes)
			{
				Dependencies.UnionWith(Node.InputDependencies.Select(x => Groups[x.GroupIdx].Nodes[x.NodeIdx]));
				Dependencies.UnionWith(Node.OrderDependencies.Select(x => Groups[x.GroupIdx].Nodes[x.NodeIdx]));
			}

			// Exclude all the dependencies within the same group
			Dependencies.ExceptWith(Groups[Batch.GroupIdx].Nodes);
			return Dependencies;
		}
	}

	/// <summary>
	/// Cumulative timing information to reach a certain point in a job
	/// </summary>
	public class TimingInfo
	{
		/// <summary>
		/// Wait time on the critical path
		/// </summary>
		public TimeSpan? TotalWaitTime { get; set; }

		/// <summary>
		/// Sync time on the critical path
		/// </summary>
		public TimeSpan? TotalInitTime { get; set; }

		/// <summary>
		/// Duration to this point
		/// </summary>
		public TimeSpan? TotalTimeToComplete { get; set; }

		/// <summary>
		/// Average wait time to this point
		/// </summary>
		public TimeSpan? AverageTotalWaitTime { get; set; }

		/// <summary>
		/// Average sync time to this point
		/// </summary>
		public TimeSpan? AverageTotalInitTime { get; set; }

		/// <summary>
		/// Average duration to this point
		/// </summary>
		public TimeSpan? AverageTotalTimeToComplete { get; set; }

		/// <summary>
		/// Individual step timing information
		/// </summary>
		public IJobStepTiming? StepTiming { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public TimingInfo()
		{
			TotalWaitTime = TimeSpan.Zero;
			TotalInitTime = TimeSpan.Zero;
			TotalTimeToComplete = TimeSpan.Zero;

			AverageTotalWaitTime = TimeSpan.Zero;
			AverageTotalInitTime = TimeSpan.Zero;
			AverageTotalTimeToComplete = TimeSpan.Zero;
		}

		/// <summary>
		/// Copy constructor
		/// </summary>
		/// <param name="Other">The timing info object to copy from</param>
		public TimingInfo(TimingInfo Other)
		{
			TotalWaitTime = Other.TotalWaitTime;
			TotalInitTime = Other.TotalInitTime;
			TotalTimeToComplete = Other.TotalTimeToComplete;

			AverageTotalWaitTime = Other.AverageTotalWaitTime;
			AverageTotalInitTime = Other.AverageTotalInitTime;
			AverageTotalTimeToComplete = Other.AverageTotalTimeToComplete;
		}

		/// <summary>
		/// Modifies this timing to wait for another timing
		/// </summary>
		/// <param name="Other">The other node to wait for</param>
		public void WaitFor(TimingInfo Other)
		{
			if (TotalTimeToComplete != null)
			{
				if (Other.TotalTimeToComplete == null || Other.TotalTimeToComplete.Value > TotalTimeToComplete.Value)
				{
					TotalInitTime = Other.TotalInitTime;
					TotalWaitTime = Other.TotalWaitTime;
					TotalTimeToComplete = Other.TotalTimeToComplete;
				}
			}

			if (AverageTotalTimeToComplete != null)
			{
				if (Other.AverageTotalTimeToComplete == null || Other.AverageTotalTimeToComplete.Value > AverageTotalTimeToComplete.Value)
				{
					AverageTotalInitTime = Other.AverageTotalInitTime;
					AverageTotalWaitTime = Other.AverageTotalWaitTime;
					AverageTotalTimeToComplete = Other.AverageTotalTimeToComplete;
				}
			}
		}

		/// <summary>
		/// Waits for all the given timing info objects to complete
		/// </summary>
		/// <param name="Others">Other timing info objects to wait for</param>
		public void WaitForAll(IEnumerable<TimingInfo> Others)
		{
			foreach(TimingInfo Other in Others)
			{
				WaitFor(Other);
			}
		}

		/// <summary>
		/// Constructs a new TimingInfo object which represents the last TimingInfo to finish
		/// </summary>
		/// <param name="Others">TimingInfo objects to wait for</param>
		/// <returns>New TimingInfo instance</returns>
		public static TimingInfo Max(IEnumerable<TimingInfo> Others)
		{
			TimingInfo TimingInfo = new TimingInfo();
			TimingInfo.WaitForAll(Others);
			return TimingInfo;
		}
	}

	/// <summary>
	/// Information about a chained job trigger
	/// </summary>
	public interface IChainedJob
	{
		/// <summary>
		/// The target to monitor
		/// </summary>
		public string Target { get; }

		/// <summary>
		/// The template to trigger on success
		/// </summary>
		public TemplateRefId TemplateRefId { get; }

		/// <summary>
		/// The triggered job id
		/// </summary>
		public JobId? JobId { get; }
	}

	/// <summary>
	/// Document describing a job
	/// </summary>
	[SuppressMessage("Compiler", "CA1056:URI parameters should not be strings")]
	public interface IJob
	{
		/// <summary>
		/// Job argument indicating a target that should be built
		/// </summary>
		public const string TargetArgumentPrefix = "-Target=";

		/// <summary>
		/// Name of the node which parses the buildgraph script
		/// </summary>
		public const string SetupNodeName = "Setup Build";

		/// <summary>
		/// Identifier for the job. Randomly generated.
		/// </summary>
		public JobId Id { get; }

		/// <summary>
		/// The stream that this job belongs to
		/// </summary>
		public StreamId StreamId { get; }

		/// <summary>
		/// The template ref id
		/// </summary>
		public TemplateRefId TemplateId { get; }

		/// <summary>
		/// The template that this job was created from
		/// </summary>
		public ContentHash? TemplateHash { get; }

		/// <summary>
		/// Hash of the graph definition
		/// </summary>
		public ContentHash GraphHash { get; }

		/// <summary>
		/// Id of the user that started this job
		/// </summary>
		public UserId? StartedByUserId { get; }

		/// <summary>
		/// Id of the user that aborted this job. Set to null if the job is not aborted.
		/// </summary>
		public UserId? AbortedByUserId { get; }

		/// <summary>
		/// Name of the job.
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// The changelist number to build
		/// </summary>
		public int Change { get; }

		/// <summary>
		/// The code changelist number for this build
		/// </summary>
		public int CodeChange { get; }

		/// <summary>
		/// The preflight changelist number
		/// </summary>
		public int PreflightChange { get; }

		/// <summary>
		/// The cloned preflight changelist number (if the prefight change is duplicated via p4 reshelve)
		/// </summary>
		public int ClonedPreflightChange { get; }

		/// <summary>
		/// Priority of this job
		/// </summary>
		public Priority Priority { get; }

		/// <summary>
		/// For preflights, submit the change if the job is successful
		/// </summary>
		public bool AutoSubmit { get; }

		/// <summary>
		/// The submitted changelist number
		/// </summary>
		public int? AutoSubmitChange { get; }

		/// <summary>
		/// Message produced by trying to auto-submit the change
		/// </summary>
		public string? AutoSubmitMessage { get; }

		/// <summary>
		/// Whether to update issues based on the outcome of this job
		/// </summary>
		public bool UpdateIssues { get; }

		/// <summary>
		/// Time that the job was created (in UTC)
		/// </summary>
		public DateTime CreateTimeUtc { get; }

		/// <summary>
		/// Largest value of the CombinedPriority value for batches in the ready state.
		/// </summary>
		public int SchedulePriority { get; }

		/// <summary>
		/// Array of jobstep runs
		/// </summary>
		public IReadOnlyList<IJobStepBatch> Batches { get; }

		/// <summary>
		/// Optional user-defined properties for this job
		/// </summary>
		public IReadOnlyList<string> Arguments { get; }

		/// <summary>
		/// Issues associated with this job
		/// </summary>
		public IReadOnlyList<int> Issues { get; }

		/// <summary>
		/// Unique id for notifications
		/// </summary>
		public ObjectId? NotificationTriggerId { get; }

		/// <summary>
		/// Whether to show badges in UGS for this job
		/// </summary>
		public bool ShowUgsBadges { get; }

		/// <summary>
		/// Whether to show alerts in UGS for this job
		/// </summary>
		public bool ShowUgsAlerts { get; }

		/// <summary>
		/// Notification channel for this job.
		/// </summary>
		public string? NotificationChannel { get; }
		
		/// <summary>
		/// Notification channel filter for this job.
		/// </summary>
		public string? NotificationChannelFilter { get; }
		
		/// <summary>
		/// Mapping of label ids to notification trigger ids for notifications
		/// </summary>
		public IReadOnlyDictionary<int, ObjectId> LabelIdxToTriggerId { get; }

		/// <summary>
		/// List of reports for this step
		/// </summary>
		public IReadOnlyList<IReport>? Reports { get; }

		/// <summary>
		/// List of downstream job triggers
		/// </summary>
		public IReadOnlyList<IChainedJob> ChainedJobs { get; }

		/// <summary>
		/// Next id for batches or groups
		/// </summary>
		public SubResourceId NextSubResourceId { get; }

		/// <summary>
		/// The last update time
		/// </summary>
		public DateTime UpdateTimeUtc { get; }

		/// <summary>
		/// The ACL for this job.
		/// </summary>
		public Acl? Acl { get; }

		/// <summary>
		/// Update counter for this document. Any updates should compare-and-swap based on the value of this counter, or increment it in the case of server-side updates.
		/// </summary>
		public int UpdateIndex { get; }
	}

	/// <summary>
	/// Extension methods for jobs
	/// </summary>
	public static class JobExtensions
	{
		/// <summary>
		/// Attempts to get a step with a given ID
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <param name="StepId">The step id</param>
		/// <param name="Step">On success, receives the step object</param>
		/// <returns>True if the step was found</returns>
		public static bool TryGetStep(this IJob Job, SubResourceId StepId, [NotNullWhen(true)] out IJobStep? Step)
		{
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				if (Batch.TryGetStep(StepId, out Step))
				{
					return true;
				}
			}

			Step = null;
			return false;
		}

		/// <summary>
		/// Gets the current job state
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <returns>Job state</returns>
		public static JobState GetState(this IJob Job)
		{
			bool bWaiting = false;
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				foreach (IJobStep Step in Batch.Steps)
				{
					if (Step.State == JobStepState.Running)
					{
						return JobState.Running;
					}
					else if (Step.State == JobStepState.Ready || Step.State == JobStepState.Waiting)
					{
						if (Batch.State == JobStepBatchState.Starting || Batch.State == JobStepBatchState.Running)
						{
							return JobState.Running;
						}
						else
						{
							bWaiting = true;
						}
					}
				}
			}
			return bWaiting ? JobState.Waiting : JobState.Complete;
		}

		/// <summary>
		/// Gets the outcome for a particular named target. May be an aggregate or node name.
		/// </summary>
		/// <param name="Job">The job to check</param>
		/// <returns>The step outcome</returns>
		public static (JobStepState, JobStepOutcome) GetTargetState(this IJob Job)
		{
			IReadOnlyDictionary<NodeRef, IJobStep> NodeToStep = GetStepForNodeMap(Job);
			return GetTargetState(NodeToStep.Values);
		}

		/// <summary>
		/// Gets the outcome for a particular named target. May be an aggregate or node name.
		/// </summary>
		/// <param name="Job">The job to check</param>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="Target">Target to find an outcome for</param>
		/// <returns>The step outcome</returns>
		public static (JobStepState, JobStepOutcome)? GetTargetState(this IJob Job, IGraph Graph, string? Target)
		{
			if (Target == null)
			{
				return GetTargetState(Job);
			}

			NodeRef NodeRef;
			if (Graph.TryFindNode(Target, out NodeRef))
			{
				IJobStep? Step;
				if (Job.TryGetStepForNode(NodeRef, out Step))
				{
					return (Step.State, Step.Outcome);
				}
				else
				{
					return null;
				}
			}

			IAggregate? Aggregate;
			if (Graph.TryFindAggregate(Target, out Aggregate))
			{
				IReadOnlyDictionary<NodeRef, IJobStep> StepForNode = GetStepForNodeMap(Job);

				List<IJobStep> Steps = new List<IJobStep>();
				foreach (NodeRef AggregateNodeRef in Aggregate.Nodes)
				{
					IJobStep? Step;
					if (!StepForNode.TryGetValue(AggregateNodeRef, out Step))
					{
						return null;
					}
					Steps.Add(Step);
				}

				return GetTargetState(Steps);
			}

			return null;
		}

		/// <summary>
		/// Gets the outcome for a particular named target. May be an aggregate or node name.
		/// </summary>
		/// <param name="Steps">Steps to include</param>
		/// <returns>The step outcome</returns>
		public static (JobStepState, JobStepOutcome) GetTargetState(IEnumerable<IJobStep> Steps)
		{
			bool bAnySkipped = false;
			bool bAnyWarnings = false;
			bool bAnyFailed = false;
			bool bAnyPending = false;
			foreach (IJobStep Step in Steps)
			{
				bAnyPending |= Step.IsPending();
				bAnySkipped |= Step.State == JobStepState.Aborted || Step.State == JobStepState.Skipped;
				bAnyFailed |= (Step.Outcome == JobStepOutcome.Failure);
				bAnyWarnings |= (Step.Outcome == JobStepOutcome.Warnings);
			}

			JobStepState NewState = bAnyPending ? JobStepState.Running : JobStepState.Completed;
			JobStepOutcome NewOutcome = bAnyFailed ? JobStepOutcome.Failure : bAnyWarnings ? JobStepOutcome.Warnings : bAnySkipped ? JobStepOutcome.Unspecified : JobStepOutcome.Success;
			return (NewState, NewOutcome);
		}

		/// <summary>
		/// Gets the outcome for a particular named target. May be an aggregate or node name.
		/// </summary>
		/// <param name="Job">The job to check</param>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="Target">Target to find an outcome for</param>
		/// <returns>The step outcome</returns>
		public static JobStepOutcome GetTargetOutcome(this IJob Job, IGraph Graph, string Target)
		{
			NodeRef NodeRef;
			if (Graph.TryFindNode(Target, out NodeRef))
			{
				IJobStep? Step;
				if (Job.TryGetStepForNode(NodeRef, out Step))
				{
					return Step.Outcome;
				}
				else
				{
					return JobStepOutcome.Unspecified;
				}
			}

			IAggregate? Aggregate;
			if (Graph.TryFindAggregate(Target, out Aggregate))
			{
				IReadOnlyDictionary<NodeRef, IJobStep> StepForNode = GetStepForNodeMap(Job);

				bool Warnings = false;
				foreach (NodeRef AggregateNodeRef in Aggregate.Nodes)
				{
					IJobStep? Step;
					if (!StepForNode.TryGetValue(AggregateNodeRef, out Step))
					{
						return JobStepOutcome.Unspecified;
					}
					if (Step.Outcome == JobStepOutcome.Failure)
					{
						return JobStepOutcome.Failure;
					}
					Warnings |= (Step.Outcome == JobStepOutcome.Warnings);
				}
				return Warnings ? JobStepOutcome.Warnings : JobStepOutcome.Success;
			}

			return JobStepOutcome.Unspecified;
		}

		/// <summary>
		/// Gets the job step for a particular node
		/// </summary>
		/// <param name="Job">The job to search</param>
		/// <param name="NodeRef">The node ref</param>
		/// <param name="JobStep">Receives the jobstep on success</param>
		/// <returns>True if the jobstep was founds</returns>
		public static bool TryGetStepForNode(this IJob Job, NodeRef NodeRef, [NotNullWhen(true)] out IJobStep? JobStep)
		{
			JobStep = null;
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				if (Batch.GroupIdx == NodeRef.GroupIdx)
				{
					foreach (IJobStep BatchStep in Batch.Steps)
					{
						if (BatchStep.NodeIdx == NodeRef.NodeIdx)
						{
							JobStep = BatchStep;
						}
					}
				}
			}
			return JobStep != null;
		}

		/// <summary>
		/// Gets a dictionary that maps <see cref="NodeRef"/> objects to their associated
		/// <see cref="IJobStep"/> objects on a <see cref="IJob"/>.
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <returns>Map of <see cref="NodeRef"/> to <see cref="IJobStep"/></returns>
		public static IReadOnlyDictionary<NodeRef, IJobStep> GetStepForNodeMap(this IJob Job)
		{
			Dictionary<NodeRef, IJobStep> StepForNode = new Dictionary<NodeRef, IJobStep>();
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				foreach (IJobStep BatchStep in Batch.Steps)
				{
					NodeRef BatchNodeRef = new NodeRef(Batch.GroupIdx, BatchStep.NodeIdx);
					StepForNode[BatchNodeRef] = BatchStep;
				}
			}
			return StepForNode;
		}

		/// <summary>
		/// Gets the estimated timing info for all nodes in the job
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <param name="Graph">Graph for this job</param>
		/// <param name="JobTiming">Job timing information</param>
		/// <returns>Map of node to expected timing info</returns>
		public static Dictionary<INode, TimingInfo> GetTimingInfo(this IJob Job, IGraph Graph, IJobTiming JobTiming)
		{
			TimeSpan CurrentTime = DateTime.UtcNow - Job.CreateTimeUtc;

			Dictionary<INode, TimingInfo> NodeToTimingInfo = Graph.Groups.SelectMany(x => x.Nodes).ToDictionary(x => x, x => new TimingInfo());
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				INodeGroup Group = Graph.Groups[Batch.GroupIdx];

				// Step through the batch, keeping track of the time that things finish.
				TimingInfo TimingInfo = new TimingInfo();

				// Wait for the dependencies for the batch to start
				HashSet<INode> DependencyNodes = Batch.GetStartDependencies(Graph.Groups);
				TimingInfo.WaitForAll(DependencyNodes.Select(x => NodeToTimingInfo[x]));

				// If the batch has actually started, correct the expected time to use this instead
				if (Batch.StartTimeUtc != null)
				{
					TimingInfo.TotalTimeToComplete = Batch.StartTimeUtc - Job.CreateTimeUtc;
				}

				// Get the average times for this batch
				TimeSpan? AverageWaitTime = GetAverageWaitTime(Graph, Batch, JobTiming);
				TimeSpan? AverageInitTime = GetAverageInitTime(Graph, Batch, JobTiming);

				// Update the wait times and initialization times along this path
				TimingInfo.TotalWaitTime = TimingInfo.TotalWaitTime + (Batch.GetWaitTime() ?? AverageWaitTime);
				TimingInfo.TotalInitTime = TimingInfo.TotalInitTime + (Batch.GetInitTime() ?? AverageInitTime);

				// Update the average wait and initialization times too
				TimingInfo.AverageTotalWaitTime = TimingInfo.AverageTotalWaitTime + AverageWaitTime;
				TimingInfo.AverageTotalInitTime = TimingInfo.AverageTotalInitTime + AverageInitTime;

				// Step through the batch, updating the expected times as we go
				foreach (IJobStep Step in Batch.Steps)
				{
					INode Node = Group.Nodes[Step.NodeIdx];

					// Get the timing for this step
					IJobStepTiming? StepTimingInfo;
					JobTiming.TryGetStepTiming(Node.Name, out StepTimingInfo);

					// If the step has already started, update the actual time to reach this point
					if(Step.StartTimeUtc != null)
					{
						TimingInfo.TotalTimeToComplete = Step.StartTimeUtc.Value - Job.CreateTimeUtc;
					}

					// If the step hasn't started yet, make sure the start time is later than the current time
					if (Step.StartTimeUtc == null && CurrentTime > TimingInfo.TotalTimeToComplete)
					{
						TimingInfo.TotalTimeToComplete = CurrentTime;
					}

					// Wait for all the node dependencies to complete
					TimingInfo.WaitForAll(Graph.GetDependencies(Node).Select(x => NodeToTimingInfo[x]));

					// If the step has actually finished, correct the time to use that instead
					if (Step.FinishTimeUtc != null)
					{
						TimingInfo.TotalTimeToComplete = Step.FinishTimeUtc.Value - Job.CreateTimeUtc;
					}
					else
					{
						TimingInfo.TotalTimeToComplete = TimingInfo.TotalTimeToComplete + NullableTimeSpanFromSeconds(StepTimingInfo?.AverageDuration);
					}

					// If the step hasn't finished yet, make sure the start time is later than the current time
					if (Step.FinishTimeUtc == null && CurrentTime > TimingInfo.TotalTimeToComplete)
					{
						TimingInfo.TotalTimeToComplete = CurrentTime;
					}

					// Update the average time to complete
					TimingInfo.AverageTotalTimeToComplete = TimingInfo.AverageTotalTimeToComplete + NullableTimeSpanFromSeconds(StepTimingInfo?.AverageDuration);

					// Add it to the lookup
					TimingInfo NodeTimingInfo = new TimingInfo(TimingInfo);
					NodeTimingInfo.StepTiming = StepTimingInfo;
					NodeToTimingInfo[Node] = NodeTimingInfo;
				}
			}
			return NodeToTimingInfo;
		}

		/// <summary>
		/// Gets the average wait time for this batch
		/// </summary>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="Batch">The batch to get timing info for</param>
		/// <param name="JobTiming">The job timing information</param>
		/// <returns>Wait time for the batch</returns>
		public static TimeSpan? GetAverageWaitTime(IGraph Graph, IJobStepBatch Batch, IJobTiming JobTiming)
		{
			TimeSpan? WaitTime = null;
			foreach (IJobStep Step in Batch.Steps)
			{
				INode Node = Graph.Groups[Batch.GroupIdx].Nodes[Step.NodeIdx];
				if(JobTiming.TryGetStepTiming(Node.Name, out IJobStepTiming? TimingInfo))
				{
					if (TimingInfo.AverageWaitTime != null)
					{
						TimeSpan StepWaitTime = TimeSpan.FromSeconds(TimingInfo.AverageWaitTime.Value);
						if (WaitTime == null || StepWaitTime > WaitTime.Value)
						{
							WaitTime = StepWaitTime;
						}
					}
				}
			}
			return WaitTime;
		}

		/// <summary>
		/// Gets the average initialization time for this batch
		/// </summary>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="Batch">The batch to get timing info for</param>
		/// <param name="JobTiming">The job timing information</param>
		/// <returns>Initialization time for this batch</returns>
		public static TimeSpan? GetAverageInitTime(IGraph Graph, IJobStepBatch Batch, IJobTiming JobTiming)
		{
			TimeSpan? InitTime = null;
			foreach (IJobStep Step in Batch.Steps)
			{
				INode Node = Graph.Groups[Batch.GroupIdx].Nodes[Step.NodeIdx];
				if (JobTiming.TryGetStepTiming(Node.Name, out IJobStepTiming? TimingInfo))
				{
					if (TimingInfo.AverageInitTime != null)
					{
						TimeSpan StepInitTime = TimeSpan.FromSeconds(TimingInfo.AverageInitTime.Value);
						if (InitTime == null || StepInitTime > InitTime.Value)
						{
							InitTime = StepInitTime;
						}
					}
				}
			}
			return InitTime;
		}

		/// <summary>
		/// Creates a nullable timespan from a nullable number of seconds
		/// </summary>
		/// <param name="Seconds">The number of seconds to construct from</param>
		/// <returns>TimeSpan object</returns>
		static TimeSpan? NullableTimeSpanFromSeconds(float? Seconds)
		{
			if (Seconds == null)
			{
				return null;
			}
			else
			{
				return TimeSpan.FromSeconds(Seconds.Value);
			}
		}

		/// <summary>
		/// Attempts to get a batch with the given id
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <param name="BatchId">The batch id</param>
		/// <param name="Batch">On success, receives the batch object</param>
		/// <returns>True if the batch was found</returns>
		public static bool TryGetBatch(this IJob Job, SubResourceId BatchId, [NotNullWhen(true)] out IJobStepBatch? Batch)
		{
			Batch = Job.Batches.FirstOrDefault(x => x.Id == BatchId);
			return Batch != null;
		}

		/// <summary>
		/// Attempts to get a batch with the given id
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <param name="BatchId">The batch id</param>
		/// <param name="StepId">The step id</param>
		/// <param name="Step">On success, receives the step object</param>
		/// <returns>True if the batch was found</returns>
		public static bool TryGetStep(this IJob Job, SubResourceId BatchId, SubResourceId StepId, [NotNullWhen(true)] out IJobStep? Step)
		{
			IJobStepBatch? Batch;
			if (!TryGetBatch(Job, BatchId, out Batch))
			{
				Step = null;
				return false;
			}
			return Batch.TryGetStep(StepId, out Step);
		}

		/// <summary>
		/// Finds the set of nodes affected by a label
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <param name="Graph">Graph definition for the job</param>
		/// <param name="LabelIdx">Index of the label. -1 or Graph.Labels.Count are treated as referring to the default lable.</param>
		/// <returns>Set of nodes affected by the given label</returns>
		public static HashSet<NodeRef> GetNodesForLabel(this IJob Job, IGraph Graph, int LabelIdx)
		{
			if (LabelIdx != -1 && LabelIdx != Graph.Labels.Count)
			{
				// Return all the nodes included by the label
				return new HashSet<NodeRef>(Graph.Labels[LabelIdx].IncludedNodes);
			}
			else
			{
				// Set of nodes which are not covered by an existing label, initially containing everything
				HashSet<NodeRef> UnlabeledNodes = new HashSet<NodeRef>();
				for (int GroupIdx = 0; GroupIdx < Graph.Groups.Count; GroupIdx++)
				{
					INodeGroup Group = Graph.Groups[GroupIdx];
					for (int NodeIdx = 0; NodeIdx < Group.Nodes.Count; NodeIdx++)
					{
						UnlabeledNodes.Add(new NodeRef(GroupIdx, NodeIdx));
					}
				}

				// Remove all the nodes that are part of an active label
				IReadOnlyDictionary<NodeRef, IJobStep> StepForNode = Job.GetStepForNodeMap();
				foreach (ILabel Label in Graph.Labels)
				{
					if (Label.RequiredNodes.Any(x => StepForNode.ContainsKey(x)))
					{
						UnlabeledNodes.ExceptWith(Label.IncludedNodes);
					}
				}
				return UnlabeledNodes;
			}
		}

		/// <summary>
		/// Create a list of aggregate responses, combining the graph definitions with the state of the job
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <param name="Graph">Graph definition for the job</param>
		/// <param name="Responses">List to receive all the responses</param>
		/// <returns>The default label state</returns>
		public static GetDefaultLabelStateResponse? GetLabelStateResponses(this IJob Job, IGraph Graph, List<GetLabelStateResponse> Responses)
		{
			// Create a lookup from noderef to step information
			IReadOnlyDictionary<NodeRef, IJobStep> StepForNode = Job.GetStepForNodeMap();

			// Set of nodes which are not covered by an existing label, initially containing everything
			HashSet<NodeRef> UnlabeledNodes = new HashSet<NodeRef>();
			for (int GroupIdx = 0; GroupIdx < Graph.Groups.Count; GroupIdx++)
			{
				INodeGroup Group = Graph.Groups[GroupIdx];
				for (int NodeIdx = 0; NodeIdx < Group.Nodes.Count; NodeIdx++)
				{
					UnlabeledNodes.Add(new NodeRef(GroupIdx, NodeIdx));
				}
			}

			// Create the responses
			foreach (ILabel Label in Graph.Labels)
			{
				// Refresh the state for this label
				LabelState NewState = LabelState.Unspecified;
				foreach (NodeRef RequiredNodeRef in Label.RequiredNodes)
				{
					if (StepForNode.ContainsKey(RequiredNodeRef))
					{
						NewState = LabelState.Complete;
						break;
					}
				}

				// Refresh the outcome
				LabelOutcome NewOutcome = LabelOutcome.Success;
				if (NewState == LabelState.Complete)
				{
					GetLabelState(Label.IncludedNodes, StepForNode, out NewState, out NewOutcome);
					UnlabeledNodes.ExceptWith(Label.IncludedNodes);
				}

				// Create the response
				Responses.Add(new GetLabelStateResponse(NewState, NewOutcome));
			}

			// Remove all the nodes that don't have a step
			UnlabeledNodes.RemoveWhere(x => !StepForNode.ContainsKey(x));

			// Remove successful "setup build" nodes from the list
			if (Graph.Groups.Count > 1 && Graph.Groups[0].Nodes.Count > 0)
			{
				INode Node = Graph.Groups[0].Nodes[0];
				if (Node.Name == IJob.SetupNodeName)
				{
					NodeRef NodeRef = new NodeRef(0, 0);
					if (UnlabeledNodes.Contains(NodeRef))
					{
						IJobStep Step = StepForNode[NodeRef];
						if (Step.State == JobStepState.Completed && Step.Outcome == JobStepOutcome.Success && Responses.Count > 0)
						{
							UnlabeledNodes.Remove(NodeRef);
						}
					}
				}
			}

			// Add a response for everything not included elsewhere.
			GetLabelState(UnlabeledNodes, StepForNode, out LabelState OtherState, out LabelOutcome OtherOutcome);
			return new GetDefaultLabelStateResponse(OtherState, OtherOutcome, UnlabeledNodes.Select(x => Graph.GetNode(x).Name).ToList());
		}

		/// <summary>
		/// Get the states of all labels for this job
		/// </summary>
		/// <param name="Job">The job to get states for</param>
		/// <param name="Graph">The graph for this job</param>
		/// <returns>Collection of label states by label index</returns>
		public static IReadOnlyList<(LabelState, LabelOutcome)> GetLabelStates(this IJob Job, IGraph Graph)
		{
			IReadOnlyDictionary<NodeRef, IJobStep> StepForNodeRef = Job.GetStepForNodeMap();

			List<(LabelState, LabelOutcome)> States = new List<(LabelState, LabelOutcome)>();
			for (int Idx = 0; Idx < Graph.Labels.Count; Idx++)
			{
				ILabel Label = Graph.Labels[Idx];

				// Default the label to the unspecified state
				LabelState NewState = LabelState.Unspecified;
				LabelOutcome NewOutcome = LabelOutcome.Unspecified;

				// Check if the label should be included
				if (Label.RequiredNodes.Any(x => StepForNodeRef.ContainsKey(x)))
				{
					// Combine the state of the steps contributing towards this label
					bool bAnySkipped = false;
					bool bAnyWarnings = false;
					bool bAnyFailed = false;
					bool bAnyPending = false;
					foreach (NodeRef IncludedNode in Label.IncludedNodes)
					{
						IJobStep? Step;
						if (StepForNodeRef.TryGetValue(IncludedNode, out Step))
						{
							bAnyPending |= Step.IsPending();
							bAnySkipped |= Step.State == JobStepState.Aborted || Step.State == JobStepState.Skipped;
							bAnyFailed |= (Step.Outcome == JobStepOutcome.Failure);
							bAnyWarnings |= (Step.Outcome == JobStepOutcome.Warnings);
						}
					}

					// Figure out the overall label state
					NewState = bAnyPending ? LabelState.Running : LabelState.Complete;
					NewOutcome = bAnyFailed ? LabelOutcome.Failure : bAnyWarnings ? LabelOutcome.Warnings : bAnySkipped? LabelOutcome.Unspecified : LabelOutcome.Success;
				}

				States.Add((NewState, NewOutcome));
			}
			return States;	
		}

		/// <summary>
		/// Get the states of all UGS badges for this job
		/// </summary>
		/// <param name="Job">The job to get states for</param>
		/// <param name="Graph">The graph for this job</param>
		/// <returns>List of badge states</returns>
		public static Dictionary<int, UgsBadgeState> GetUgsBadgeStates(this IJob Job, IGraph Graph)
		{
			IReadOnlyList<(LabelState, LabelOutcome)> LabelStates = GetLabelStates(Job, Graph);
			return Job.GetUgsBadgeStates(Graph, LabelStates);
		}

		/// <summary>
		/// Get the states of all UGS badges for this job
		/// </summary>
		/// <param name="Job">The job to get states for</param>
		/// <param name="Graph">The graph for this job</param>
		/// <param name="LabelStates">The existing label states to get the UGS badge states from</param>
		/// <returns>List of badge states</returns>
		public static Dictionary<int, UgsBadgeState> GetUgsBadgeStates(this IJob Job, IGraph Graph, IReadOnlyList<(LabelState, LabelOutcome)> LabelStates)
		{
			Dictionary<int, UgsBadgeState> UgsBadgeStates = new Dictionary<int, UgsBadgeState>();
			for (int LabelIdx = 0; LabelIdx < LabelStates.Count; ++LabelIdx)
			{
				if (Graph.Labels[LabelIdx].UgsName == null)
				{
					continue;
				}

				(LabelState State, LabelOutcome Outcome) Label = LabelStates[LabelIdx];
				switch (Label.State)
				{
					case LabelState.Complete:
					{
						switch (Label.Outcome)
						{
							case LabelOutcome.Success:
							{
								UgsBadgeStates.Add(LabelIdx, UgsBadgeState.Success);
								break;
							}

							case LabelOutcome.Warnings:
							{
								UgsBadgeStates.Add(LabelIdx, UgsBadgeState.Warning);
								break;
							}

							case LabelOutcome.Failure:
							{
								UgsBadgeStates.Add(LabelIdx, UgsBadgeState.Failure);
								break;
							}

							case LabelOutcome.Unspecified:
							{
								UgsBadgeStates.Add(LabelIdx, UgsBadgeState.Skipped);
								break;
							}
						}
						break;
					}

					case LabelState.Running:
					{
						UgsBadgeStates.Add(LabelIdx, UgsBadgeState.Starting);
						break;
					}

					case LabelState.Unspecified:
					{
						UgsBadgeStates.Add(LabelIdx, UgsBadgeState.Skipped);
						break;
					}
				}
			}
			return UgsBadgeStates;
		}

		/// <summary>
		/// Gets the state of a job, as a label that includes all steps
		/// </summary>
		/// <param name="Job">The job to query</param>
		/// <param name="StepForNode">Map from node to step</param>
		/// <param name="NewState">Receives the state of the label</param>
		/// <param name="NewOutcome">Receives the outcome of the label</param>
		public static void GetJobState(this IJob Job, IReadOnlyDictionary<NodeRef, IJobStep> StepForNode, out LabelState NewState, out LabelOutcome NewOutcome)
		{
			List<NodeRef> Nodes = new List<NodeRef>();
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				foreach (IJobStep Step in Batch.Steps)
				{
					Nodes.Add(new NodeRef(Batch.GroupIdx, Step.NodeIdx));
				}
			}
			GetLabelState(Nodes, StepForNode, out NewState, out NewOutcome);
		}
		
		/// <summary>
		/// Gets the state of a label
		/// </summary>
		/// <param name="IncludedNodes">Nodes to include in this label</param>
		/// <param name="StepForNode">Map from node to step</param>
		/// <param name="NewState">Receives the state of the label</param>
		/// <param name="NewOutcome">Receives the outcome of the label</param>
		public static void GetLabelState(IEnumerable<NodeRef> IncludedNodes, IReadOnlyDictionary<NodeRef, IJobStep> StepForNode, out LabelState NewState, out LabelOutcome NewOutcome)
		{
			NewState = LabelState.Complete;
			NewOutcome = LabelOutcome.Success;
			foreach (NodeRef IncludedNodeRef in IncludedNodes)
			{
				IJobStep? IncludedStep;
				if (StepForNode.TryGetValue(IncludedNodeRef, out IncludedStep))
				{
					// Update the state
					if (IncludedStep.State != JobStepState.Completed && IncludedStep.State != JobStepState.Skipped && IncludedStep.State != JobStepState.Aborted)
					{
						NewState = LabelState.Running;
					}

					// Update the outcome
					if (IncludedStep.State == JobStepState.Skipped || IncludedStep.State == JobStepState.Aborted || IncludedStep.Outcome == JobStepOutcome.Failure)
					{
						NewOutcome = LabelOutcome.Failure;
					}
					else if (IncludedStep.Outcome == JobStepOutcome.Warnings && NewOutcome == LabelOutcome.Success)
					{
						NewOutcome = LabelOutcome.Warnings;
					}
				}
			}
		}

		/// <summary>
		/// Creates an RPC response object
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <returns></returns>
		public static HordeCommon.Rpc.GetJobResponse ToRpcResponse(this IJob Job)
		{
			HordeCommon.Rpc.GetJobResponse Response = new HordeCommon.Rpc.GetJobResponse();
			Response.StreamId = Job.StreamId.ToString();
			Response.Change = Job.Change;
			Response.CodeChange = Job.CodeChange;
			Response.PreflightChange = Job.PreflightChange;
			Response.ClonedPreflightChange = Job.ClonedPreflightChange;
			Response.Arguments.Add(Job.Arguments);
			return Response;
		}
	}

	/// <summary>
	/// Projection of a job definition to just include permissions info
	/// </summary>
	public interface IJobPermissions
	{
		/// <summary>
		/// ACL for the job
		/// </summary>
		public Acl? Acl { get; }

		/// <summary>
		/// The stream containing 
		/// </summary>
		public StreamId StreamId { get; }
	}
}
