// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Api;
using HordeServer.Models;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Notifications
{
	/// <summary>
	/// Implements a notification method
	/// </summary>
	public interface INotificationSink
	{
		/// <summary>
		/// Send notifications that a job has completed
		/// </summary>
		/// <param name="JobStream"></param>
		/// <param name="Job">The job containing the step</param>
		/// <param name="Graph"></param>
		/// <param name="Outcome"></param>
		/// <returns>Async task</returns>
		Task NotifyJobCompleteAsync(IStream JobStream, IJob Job, IGraph Graph, LabelOutcome Outcome);

		/// <summary>
		/// Send notifications that a job has completed
		/// </summary>
		/// <param name="User">User to notify</param>
		/// <param name="JobStream"></param>
		/// <param name="Job">The job containing the step</param>
		/// <param name="Graph"></param>
		/// <param name="Outcome"></param>
		/// <returns>Async task</returns>
		Task NotifyJobCompleteAsync(IUser User, IStream JobStream, IJob Job, IGraph Graph, LabelOutcome Outcome);

		/// <summary>
		/// Send notifications that a job step has completed
		/// </summary>
		/// <param name="User">User to notify</param>
		/// <param name="JobStream">Stream containing the job</param>
		/// <param name="Job">The job containing the step</param>
		/// <param name="Batch">Unique id of the batch</param>
		/// <param name="Step">The step id</param>
		/// <param name="Node">Corresponding node for the step</param>
		/// <param name="JobStepEventData"></param>
		/// <returns>Async task</returns>
		Task NotifyJobStepCompleteAsync(IUser User, IStream JobStream, IJob Job, IJobStepBatch Batch, IJobStep Step, INode Node, List<ILogEventData> JobStepEventData);

		/// <summary>
		/// Send notifications that a job step has completed
		/// </summary>
		/// <param name="User">User to notify</param>
		/// <param name="Job">The job containing the step</param>
		/// <param name="Stream"></param>
		/// <param name="Label"></param>
		/// <param name="LabelIdx"></param>
		/// <param name="Outcome"></param>
		/// <param name="StepData"></param>
		/// <returns>Async task</returns>
		Task NotifyLabelCompleteAsync(IUser User, IJob Job, IStream Stream, ILabel Label, int LabelIdx, LabelOutcome Outcome, List<(string, JobStepOutcome, Uri)> StepData);

		/// <summary>
		/// Send notifications that a new issue has been created or assigned
		/// </summary>
		/// <param name="Issue"></param>
		/// <returns></returns>
		Task NotifyIssueUpdatedAsync(IIssue Issue);

		/// <summary>
		/// Notification that a stream has failed to update
		/// </summary>
		/// <param name="ErrorMessage"></param>
		/// <param name="FileName"></param>
		/// <param name="Change"></param>
		/// <param name="Author"></param>
		/// <param name="Description"></param>
		/// <returns></returns>
		Task NotifyConfigUpdateFailureAsync(string ErrorMessage, string FileName, int? Change = null, IUser? Author = null, string? Description = null);
		
		/// <summary>
		/// Notification for device service
		/// </summary>
		/// <param name="Message"></param>
		/// <param name="Device"></param>
		/// <param name="Pool"></param>
		/// <param name="Stream"></param>
		/// <param name="Job"></param>
        /// <param name="Step"></param>
        /// <param name="Node"></param>
		/// <returns></returns>
        Task NotifyDeviceServiceAsync(string Message, IDevice? Device = null, IDevicePool? Pool = null, IStream? Stream = null, IJob? Job = null, IJobStep? Step = null, INode? Node = null);
    }
}
