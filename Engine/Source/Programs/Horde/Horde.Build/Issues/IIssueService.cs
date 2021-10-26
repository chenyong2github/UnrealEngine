// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Hosting;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	using LogId = ObjectId<ILogFile>;
	using StreamId = StringId<IStream>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Service which manages build health issues
	/// </summary>
	public interface IIssueService : IHostedService
	{
		/// <summary>
		/// Delegate for issue creation events
		/// </summary>
		public delegate void IssueUpdatedEvent(IIssue Issue);

		/// <summary>
		/// Event triggered when a label state changes
		/// </summary>
		event IssueUpdatedEvent? OnIssueUpdated;

		/// <summary>
		/// Gets a list of cached open issues
		/// </summary>
		/// <returns>List of cached issues</returns>
		IReadOnlyList<IIssueDetails> CachedOpenIssues { get; }

		/// <summary>
		/// Find a cached issue, or update it
		/// </summary>
		/// <param name="IssueId">The issue id</param>
		/// <returns>The cached issue data</returns>
		Task<IIssueDetails?> GetCachedIssueDetailsAsync(int IssueId);

		/// <summary>
		/// Gets the details for a particular issue
		/// </summary>
		/// <param name="Issue">The issue id</param>
		/// <returns>The issue details</returns>
		Task<IIssueDetails> GetIssueDetailsAsync(IIssue Issue);

		/// <summary>
		/// Determines whether to show alerts for a given issue
		/// </summary>
		/// <param name="Issue">The issue to query</param>
		/// <param name="Spans">Spans for the issue</param>
		/// <returns>True if we should show alerts for the given issue</returns>
		bool ShowDesktopAlertsForIssue(IIssue Issue, IReadOnlyList<IIssueSpan> Spans);

		/// <summary>
		/// Gets an issue with the given id
		/// </summary>
		/// <param name="IssueId"></param>
		/// <returns></returns>
		Task<IIssue?> GetIssueAsync(int IssueId);

		/// <summary>
		/// Gets the spans for the given issue
		/// </summary>
		/// <param name="Issue">The issue id</param>
		/// <returns>List of spans for the issue</returns>
		Task<List<IIssueSpan>> GetIssueSpansAsync(IIssue Issue);

		/// <summary>
		/// Gets the spans for the given issue
		/// </summary>
		/// <param name="Span">The issue span</param>
		/// <returns>List of steps for the issue</returns>
		Task<List<IIssueStep>> GetIssueStepsAsync(IIssueSpan Span);

		/// <summary>
		/// Gets the suspects for an issue
		/// </summary>
		/// <param name="Issue"></param>
		/// <returns></returns>
		Task<List<IIssueSuspect>> GetIssueSuspectsAsync(IIssue Issue);

		/// <summary>
		/// Searches for open issues
		/// </summary>
		/// <param name="Ids">List of issue ids to include</param>
		/// <param name="UserId">User to filter issues returned by</param>
		/// <param name="StreamId">The stream to return issues for</param>
		/// <param name="MinChange">The minimum changelist number to include</param>
		/// <param name="MaxChange">The maximum changelist number to include</param>
		/// <param name="Resolved">Whether to include results that are resolved</param>
		/// <param name="Promoted">Whether to filter to issues that are promoted</param>
		/// <param name="Index">Index within the results to return</param>
		/// <param name="Count">Number of results</param>
		/// <returns>List of streams open in the given stream at the given changelist</returns>
		Task<List<IIssue>> FindIssuesAsync(IEnumerable<int>? Ids = null, UserId? UserId = null, StreamId? StreamId = null, int? MinChange = null, int? MaxChange = null, bool? Resolved = null, bool? Promoted = null, int? Index = null, int? Count = null);

		/// <summary>
		/// Searches for open issues affecting a job
		/// </summary>
		/// <param name="Ids"></param>
		/// <param name="Job"></param>
		/// <param name="Graph"></param>
		/// <param name="StepId"></param>
		/// <param name="BatchId"></param>
		/// <param name="LabelIdx"></param>
		/// <param name="UserId"></param>
		/// <param name="Resolved">Whether to include results that are resolved</param>
		/// <param name="Promoted">Whether to filter by promoted issues</param>
		/// <param name="Index">Index within the results to return</param>
		/// <param name="Count">Number of results</param>
		/// <returns></returns>
		Task<List<IIssue>> FindIssuesForJobAsync(int[]? Ids, IJob Job, IGraph Graph, SubResourceId? StepId = null, SubResourceId? BatchId = null, int? LabelIdx = null, UserId? UserId = null, bool? Resolved = null, bool? Promoted = null, int? Index = null, int? Count = null);

		/// <summary>
		/// Find the events for a particular issue
		/// </summary>
		/// <param name="IssueId">The issue id</param>
		/// <param name="LogIds">Log ids to include</param>
		/// <param name="Index">Index of the first event to return</param>
		/// <param name="Count">Number of issues to return</param>
		/// <returns>List of log events</returns>
		Task<List<ILogEvent>> FindEventsForIssueAsync(int IssueId, LogId[]? LogIds = null, int Index = 0, int Count = 10);

		/// <summary>
		/// Updates the state of an issue
		/// </summary>
		/// <param name="Id">The current issue id</param>s
		/// <param name="Summary">New summary for the issue</param>
		/// <param name="Description">New description for the issue</param>
		/// <param name="Promoted">Whether the issue should be set as promoted</param>
		/// <param name="OwnerId">New owner of the issue</param>
		/// <param name="NominatedById">Person that nominated the new owner</param>
		/// <param name="Acknowledged">Whether the issue has been acknowledged</param>
		/// <param name="DeclinedById">Name of the user that has declined this issue</param>
		/// <param name="FixChange">Fix changelist for the issue</param>
		/// <param name="ResolvedById">Whether the issue has been resolved</param>
		/// <param name="AddSpanIds">Add spans to this issue</param>
		/// <param name="RemoveSpanIds">Remove spans from this issue</param>
		/// <returns>True if the issue was updated</returns>
		Task<bool> UpdateIssueAsync(int Id, string? Summary = null, string? Description = null, bool? Promoted = null, UserId? OwnerId = null, UserId? NominatedById = null, bool? Acknowledged = null, UserId? DeclinedById = null, int? FixChange = null, UserId? ResolvedById = null, List<ObjectId>? AddSpanIds = null, List<ObjectId>? RemoveSpanIds = null);

		/// <summary>
		/// Marks a step as complete
		/// </summary>
		/// <param name="Job">The job to update</param>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="BatchId">Unique id of the batch</param>
		/// <param name="StepId">Unique id of the step</param>
		/// <returns>Async task</returns>
		Task UpdateCompleteStep(IJob Job, IGraph Graph, SubResourceId BatchId, SubResourceId StepId);
	}
}
