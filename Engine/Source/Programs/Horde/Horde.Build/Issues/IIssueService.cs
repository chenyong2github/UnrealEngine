// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using Horde.Build.Models;
using Horde.Build.Utilities;
using Microsoft.Extensions.Hosting;
using MongoDB.Bson;

namespace Horde.Build.Services
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
		public delegate void IssueUpdatedEvent(IIssue issue);

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
		/// <param name="issueId">The issue id</param>
		/// <returns>The cached issue data</returns>
		Task<IIssueDetails?> GetCachedIssueDetailsAsync(int issueId);

		/// <summary>
		/// Gets the details for a particular issue
		/// </summary>
		/// <param name="issue">The issue id</param>
		/// <returns>The issue details</returns>
		Task<IIssueDetails> GetIssueDetailsAsync(IIssue issue);

		/// <summary>
		/// Determines whether to show alerts for a given issue
		/// </summary>
		/// <param name="issue">The issue to query</param>
		/// <param name="spans">Spans for the issue</param>
		/// <returns>True if we should show alerts for the given issue</returns>
		bool ShowDesktopAlertsForIssue(IIssue issue, IReadOnlyList<IIssueSpan> spans);

		/// <summary>
		/// Gets an issue with the given id
		/// </summary>
		/// <param name="issueId"></param>
		/// <returns></returns>
		Task<IIssue?> GetIssueAsync(int issueId);

		/// <summary>
		/// Gets the spans for the given issue
		/// </summary>
		/// <param name="issue">The issue id</param>
		/// <returns>List of spans for the issue</returns>
		Task<List<IIssueSpan>> GetIssueSpansAsync(IIssue issue);

		/// <summary>
		/// Gets the spans for the given issue
		/// </summary>
		/// <param name="span">The issue span</param>
		/// <returns>List of steps for the issue</returns>
		Task<List<IIssueStep>> GetIssueStepsAsync(IIssueSpan span);

		/// <summary>
		/// Gets the suspects for an issue
		/// </summary>
		/// <param name="issue"></param>
		/// <returns></returns>
		Task<List<IIssueSuspect>> GetIssueSuspectsAsync(IIssue issue);

		/// <summary>
		/// Searches for open issues
		/// </summary>
		/// <param name="ids">List of issue ids to include</param>
		/// <param name="userId">User to filter issues returned by</param>
		/// <param name="streamId">The stream to return issues for</param>
		/// <param name="minChange">The minimum changelist number to include</param>
		/// <param name="maxChange">The maximum changelist number to include</param>
		/// <param name="resolved">Whether to include results that are resolved</param>
		/// <param name="promoted">Whether to filter to issues that are promoted</param>
		/// <param name="index">Index within the results to return</param>
		/// <param name="count">Number of results</param>
		/// <returns>List of streams open in the given stream at the given changelist</returns>
		Task<List<IIssue>> FindIssuesAsync(IEnumerable<int>? ids = null, UserId? userId = null, StreamId? streamId = null, int? minChange = null, int? maxChange = null, bool? resolved = null, bool? promoted = null, int? index = null, int? count = null);

		/// <summary>
		/// Searches for open issues affecting a job
		/// </summary>
		/// <param name="ids"></param>
		/// <param name="job"></param>
		/// <param name="graph"></param>
		/// <param name="stepId"></param>
		/// <param name="batchId"></param>
		/// <param name="labelIdx"></param>
		/// <param name="userId"></param>
		/// <param name="resolved">Whether to include results that are resolved</param>
		/// <param name="promoted">Whether to filter by promoted issues</param>
		/// <param name="index">Index within the results to return</param>
		/// <param name="count">Number of results</param>
		/// <returns></returns>
		Task<List<IIssue>> FindIssuesForJobAsync(int[]? ids, IJob job, IGraph graph, SubResourceId? stepId = null, SubResourceId? batchId = null, int? labelIdx = null, UserId? userId = null, bool? resolved = null, bool? promoted = null, int? index = null, int? count = null);

		/// <summary>
		/// Find the events for a particular issue
		/// </summary>
		/// <param name="issueId">The issue id</param>
		/// <param name="logIds">Log ids to include</param>
		/// <param name="index">Index of the first event to return</param>
		/// <param name="count">Number of issues to return</param>
		/// <returns>List of log events</returns>
		Task<List<ILogEvent>> FindEventsForIssueAsync(int issueId, LogId[]? logIds = null, int index = 0, int count = 10);

		/// <summary>
		/// Updates the state of an issue
		/// </summary>
		/// <param name="id">The current issue id</param>s
		/// <param name="summary">New summary for the issue</param>
		/// <param name="description">New description for the issue</param>
		/// <param name="promoted">Whether the issue should be set as promoted</param>
		/// <param name="ownerId">New owner of the issue</param>
		/// <param name="nominatedById">Person that nominated the new owner</param>
		/// <param name="acknowledged">Whether the issue has been acknowledged</param>
		/// <param name="declinedById">Name of the user that has declined this issue</param>
		/// <param name="fixChange">Fix changelist for the issue</param>
		/// <param name="resolvedById">Whether the issue has been resolved</param>
		/// <param name="addSpanIds">Add spans to this issue</param>
		/// <param name="removeSpanIds">Remove spans from this issue</param>
		/// <param name="externalIssueKey">Key for external issue tracking</param>
		/// <param name="quarantinedById">User who has quarantined the issue</param>
		/// <returns>True if the issue was updated</returns>
		Task<bool> UpdateIssueAsync(int id, string? summary = null, string? description = null, bool? promoted = null, UserId? ownerId = null, UserId? nominatedById = null, bool? acknowledged = null, UserId? declinedById = null, int? fixChange = null, UserId? resolvedById = null, List<ObjectId>? addSpanIds = null, List<ObjectId>? removeSpanIds = null, string? externalIssueKey = null, UserId? quarantinedById = null);

		/// <summary>
		/// Marks a step as complete
		/// </summary>
		/// <param name="job">The job to update</param>
		/// <param name="graph">Graph for the job</param>
		/// <param name="batchId">Unique id of the batch</param>
		/// <param name="stepId">Unique id of the step</param>
		/// <returns>Async task</returns>
		Task UpdateCompleteStep(IJob job, IGraph graph, SubResourceId batchId, SubResourceId stepId);
	}
}
