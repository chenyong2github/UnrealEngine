// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Collections;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Server.Kestrel.Core.Features;
using Microsoft.IdentityModel.Tokens;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/issues endpoint
	/// </summary>
	[Authorize]
	[ApiController]
	[Route("[controller]")]
	public class IssuesController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the issue service
		/// </summary>
		private readonly IIssueService IssueService;

		/// <summary>
		/// Singleton instance of the job service
		/// </summary>
		private readonly JobService JobService;

		/// <summary>
		/// Singleton instance of the stream service
		/// </summary>
		private readonly StreamService StreamService;

		/// <summary>
		/// Collection of events
		/// </summary>
		private readonly ILogEventCollection LogEventCollection;

		/// <summary>
		/// The log file service
		/// </summary>
		private readonly ILogFileService LogFileService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="IssueService">The issue service</param>
		/// <param name="JobService">The job service</param>
		/// <param name="StreamService">The stream service</param>
		/// <param name="LogEventCollection">The event collection</param>
		/// <param name="LogFileService">The log file service</param>
		public IssuesController(IIssueService IssueService, JobService JobService, StreamService StreamService, ILogEventCollection LogEventCollection, ILogFileService LogFileService)
		{
			this.IssueService = IssueService;
			this.JobService = JobService;
			this.StreamService = StreamService;
			this.LogEventCollection = LogEventCollection;
			this.LogFileService = LogFileService;
		}

		/// <summary>
		/// Retrieve information about a specific issue
		/// </summary>
		/// <param name="Ids">Set of issue ids to find</param>
		/// <param name="StreamId">The stream to query for</param>
		/// <param name="Change">The changelist to query</param>
		/// <param name="MinChange">The minimum changelist range to query, inclusive</param>
		/// <param name="MaxChange">The minimum changelist range to query, inclusive</param>
		/// <param name="JobId">Job id to filter by</param>
		/// <param name="BatchId">The batch to filter by</param>
		/// <param name="StepId">The step to filter by</param>
		/// <param name="LabelIdx">The label within the job to filter by</param>
		/// <param name="User">User to filter issues for</param>
		/// <param name="Resolved">Whether to include resolved issues</param>
		/// <param name="Index">Starting offset of the window of results to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of matching agents</returns>
		[HttpGet]
		[Route("/api/v1/issues")]
		[ProducesResponseType(typeof(List<GetIssueResponse>), 200)]
		public async Task<ActionResult<object>> FindIssuesAsync([FromQuery(Name = "Id")] int[]? Ids = null, [FromQuery] string? StreamId = null, [FromQuery] int? Change = null, [FromQuery] int? MinChange = null, [FromQuery] int? MaxChange = null, [FromQuery] string? JobId = null, [FromQuery] string? BatchId = null, [FromQuery] string? StepId = null, [FromQuery(Name = "label")] int? LabelIdx = null, [FromQuery] string? User = null, [FromQuery] bool? Resolved = null, [FromQuery] int Index = 0, [FromQuery] int Count = 10, [FromQuery] PropertyFilter? Filter = null)
		{
			List<IIssue> Issues;
			if (JobId == null)
			{
				StreamId? StreamIdValue = null;
				if (StreamId != null)
				{
					StreamIdValue = new StreamId(StreamId);
				}

				Issues = await IssueService.FindIssuesAsync(Ids, StreamIdValue, MinChange ?? Change, MaxChange ?? Change, Resolved, Index, Count);
			}
			else
			{
				ObjectId JobIdValue = JobId.ToObjectId();

				IJob? Job = await JobService.GetJobAsync(JobIdValue);
				if (Job == null)
				{
					return NotFound();
				}
				if(!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, this.User, null))
				{
					return Forbid();
				}

				IGraph Graph = await JobService.GetGraphAsync(Job);
				Issues = await IssueService.FindIssuesForJobAsync(Ids, Job, Graph, StepId?.ToSubResourceId(), BatchId?.ToSubResourceId(), LabelIdx, Resolved, Index, Count);
			}

			StreamPermissionsCache PermissionsCache = new StreamPermissionsCache();

			List<object> Responses = new List<object>();
			foreach (IIssue Issue in Issues)
			{
				if (User == null || IncludeIssueForUser(Issue, User))
				{
					IIssueDetails Details = await IssueService.GetIssueDetailsAsync(Issue);
					if (await AuthorizeIssue(Details, PermissionsCache))
					{
						bool bShowDesktopAlerts = IssueService.ShowDesktopAlertsForIssue(Issue, Details.Spans);
						GetIssueResponse Response = await CreateIssueResponseAsync(Details, bShowDesktopAlerts);
						Responses.Add(PropertyFilter.Apply(Response, Filter));
					}
				}
			}
			return Responses;
		}

		/// <summary>
		/// Retrieve information about a specific issue
		/// </summary>
		/// <param name="IssueId">Id of the issue to get information about</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of matching agents</returns>
		[HttpGet]
		[Route("/api/v1/issues/{IssueId}")]
		[ProducesResponseType(typeof(GetIssueResponse), 200)]
		public async Task<ActionResult<object>> GetIssueAsync(int IssueId, [FromQuery] PropertyFilter? Filter = null)
		{
			IIssueDetails? Details = await IssueService.GetIssueDetailsAsync(IssueId);
			if (Details == null)
			{
				return NotFound();
			}
			if (!await AuthorizeIssue(Details, null))
			{
				return Forbid();
			}

			bool bShowDesktopAlerts = IssueService.ShowDesktopAlertsForIssue(Details.Issue, Details.Spans);
			return PropertyFilter.Apply(await CreateIssueResponseAsync(Details, bShowDesktopAlerts), Filter);
		}

		/// <summary>
		/// Create an issue response object
		/// </summary>
		/// <param name="Details"></param>
		/// <param name="ShowDesktopAlerts"></param>
		/// <returns></returns>
		async Task<GetIssueResponse> CreateIssueResponseAsync(IIssueDetails Details, bool ShowDesktopAlerts)
		{
			List<GetIssueAffectedStreamResponse> AffectedStreams = new List<GetIssueAffectedStreamResponse>();
			foreach (IGrouping<StreamId, IIssueSpan> StreamSpans in Details.Spans.GroupBy(x => x.StreamId))
			{
				IStream? Stream = await StreamService.GetCachedStream(StreamSpans.Key);
				AffectedStreams.Add(new GetIssueAffectedStreamResponse(Stream, StreamSpans));
			}
			return new GetIssueResponse(Details, AffectedStreams, ShowDesktopAlerts);
		}

		/// <summary>
		/// Retrieve events for a specific issue
		/// </summary>
		/// <param name="IssueId">Id of the issue to get information about</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of matching agents</returns>
		[HttpGet]
		[Route("/api/v1/issues/{IssueId}/streams")]
		[ProducesResponseType(typeof(List<GetIssueStreamResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetIssueStreamsAsync(int IssueId, [FromQuery] PropertyFilter? Filter = null)
		{
			IIssueDetails? Issue = await IssueService.GetIssueDetailsAsync(IssueId);
			if (Issue == null)
			{
				return NotFound();
			}

			StreamPermissionsCache Cache = new StreamPermissionsCache();
			if (!await AuthorizeIssue(Issue, Cache))
			{
				return Forbid();
			}

			List<object> Responses = new List<object>();
			foreach (IGrouping<StreamId, IIssueSpan> SpanGroup in Issue.Spans.GroupBy(x => x.StreamId))
			{
				if (await StreamService.AuthorizeAsync(SpanGroup.Key, AclAction.ViewStream, User, Cache))
				{
					HashSet<ObjectId> SpanIds = new HashSet<ObjectId>(SpanGroup.Select(x => x.Id));
					List<IIssueStep> Steps = Issue.Steps.Where(x => SpanIds.Contains(x.SpanId)).ToList();
					Responses.Add(PropertyFilter.Apply(new GetIssueStreamResponse(SpanGroup.Key, SpanGroup.ToList(), Steps), Filter));
				}
			}
			return Responses;
		}

		/// <summary>
		/// Retrieve events for a specific issue
		/// </summary>
		/// <param name="IssueId">Id of the issue to get information about</param>
		/// <param name="StreamId">The stream id</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of matching agents</returns>
		[HttpGet]
		[Route("/api/v1/issues/{IssueId}/streams/{StreamId}")]
		[ProducesResponseType(typeof(List<GetIssueStreamResponse>), 200)]
		public async Task<ActionResult<object>> GetIssueStreamAsync(int IssueId, string StreamId, [FromQuery] PropertyFilter? Filter = null)
		{
			IIssueDetails? Details = await IssueService.GetIssueDetailsAsync(IssueId);
			if (Details == null)
			{
				return NotFound();
			}

			StreamId StreamIdValue = new StreamId(StreamId);
			if (!await StreamService.AuthorizeAsync(StreamIdValue, AclAction.ViewStream, User, null))
			{
				return Forbid();
			}

			List<IIssueSpan> Spans = Details.Spans.Where(x => x.StreamId == StreamIdValue).ToList();
			if(Spans.Count == 0)
			{
				return NotFound();
			}

			HashSet<ObjectId> SpanIds = new HashSet<ObjectId>(Spans.Select(x => x.Id));
			List<IIssueStep> Steps = Details.Steps.Where(x => SpanIds.Contains(x.SpanId)).ToList();

			return PropertyFilter.Apply(new GetIssueStreamResponse(StreamIdValue, Spans, Steps), Filter);
		}

		/// <summary>
		/// Retrieve events for a specific issue
		/// </summary>
		/// <param name="IssueId">Id of the issue to get information about</param>
		/// <param name="JobId">The job id to filter for</param>
		/// <param name="BatchId">The batch to filter by</param>
		/// <param name="StepId">The step to filter by</param>
		/// <param name="LabelIdx">The label within the job to filter by</param>
		/// <param name="LogIds">List of log ids to return issues for</param>
		/// <param name="Index">Index of the first event</param>
		/// <param name="Count">Number of events to return</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of matching agents</returns>
		[HttpGet]
		[Route("/api/v1/issues/{IssueId}/events")]
		[ProducesResponseType(typeof(List<GetLogEventResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetIssueEventsAsync(int IssueId, [FromQuery] string? JobId = null, [FromQuery] string? BatchId = null, [FromQuery] string? StepId = null, [FromQuery(Name = "label")] int? LabelIdx = null, [FromQuery] string[]? LogIds = null, [FromQuery] int Index = 0, [FromQuery] int Count = 10, [FromQuery] PropertyFilter? Filter = null)
		{
			HashSet<ObjectId> LogIdValues = new HashSet<ObjectId>();
			if(JobId != null)
			{
				IJob? Job = await JobService.GetJobAsync(JobId.ToObjectId());
				if(Job == null)
				{
					return NotFound();
				}

				if (StepId != null)
				{
					IJobStep? Step;
					if (Job.TryGetStep(StepId.ToSubResourceId(), out Step) && Step.Outcome != JobStepOutcome.Success && Step.LogId != null)
					{
						LogIdValues.Add(Step.LogId.Value);
					}
				}
				else if (BatchId != null)
				{
					IJobStepBatch? Batch;
					if (Job.TryGetBatch(BatchId.ToSubResourceId(), out Batch))
					{
						LogIdValues.UnionWith(Batch.Steps.Where(x => x.Outcome != JobStepOutcome.Success && x.LogId != null).Select(x => x.LogId!.Value));
					}
				}
				else if (LabelIdx != null)
				{
					IGraph Graph = await JobService.GetGraphAsync(Job);

					HashSet<NodeRef> IncludedNodes = new HashSet<NodeRef>(Graph.Labels[LabelIdx.Value].IncludedNodes);

					foreach (IJobStepBatch Batch in Job.Batches)
					{
						foreach (IJobStep Step in Batch.Steps)
						{
							NodeRef NodeRef = new NodeRef(Batch.GroupIdx, Step.NodeIdx);
							if (Step.Outcome != JobStepOutcome.Success && Step.LogId != null && IncludedNodes.Contains(NodeRef))
							{
								LogIdValues.Add(Step.LogId.Value);
							}
						}
					}
				}
				else
				{
					LogIdValues.UnionWith(Job.Batches.SelectMany(x => x.Steps).Where(x => x.Outcome != JobStepOutcome.Success && x.LogId != null).Select(x => x.LogId!.Value));
				}
			}
			if(LogIds != null)
			{
				LogIdValues.UnionWith(LogIds.Select(x => x.ToObjectId()));
			}

			List<ILogEvent> Events = await IssueService.FindEventsForIssueAsync(IssueId, LogIdValues.ToArray(), Index, Count);

			JobPermissionsCache PermissionsCache = new JobPermissionsCache();
			Dictionary<ObjectId, ILogFile?> LogFiles = new Dictionary<ObjectId, ILogFile?>();

			List<object> Responses = new List<object>();
			foreach (ILogEvent Event in Events)
			{
				ILogFile? LogFile;
				if (!LogFiles.TryGetValue(Event.LogId, out LogFile))
				{
					LogFile = await LogFileService.GetLogFileAsync(Event.LogId);
					LogFiles[Event.LogId] = LogFile;
				}
				if (LogFile != null && await JobService.AuthorizeAsync(LogFile.JobId, AclAction.ViewLog, User, PermissionsCache))
				{
					ILogEventData Data = await LogFileService.GetEventDataAsync(LogFile, Event.LineIndex, Event.LineCount);
					GetLogEventResponse Response = new GetLogEventResponse(Event, Data, IssueId);
					Responses.Add(PropertyFilter.Apply(Response, Filter));
				}
			}
			return Responses;
		}

		/// <summary>
		/// Authorize the current user to see an issue
		/// </summary>
		/// <param name="Issue">The issue to authorize</param>
		/// <param name="PermissionsCache">Cache of permissions</param>
		/// <returns>True if the user is authorized to see the issue</returns>
		private async Task<bool> AuthorizeIssue(IIssueDetails Issue, StreamPermissionsCache? PermissionsCache)
		{
			foreach (StreamId StreamId in Issue.Spans.Select(x => x.StreamId).Distinct())
			{
				if (await StreamService.AuthorizeAsync(StreamId, AclAction.ViewStream, User, PermissionsCache))
				{
					return true;
				}
			}
			return false;
		}

		/// <summary>
		/// Determines whether to include an issue for a particular user
		/// </summary>
		/// <param name="Issue"></param>
		/// <param name="User"></param>
		/// <returns></returns>
		static bool IncludeIssueForUser(IIssue Issue, string User)
		{
			if (Issue.Owner != null)
			{
				return Issue.Owner.Equals(User, StringComparison.OrdinalIgnoreCase);
			}
			else
			{
				return Issue.Suspects.Any(x => x.Author.Equals(User, StringComparison.OrdinalIgnoreCase));
			}
		}

		/// <summary>
		/// Update an issue
		/// </summary>
		/// <param name="IssueId">Id of the issue to get information about</param>
		/// <param name="Request">The update information</param>
		/// <returns>List of matching agents</returns>
		[HttpPut]
		[Route("/api/v1/issues/{IssueId}")]
		public async Task<ActionResult> UpdateIssueAsync(int IssueId, [FromBody] UpdateIssueRequest Request)
		{
			string? DeclinedBy = null;
			if(Request.Declined ?? false)
			{
				DeclinedBy = User.GetPerforceUser();
			}
			if (!await IssueService.UpdateIssueAsync(IssueId, Request.Summary, Request.Owner, Request.NominatedBy, Request.Acknowledged, DeclinedBy, Request.FixChange, Request.Resolved))
			{
				return NotFound();
			}
			return Ok();
		}
	}
}
