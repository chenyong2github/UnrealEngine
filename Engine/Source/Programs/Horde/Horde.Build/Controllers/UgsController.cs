// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Server.Kestrel.Core.Features;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using Serilog.Events;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;

namespace HordeServer.Controllers
{
	using LogId = ObjectId<ILogFile>;
	using StreamId = StringId<IStream>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Controller for the /api/v1/issues endpoint
	/// </summary>
	[ApiController]
	[Route("[controller]")]
	public sealed class UgsController : ControllerBase
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
		/// Collection of stream documents
		/// </summary>
		private readonly StreamService StreamService;

		/// <summary>
		/// Collection of metadata documents
		/// </summary>
		private readonly IUgsMetadataCollection UgsMetadataCollection;

		/// <summary>
		/// Collection of log events
		/// </summary>
		private readonly ILogEventCollection LogEventCollection;

		/// <summary>
		/// Collection of users
		/// </summary>
		private readonly IUserCollection UserCollection;

		/// <summary>
		/// The log file service
		/// </summary>
		private readonly ILogFileService LogFileService;

		/// <summary>
		/// Server settings
		/// </summary>
		private readonly ServerSettings Settings;

		/// <summary>
		/// Logger 
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="IssueService">The issue service</param>
		/// <param name="JobService">The job service</param>
		/// <param name="StreamService">Collection of stream documents</param>
		/// <param name="UgsMetadataCollection">Collection of UGS metadata documents</param>
		/// <param name="LogEventCollection">Collection of log event documents</param>
		/// <param name="UserCollection">Collection of user documents</param>
		/// <param name="LogFileService">The log file service</param>
		/// <param name="OptionsMonitor">The server settings</param>
		/// <param name="Logger">Logger</param>
		public UgsController(IIssueService IssueService, JobService JobService, StreamService StreamService, IUgsMetadataCollection UgsMetadataCollection, ILogEventCollection LogEventCollection, IUserCollection UserCollection, ILogFileService LogFileService, IOptionsMonitor<ServerSettings> OptionsMonitor, ILogger<UgsController> Logger)
		{
			this.IssueService = IssueService;
			this.JobService = JobService;
			this.StreamService = StreamService;
			this.UgsMetadataCollection = UgsMetadataCollection;
			this.LogEventCollection = LogEventCollection;
			this.UserCollection = UserCollection;
			this.LogFileService = LogFileService;
			this.Settings = OptionsMonitor.CurrentValue;
			this.Logger = Logger;
		}

		/// <summary>
		/// Gets the latest version info
		/// </summary>
		/// <returns>Result code</returns>
		[HttpGet]
		[Route("/ugs/api/latest")]
		public ActionResult<object> GetLatest()
		{
			return new { Version = 2 };
		}

		/// <summary>
		/// Adds new metadata to the database
		/// </summary>
		/// <param name="Request">Request object</param>
		/// <returns>Result code</returns>
		[HttpPost]
		[Route("/ugs/api/metadata")]
		public async Task<ActionResult> AddMetadataAsync(AddUgsMetadataRequest Request)
		{
			IUgsMetadata Metadata = await UgsMetadataCollection.FindOrAddAsync(Request.Stream, Request.Change, Request.Project);
			if (Request.Synced != null || Request.Vote != null || Request.Investigating != null || Request.Starred != null || Request.Comment != null)
			{
				if (Request.UserName == null)
				{
					return BadRequest("Missing UserName field on request body");
				}
				Metadata = await UgsMetadataCollection.UpdateUserAsync(Metadata, Request.UserName, Request.Synced, Request.Vote, Request.Investigating, Request.Starred, Request.Comment);
			}
			if (Request.Badges != null)
			{
				foreach (AddUgsBadgeRequest Badge in Request.Badges)
				{
					Metadata = await UgsMetadataCollection.UpdateBadgeAsync(Metadata, Badge.Name, Badge.Url, Badge.State);
				}
			}
			return Ok();
		}

		/// <summary>
		/// Searches for metadata updates
		/// </summary>
		/// <param name="Stream">THe stream to search for</param>
		/// <param name="MinChange">Minimum changelist number</param>
		/// <param name="MaxChange">Maximum changelist number</param>
		/// <param name="Project">The project identifiers to search for</param>
		/// <param name="Sequence">Last sequence number</param>
		/// <returns>List of metadata updates</returns>
		[HttpGet]
		[Route("/ugs/api/metadata")]
		public async Task<GetUgsMetadataListResponse> FindMetadataAsync([FromQuery] string Stream, [FromQuery] int MinChange, [FromQuery] int? MaxChange = null, [FromQuery] string? Project = null, [FromQuery] long? Sequence = null)
		{
			List<IUgsMetadata> MetadataList = await UgsMetadataCollection.FindAsync(Stream, MinChange, MaxChange, Sequence);
		
			GetUgsMetadataListResponse Response = new GetUgsMetadataListResponse();
			if(Sequence != null)
			{
				Response.SequenceNumber = Sequence.Value;
			}

			foreach (IUgsMetadata Metadata in MetadataList)
			{
				if (Metadata.UpdateTicks > Response.SequenceNumber)
				{
					Response.SequenceNumber = Metadata.UpdateTicks;
				}
				if (String.IsNullOrEmpty(Metadata.Project) || Metadata.Project.Equals(Project, StringComparison.OrdinalIgnoreCase))
				{
					Response.Items.Add(new GetUgsMetadataResponse(Metadata));
				}
			}
			return Response;
		}

		/// <summary>
		/// Retrieve information about open issues
		/// </summary>
		/// <param name="User"></param>
		/// <param name="IncludeResolved">Whether to include resolved issues</param>
		/// <param name="MaxResults">Maximum number of results to return</param>
		/// <returns>List of matching agents</returns>
		[HttpGet]
		[Route("/ugs/api/issues")]
		[ProducesResponseType(typeof(GetUgsIssueResponse), 200)]
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1801:Remove unused parameter", Justification = "<Pending>")]
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "<Pending>")]
		public async Task<ActionResult<List<GetUgsIssueResponse>>> GetIssuesAsync([FromQuery] string? User = null, [FromQuery] bool IncludeResolved = false, [FromQuery] int MaxResults = 100)
		{
			IUser? UserInfo = (User != null) ? await UserCollection.FindUserByLoginAsync(User) : null;

			List<GetUgsIssueResponse> Responses = new List<GetUgsIssueResponse>();
			if (IncludeResolved)
			{
				List<IIssue> Issues = await IssueService.FindIssuesAsync(null, Resolved: null, Count: MaxResults);
				foreach(IIssue Issue in Issues)
				{
					IIssueDetails Details = await IssueService.GetIssueDetailsAsync(Issue);
					bool bNotify = UserInfo != null && Details.Suspects.Any(x => x.AuthorId == UserInfo.Id);
					Responses.Add(await CreateIssueResponseAsync(Details, bNotify));
				}
			}
			else
			{
				foreach (IIssueDetails CachedOpenIssue in IssueService.CachedOpenIssues)
				{
					if (Responses.Count >= MaxResults)
					{
						break;
					}

					if (CachedOpenIssue.ShowNotifications())
					{
						bool bNotify = UserInfo != null && CachedOpenIssue.IncludeForUser(UserInfo.Id);
						Responses.Add(await CreateIssueResponseAsync(CachedOpenIssue, bNotify));
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
		[Route("/ugs/api/issues/{IssueId}")]
		[ProducesResponseType(typeof(GetUgsIssueBuildResponse), 200)]
		public async Task<ActionResult<object>> GetIssueAsync(int IssueId, [FromQuery] PropertyFilter? Filter = null)
		{
			IIssueDetails? Issue = await IssueService.GetIssueDetailsAsync(IssueId);
			if (Issue == null)
			{
				return NotFound();
			}

			return PropertyFilter.Apply(await CreateIssueResponseAsync(Issue, false), Filter);
		}

		/// <summary>
		/// Retrieve information about builds for a specific issue
		/// </summary>
		/// <param name="IssueId">Id of the issue to get information about</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of matching agents</returns>
		[HttpGet]
		[Route("/ugs/api/issues/{IssueId}/builds")]
		[ProducesResponseType(typeof(List<GetUgsIssueBuildResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetIssueBuildsAsync(int IssueId, [FromQuery] PropertyFilter? Filter = null)
		{
			IIssueDetails? Issue = await IssueService.GetCachedIssueDetailsAsync(IssueId);
			if (Issue == null)
			{
				return NotFound();
			}

			List<GetUgsIssueBuildResponse> Responses = new List<GetUgsIssueBuildResponse>();
			foreach (IIssueSpan Span in Issue.Spans)
			{
				if (Span.LastSuccess != null)
				{
					Responses.Add(CreateBuildResponse(Span, Span.LastSuccess, IssueBuildOutcome.Success));
				}
				foreach (IIssueStep Step in Issue.Steps)
				{
					Responses.Add(CreateBuildResponse(Span, Step, IssueBuildOutcome.Error));
				}
				if (Span.NextSuccess != null)
				{
					Responses.Add(CreateBuildResponse(Span, Span.NextSuccess, IssueBuildOutcome.Success));
				}
			}

			return Responses.ConvertAll(x => PropertyFilter.Apply(x, Filter));
		}

		/// <summary>
		/// Retrieve information about builds for a specific issue
		/// </summary>
		/// <param name="IssueId">Id of the issue to get information about</param>
		/// <returns>List of matching agents</returns>
		[HttpGet]
		[Route("/ugs/api/issues/{IssueId}/diagnostics")]
		[ProducesResponseType(typeof(List<GetUgsIssueDiagnosticResponse>), 200)]
		public async Task<ActionResult<List<GetUgsIssueDiagnosticResponse>>> GetIssueDiagnosticsAsync(int IssueId)
		{
			List<GetUgsIssueDiagnosticResponse> Diagnostics = new List<GetUgsIssueDiagnosticResponse>();

			Dictionary<LogId, ILogFile?> LogFiles = new Dictionary<LogId, ILogFile?>();

			JobPermissionsCache PermissionsCache = new JobPermissionsCache();

			List<ILogEvent> Events = await IssueService.FindEventsForIssueAsync(IssueId, Count: 10);
			foreach (ILogEvent Event in Events)
			{
				ILogFile? LogFile;
				if(!LogFiles.TryGetValue(Event.LogId, out LogFile))
				{
					LogFile = await LogFileService.GetLogFileAsync(Event.LogId);
					LogFiles.Add(Event.LogId, LogFile);
				}
				if (LogFile != null)
				{
					ILogEventData EventData = await LogFileService.GetEventDataAsync(LogFile, Event.LineIndex, Event.LineCount);
					long BuildId = Event.LogId.GetHashCode();
					Uri Url = new Uri(Settings.DashboardUrl, $"log/{Event.LogId}?lineindex={Event.LineIndex}");

					GetUgsIssueDiagnosticResponse Diagnostic = new GetUgsIssueDiagnosticResponse(BuildId, EventData.Message, Url);
					Diagnostics.Add(Diagnostic);
				}
			}

			return Diagnostics;
		}

		/// <summary>
		/// Gets the URL for a failing step in the
		/// </summary>
		/// <param name="Details">The issue to get a URL for</param>
		/// <param name="bNotify">Whether to show notifications for this issue</param>
		/// <returns>The issue response</returns>
		async Task<GetUgsIssueResponse> CreateIssueResponseAsync(IIssueDetails Details, bool bNotify)
		{
			Uri? BuildUrl = GetIssueBuildUrl(Details);

			IUser? Owner = Details.Issue.OwnerId.HasValue ? await UserCollection.GetCachedUserAsync(Details.Issue.OwnerId.Value) : null;
			IUser? NominatedBy = Details.Issue.NominatedById.HasValue ? await UserCollection.GetCachedUserAsync(Details.Issue.NominatedById.Value) : null;

			return new GetUgsIssueResponse(Details, Owner, NominatedBy, bNotify, BuildUrl);
		}

		/// <summary>
		/// Gets the URL for a failing step in the given issue
		/// </summary>
		/// <param name="Issue">The issue details</param>
		/// <returns>The build URL</returns>
		Uri? GetIssueBuildUrl(IIssueDetails Issue)
		{
			HashSet<ObjectId> UnresolvedSpans = new HashSet<ObjectId>(Issue.Spans.Where(x => x.NextSuccess == null).Select(x => x.Id));

			IIssueStep? Step = Issue.Steps.OrderByDescending(x => UnresolvedSpans.Contains(x.SpanId)).ThenByDescending(x => x.Change).FirstOrDefault();
			if (Step == null)
			{
				return null;
			}

			return new Uri(Settings.DashboardUrl, $"job/{Step.JobId}?step={Step.StepId}");
		}

		/// <summary>
		/// Creates the response for a particular build
		/// </summary>
		/// <param name="Span">Span containing the step</param>
		/// <param name="Step">The step to describe</param>
		/// <param name="Outcome">Outcome of this step</param>
		/// <returns>Response object</returns>
		GetUgsIssueBuildResponse CreateBuildResponse(IIssueSpan Span, IIssueStep Step, IssueBuildOutcome Outcome)
		{
			GetUgsIssueBuildResponse Response = new GetUgsIssueBuildResponse(Span.StreamName, Step.Change, Outcome);
			Response.Id = Step.LogId.GetHashCode();
			Response.JobName = $"{Step.JobName}: {Span.NodeName}";
			Response.JobUrl = new Uri(Settings.DashboardUrl, $"job/{Step.JobId}");
			Response.JobStepName = Span.NodeName;
			Response.JobStepUrl = new Uri(Settings.DashboardUrl, $"job/{Step.JobId}?step={Step.StepId}");
			Response.ErrorUrl = Response.JobStepUrl;
			return Response;
		}

		/// <summary>
		/// Update an issue
		/// </summary>
		/// <param name="IssueId">Id of the issue to get information about</param>
		/// <param name="Request">The update information</param>
		/// <returns>List of matching agents</returns>
		[HttpPut]
		[Route("/ugs/api/issues/{IssueId}")]
		public async Task<ActionResult> UpdateIssueAsync(int IssueId, [FromBody] UpdateUgsIssueRequest Request)
		{
			UserId? NewOwnerId = null;
			if (!String.IsNullOrEmpty(Request.Owner))
			{
				NewOwnerId = (await UserCollection.FindOrAddUserByLoginAsync(Request.Owner))?.Id;
			}

			UserId? NewNominatedById = null;
			if (!String.IsNullOrEmpty(Request.NominatedBy))
			{
				NewNominatedById = (await UserCollection.FindOrAddUserByLoginAsync(Request.NominatedBy))?.Id;
			}

			UserId? NewDeclinedById = null;
			if (!String.IsNullOrEmpty(Request.DeclinedBy))
			{
				NewDeclinedById = (await UserCollection.FindOrAddUserByLoginAsync(Request.DeclinedBy))?.Id;
			}

			UserId? NewResolvedById = null;
			if (!String.IsNullOrEmpty(Request.ResolvedBy))
			{
				NewResolvedById = (await UserCollection.FindOrAddUserByLoginAsync(Request.ResolvedBy))?.Id;
			}
			if (NewResolvedById == null && Request.Resolved.HasValue)
			{
				NewResolvedById = Request.Resolved.Value ? IIssue.ResolvedByUnknownId : UserId.Empty;
			}

			if (!await IssueService.UpdateIssueAsync(IssueId, OwnerId: NewOwnerId, NominatedById: NewNominatedById, Acknowledged: Request.Acknowledged, DeclinedById: NewDeclinedById, FixChange: Request.FixChange, ResolvedById: NewResolvedById))
			{
				return NotFound();
			}
			return Ok();
		}

		/// <summary>
		/// Post information about net core installation. 
		/// </summary>
		[HttpPost]
		[Route("/ugs/api/netcore")]
		public ActionResult<object> PostNetCoreInfo(string? User = null, string? Machine = null, bool NetCore = false)
		{
			Logger.LogInformation("NetCore: User={User}, Machine={Machine}, NetCore={NetCore}", User, Machine, NetCore);
			return new { };
		}
	}
}
