// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using HordeServer.Utilities;

namespace HordeServer.Collections
{
	using LogId = ObjectId<ILogFile>;
	using PoolId = StringId<IPool>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Interface for a collection of JobStepRef documents
	/// </summary>
	public interface IJobStepRefCollection
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Unique id for the step being referenced</param>
		/// <param name="JobName">Name of the job</param>
		/// <param name="StepName">Name of the step</param>
		/// <param name="StreamId">Unique id for the stream containing the job</param>
		/// <param name="TemplateId"></param>
		/// <param name="Change">The change number being built</param>
		/// <param name="LogId">The log file id</param>
		/// <param name="PoolId">The pool id</param>
		/// <param name="AgentId">The agent id</param>
		/// <param name="Outcome">Outcome of this step, if known</param>
		/// <param name="LastSuccess">The last change that completed with success</param>
		/// <param name="LastWarning">The last change that completed with a warning (or success)</param>
		/// <param name="WaitTime">Time taken for the batch containing this step to start</param>
		/// <param name="InitTime">Time taken for the batch containing this step to initializer</param>
		/// <param name="StartTimeUtc">Start time</param>
		/// <param name="FinishTimeUtc">Finish time for the step, if known</param>
		Task<IJobStepRef> InsertOrReplaceAsync(JobStepRefId Id, string JobName, string StepName, StreamId StreamId, TemplateRefId TemplateId, int Change, LogId? LogId, PoolId? PoolId, AgentId? AgentId, JobStepOutcome? Outcome, int? LastSuccess, int? LastWarning, float WaitTime, float InitTime, DateTime StartTimeUtc, DateTime? FinishTimeUtc);

		/// <summary>
		/// Gets the history of a given node
		/// </summary>
		/// <param name="StreamId">Unique id for a stream</param>
		/// <param name="TemplateId"></param>
		/// <param name="NodeName">Name of the node</param>
		/// <param name="Change">The current change</param>
		/// <param name="IncludeFailed">Whether to include failed nodes</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of step references</returns>
		Task<List<IJobStepRef>> GetStepsForNodeAsync(StreamId StreamId, TemplateRefId TemplateId, string NodeName, int? Change, bool IncludeFailed, int Count);

		/// <summary>
		/// Gets the previous job that ran a given step
		/// </summary>
		/// <param name="StreamId">Id of the stream to search</param>
		/// <param name="TemplateId">The template id</param>
		/// <param name="NodeName">Name of the step to find</param>
		/// <param name="Change">The current changelist number</param>
		/// <returns>The previous job, or null.</returns>
		Task<IJobStepRef?> GetPrevStepForNodeAsync(StreamId StreamId, TemplateRefId TemplateId, string NodeName, int Change);

		/// <summary>
		/// Gets the next job that ran a given step
		/// </summary>
		/// <param name="StreamId">Id of the stream to search</param>
		/// <param name="TemplateId">The template id</param>
		/// <param name="NodeName">Name of the step to find</param>
		/// <param name="Change">The current changelist number</param>
		/// <returns>The previous job, or null.</returns>
		Task<IJobStepRef?> GetNextStepForNodeAsync(StreamId StreamId, TemplateRefId TemplateId, string NodeName, int Change);
	}

	static class JobStepRefCollectionExtensions
	{
		public static async Task UpdateAsync(this IJobStepRefCollection JobStepRefs, IJob Job, IJobStepBatch Batch, IJobStep Step, IGraph Graph)
		{
			if (Job.PreflightChange == 0)
			{
				float WaitTime = (float)(Batch.GetWaitTime() ?? TimeSpan.Zero).TotalSeconds;
				float InitTime = (float)(Batch.GetInitTime() ?? TimeSpan.Zero).TotalSeconds;

				string NodeName = Graph.Groups[Batch.GroupIdx].Nodes[Step.NodeIdx].Name;
				JobStepOutcome? Outcome = Step.IsPending() ? (JobStepOutcome?)null : Step.Outcome;

				int? LastSuccess = null;
				int? LastWarning = null;
				if (Outcome != JobStepOutcome.Success)
				{
					IJobStepRef? PrevStep = await JobStepRefs.GetPrevStepForNodeAsync(Job.StreamId, Job.TemplateId, NodeName, Job.Change);
					if (PrevStep != null)
					{
						LastSuccess = PrevStep.LastSuccess;
						if (Outcome != JobStepOutcome.Warnings)
						{
							LastWarning = PrevStep.LastWarning;
						}
					}
				}

				// If the job has been aborted, mark the step as finished and set outcome to unspecified
				DateTime? FinishTime = Step.FinishTimeUtc;
				if (Job.AbortedByUserId != null && Outcome == null)
				{
					Outcome = JobStepOutcome.Unspecified;
					FinishTime = DateTime.UtcNow;
				}

				await JobStepRefs.InsertOrReplaceAsync(new JobStepRefId(Job.Id, Batch.Id, Step.Id), Job.Name, NodeName, Job.StreamId, Job.TemplateId, Job.Change, Step.LogId, Batch.PoolId, Batch.AgentId, Outcome, LastSuccess, LastWarning, WaitTime, InitTime, Step.StartTimeUtc ?? DateTime.UtcNow, FinishTime);
			}
		}
	}
}
