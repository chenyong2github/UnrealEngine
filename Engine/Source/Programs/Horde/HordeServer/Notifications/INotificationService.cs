// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;

namespace HordeServer.Notifications
{
	/// <summary>
	/// Interface for the notification service
	/// </summary>
	public interface INotificationService
	{
		/// <summary>
		/// Updates a subscription to a trigger
		/// </summary>
		/// <param name="TriggerId"></param>
		/// <param name="User"></param>
		/// <param name="Email">Whether to receive email notifications</param>
		/// <param name="Slack">Whether to receive Slack notifications</param>
		/// <returns></returns>
		Task<bool> UpdateSubscriptionsAsync(ObjectId TriggerId, ClaimsPrincipal User, bool? Email, bool? Slack);

		/// <summary>
		/// Gets the current subscriptions for a user
		/// </summary>
		/// <param name="TriggerId"></param>
		/// <param name="User"></param>
		/// <returns>Subscriptions for that user</returns>
		Task<INotificationSubscription?> GetSubscriptionsAsync(ObjectId TriggerId, ClaimsPrincipal User);

		/// <summary>
		/// Notify all subscribers that a job step has finished
		/// </summary>
		/// <param name="Job">The job containing the step that has finished</param>
		/// <param name="Graph"></param>
		/// <param name="BatchId">The batch id</param>
		/// <param name="StepId">The step id</param>
		/// <returns>Async task</returns>
		void NotifyJobStepComplete(IJob Job, IGraph Graph, SubResourceId BatchId, SubResourceId StepId);

		/// <summary>
		/// Notify all subscribers that a job step's outcome has changed
		/// </summary>
		/// <param name="Job">The job containing the step that has changed</param>
		/// <param name="OldLabelStates">The old label states for the job</param>
		/// <param name="NewLabelStates">The new label states for the job</param>
		/// <returns>Async task</returns>
		void NotifyLabelUpdate(IJob Job, IReadOnlyList<(LabelState, LabelOutcome)> OldLabelStates, IReadOnlyList<(LabelState, LabelOutcome)> NewLabelStates);

		/// <summary>
		/// Notify slack channel about a stream update failure
		/// </summary>
		/// <param name="Stream">The stream</param>
		/// <param name="ErrorMessage">Error message passed back</param>
		/// <param name="Change">Latest change number of the file</param>
		/// <param name="Author">Author of the change, if found from p4 service</param>
		/// <param name="Description">Description of the change, if found from p4 service</param>
		void NotifyUpdateStreamFailure(IStream Stream, string ErrorMessage, int Change, string? Author, string? Description);

		/// <summary>
		/// Notify slack channel about a stream update failure in relation to a file
		/// </summary>
		/// <param name="File">The file</param>
		void NotifyUpdateStreamFailure(FileSummary File);

		/// <summary>
		/// Sends a notification to a user regarding a build health issue.
		/// </summary>
		/// <param name="Issue">The new issue that was created</param>
		void NotifyIssueUpdated(IIssue Issue);
	}
}
