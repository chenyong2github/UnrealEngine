// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Horde.Build.Utilities;
using HordeServer.Api;
using HordeServer.Collections;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Globalization;
using System.Linq;
using System.Security.Claims;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

using Microsoft.Extensions.Logging;
using HordeServer.Notifications;
using OpenTracing.Util;
using OpenTracing;
using HordeServer.Jobs;
using EpicGames.Perforce;

namespace HordeServer.Controllers
{
	using JobId = ObjectId<IJob>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Controller for the /api/v1/jobs endpoing
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class JobsController : HordeControllerBase
	{
		private readonly AclService AclService;
		private readonly IGraphCollection Graphs;
		private readonly IPerforceService Perforce;
		private readonly StreamService StreamService;
		private readonly JobService JobService;
		private readonly ITemplateCollection TemplateCollection;
		private readonly IArtifactCollection ArtifactCollection;
		private readonly IUserCollection UserCollection;
		private readonly INotificationService NotificationService;
		private readonly AgentService AgentService;
		private ILogger<JobsController> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public JobsController(AclService AclService, IGraphCollection Graphs, IPerforceService Perforce, StreamService StreamService, JobService JobService, ITemplateCollection TemplateCollection, IArtifactCollection ArtifactCollection, IUserCollection UserCollection, INotificationService NotificationService, AgentService AgentService, ILogger<JobsController> Logger)
		{
			this.AclService = AclService;
			this.Graphs = Graphs;
			this.Perforce = Perforce;
			this.StreamService = StreamService;
			this.JobService = JobService;
			this.TemplateCollection = TemplateCollection;
			this.ArtifactCollection = ArtifactCollection;
			this.UserCollection = UserCollection;
			this.NotificationService = NotificationService;
			this.AgentService = AgentService;
			this.Logger = Logger;
		}

		/// <summary>
		/// Creates a new job
		/// </summary>
		/// <param name="Create">Properties of the new job</param>
		/// <returns>Id of the new job</returns>
		[HttpPost]
		[Route("/api/v1/jobs")]
		public async Task<ActionResult<CreateJobResponse>> CreateJobAsync([FromBody] CreateJobRequest Create)
		{
			IStream? Stream = await StreamService.GetStreamAsync(new StreamId(Create.StreamId));
			if (Stream == null)
			{
				return NotFound(Create.StreamId);
			}
			if (!await StreamService.AuthorizeAsync(Stream, AclAction.CreateJob, User, null))
			{
				return Forbid(AclAction.CreateJob, Stream.Id);
			}

			// Get the name of the template ref
			TemplateRefId TemplateRefId = new TemplateRefId(Create.TemplateId);

			// Augment the request with template properties
			TemplateRef? TemplateRef;
			if (!Stream.Templates.TryGetValue(TemplateRefId, out TemplateRef))
			{
				return BadRequest($"Invalid {Create.TemplateId} parameter");
			}
			if (!await StreamService.AuthorizeAsync(Stream, TemplateRef, AclAction.CreateJob, User, null))
			{
				return Forbid(AclAction.CreateJob, Stream.Id);
			}

			ITemplate? Template = await TemplateCollection.GetAsync(TemplateRef.Hash);
			if (Template == null)
			{
				return BadRequest("Missing template referenced by {TemplateId}", Create.TemplateId);
			}
			if (!Template.AllowPreflights && Create.PreflightChange > 0)
			{
				return BadRequest("Template {TemplateId} does not allow preflights", Create.TemplateId);
			}

			// Get the name of the new job
			string Name = Create.Name ?? Template.Name;
			if (Create.TemplateId.Equals("stage-to-marketplace", StringComparison.Ordinal) && Create.Arguments != null)
			{
				foreach (string Argument in Create.Arguments)
				{
					const string Prefix = "-set:UserContentItems=";
					if (Argument.StartsWith(Prefix, StringComparison.Ordinal))
					{
						Name += $" - {Argument.Substring(Prefix.Length)}";
						break;
					}
				}
			}

			// Get the priority of the new job
			Priority Priority = Create.Priority ?? Template.Priority ?? Priority.Normal;

			// New groups for the job
			IGraph Graph = await Graphs.AddAsync(Template);

			// Get the change to build
			int Change;
			if (Create.Change.HasValue)
			{
				Change = Create.Change.Value;
			}
			else if (Create.ChangeQuery != null)
			{
				Change = await ExecuteChangeQueryAsync(Stream, new TemplateRefId(Create.ChangeQuery.TemplateId ?? Create.TemplateId), Create.ChangeQuery.Target, Create.ChangeQuery.Outcomes ?? new List<JobStepOutcome> { JobStepOutcome.Success });
			}
			else if (Create.PreflightChange == null && Template.SubmitNewChange != null)
			{
				Change = await Perforce.CreateNewChangeForTemplateAsync(Stream, Template);
			}
			else
			{
				Change = await Perforce.GetLatestChangeAsync(Stream.ClusterName, Stream.Name, null);
			}

			// And get the matching code changelist
			int CodeChange = await Perforce.GetCodeChangeAsync(Stream.ClusterName, Stream.Name, Change);

			// New properties for the job
			List<string> Arguments = Create.Arguments ?? Template.GetDefaultArguments();

			// Special handling for internal graph evaluation
			if (Arguments.Any(x => x.Equals("-InternalParser", StringComparison.OrdinalIgnoreCase)))
			{
				IPerforceConnection? PerforceConnection = await Perforce.GetServiceUserConnection(Stream.ClusterName);
				if (PerforceConnection != null)
				{
					Graph = await Graphs.AddAsync(PerforceConnection, Stream, Change, CodeChange, Arguments);
				}
			}

			// Check the preflight change is valid
			if (Create.PreflightChange != null)
			{
				CheckShelfResult Result = await Perforce.CheckShelfAsync(Stream.ClusterName, Stream.Name, Create.PreflightChange.Value, null);
				switch (Result)
				{
					case CheckShelfResult.Ok:
						break;
					case CheckShelfResult.NoChange:
						return BadRequest(KnownLogEvents.Horde_InvalidPreflight, "CL {Change} does not exist", Create.PreflightChange);
					case CheckShelfResult.NoShelvedFiles:
						return BadRequest(KnownLogEvents.Horde_InvalidPreflight, "CL {Change} does not contain any shelved files", Create.PreflightChange);
					case CheckShelfResult.WrongStream:
						return BadRequest(KnownLogEvents.Horde_InvalidPreflight, "CL {Change} does not contain files in {Stream}", Create.PreflightChange, Stream.Name);
					case CheckShelfResult.MixedStream:
						return BadRequest(KnownLogEvents.Horde_InvalidPreflight, "CL {Change} contains files from multiple streams", Create.PreflightChange);
					default:
						return BadRequest(KnownLogEvents.Horde_InvalidPreflight, "CL {Change} cannot be preflighted ({Result})", Create.PreflightChange, Result);
				}
			}

			bool? UpdateIssues = null;
			if (Template.UpdateIssues)
			{
				UpdateIssues = true;
			}
			else if (Create.UpdateIssues.HasValue)
			{
				UpdateIssues = Create.UpdateIssues.Value;
			}

			// Create the job
			IJob Job = await JobService.CreateJobAsync(null, Stream, TemplateRefId, Template.Id, Graph, Name, Change, CodeChange, Create.PreflightChange, null, User.GetUserId(), Priority, Create.AutoSubmit, UpdateIssues, TemplateRef.ChainedJobs, TemplateRef.ShowUgsBadges, TemplateRef.ShowUgsAlerts, TemplateRef.NotificationChannel, TemplateRef.NotificationChannelFilter, Arguments);
			await UpdateNotificationsAsync(Job.Id, new UpdateNotificationsRequest { Slack = true });
			return new CreateJobResponse(Job.Id.ToString());
		}

		/// <summary>
		/// Evaluate a change query to determine which CL to run a job at
		/// </summary>
		/// <param name="Stream"></param>
		/// <param name="TemplateId"></param>
		/// <param name="Target"></param>
		/// <param name="Outcomes"></param>
		/// <returns></returns>
		async Task<int> ExecuteChangeQueryAsync(IStream Stream, TemplateRefId TemplateId, string? Target, List<JobStepOutcome> Outcomes)
		{
			IList<IJob> Jobs = await JobService.FindJobsAsync(StreamId: Stream.Id, Templates: new[] { TemplateId }, Target: Target, State: new[] { JobStepState.Completed }, Outcome: Outcomes.ToArray(), Count: 1, ExcludeUserJobs: true);
			if (Jobs.Count == 0)
			{
				Logger.LogInformation("Unable to find successful build of {TemplateId} target {Target}. Using latest change instead", TemplateId, Target);
				return await Perforce.GetLatestChangeAsync(Stream.ClusterName, Stream.Name, null);
			}
			else
			{
				Logger.LogInformation("Last successful build of {TemplateId} target {Target} was job {JobId} at change {Change}", TemplateId, Target, Jobs[0].Id, Jobs[0].Change);
				return Jobs[0].Change;
			}
		}

		/// <summary>
		/// Deletes a specific job.
		/// </summary>
		/// <param name="JobId">Id of the job to delete</param>
		/// <returns>Async task</returns>
		[HttpDelete]
		[Route("/api/v1/jobs/{JobId}")]
		public async Task<ActionResult> DeleteJobAsync(JobId JobId)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.DeleteJob, User, null))
			{
				return Forbid(AclAction.DeleteJob, JobId);
			}
			if (!await JobService.DeleteJobAsync(Job))
			{
				return NotFound(JobId);
			}
			return Ok();
		}

		/// <summary>
		/// Updates a specific job.
		/// </summary>
		/// <param name="JobId">Id of the job to find</param>
		/// <param name="Request">Settings to update in the job</param>
		/// <returns>Async task</returns>
		[HttpPut]
		[Route("/api/v1/jobs/{JobId}")]
		public async Task<ActionResult> UpdateJobAsync(JobId JobId, [FromBody] UpdateJobRequest Request)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}

			StreamPermissionsCache PermissionsCache = new StreamPermissionsCache();
			if (!await JobService.AuthorizeAsync(Job, AclAction.UpdateJob, User, PermissionsCache))
			{
				return Forbid(AclAction.UpdateJob, JobId);
			}
			if (Request.Acl != null && !await JobService.AuthorizeAsync(Job, AclAction.ChangePermissions, User, PermissionsCache))
			{
				return Forbid(AclAction.ChangePermissions, JobId);
			}

			// Convert legacy behavior of clearing out the argument to setting the aborted flag
			if (Request.Arguments != null && Request.Arguments.Count == 0)
			{
				Request.Aborted = true;
				Request.Arguments = null;
			}

			UserId? AbortedByUserId = null;
			if (Request.Aborted ?? false)
			{
				AbortedByUserId = User.GetUserId();
			}

			IJob? NewJob = await JobService.UpdateJobAsync(Job, Name: Request.Name, Priority: Request.Priority, AutoSubmit: Request.AutoSubmit, AbortedByUserId: AbortedByUserId, Arguments: Request.Arguments);
			if (NewJob == null)
			{
				return NotFound(JobId);
			}
			return Ok();
		}

		/// <summary>
		/// Updates notifications for a specific job.
		/// </summary>
		/// <param name="JobId">Id of the job to find</param>
		/// <param name="Request">The notification request</param>
		/// <returns>Information about the requested job</returns>
		[HttpPut]
		[Route("/api/v1/jobs/{JobId}/notifications")]
		public async Task<ActionResult> UpdateNotificationsAsync(JobId JobId, [FromBody] UpdateNotificationsRequest Request)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.CreateSubscription, User, null))
			{
				return Forbid(AclAction.CreateSubscription, JobId);
			}

			ObjectId TriggerId = Job.NotificationTriggerId ?? ObjectId.GenerateNewId();

			Job = await JobService.UpdateJobAsync(Job, null, null, null, null, TriggerId, null, null);
			if (Job == null)
			{
				return NotFound(JobId);
			}

			await NotificationService.UpdateSubscriptionsAsync(TriggerId, User, Request.Email, Request.Slack);
			return Ok();
		}

		/// <summary>
		/// Gets information about a specific job.
		/// </summary>
		/// <param name="JobId">Id of the job to find</param>
		/// <returns>Information about the requested job</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/notifications")]
		public async Task<ActionResult<GetNotificationResponse>> GetNotificationsAsync(JobId JobId)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.CreateSubscription, User, null))
			{
				return Forbid(AclAction.CreateSubscription, JobId);
			}

			INotificationSubscription? Subscription;
			if (Job.NotificationTriggerId == null)
			{
				Subscription = null;
			}
			else
			{
				Subscription = await NotificationService.GetSubscriptionsAsync(Job.NotificationTriggerId.Value, User);
			}
			return new GetNotificationResponse(Subscription);
		}

		/// <summary>
		/// Gets information about a specific job.
		/// </summary>
		/// <param name="JobId">Id of the job to find</param>
		/// <param name="ModifiedAfter">If specified, returns an empty response unless the job's update time is equal to or less than the given value</param>
		/// <param name="Filter">Filter for the fields to return</param>
		/// <returns>Information about the requested job</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}")]
		[ProducesResponseType(typeof(GetJobResponse), 200)]
		public async Task<ActionResult<object>> GetJobAsync(JobId JobId, [FromQuery] DateTimeOffset? ModifiedAfter = null, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}

			StreamPermissionsCache Cache = new StreamPermissionsCache();
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, Cache))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}
			if (ModifiedAfter != null && Job.UpdateTimeUtc <= ModifiedAfter.Value)
			{
				return new Dictionary<string, object>();
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			bool bIncludeAcl = await JobService.AuthorizeAsync(Job, AclAction.ViewPermissions, User, Cache);
			bool bIncludeCosts = await JobService.AuthorizeAsync(Job, AclAction.ViewCosts, User, Cache);
			return await CreateJobResponseAsync(Job, Graph, bIncludeAcl, bIncludeCosts, Filter);
		}

		/// <summary>
		/// Creates a response containing information about this object
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <param name="Graph">The graph for this job</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response</param>
		/// <param name="bIncludeCosts">Whether to include costs in the response</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Object containing the requested properties</returns>
		async Task<object> CreateJobResponseAsync(IJob Job, IGraph Graph, bool bIncludeAcl, bool bIncludeCosts, PropertyFilter? Filter)
		{
			if (Filter == null)
			{
				return await CreateJobResponseAsync(Job, Graph, true, true, bIncludeAcl, bIncludeCosts);
			}
			else
			{
				return Filter.ApplyTo(await CreateJobResponseAsync(Job, Graph, Filter.Includes(nameof(GetJobResponse.Batches)), Filter.Includes(nameof(GetJobResponse.Labels)) || Filter.Includes(nameof(GetJobResponse.DefaultLabel)), bIncludeAcl, bIncludeCosts));
			}
		}

		/// <summary>
		/// Creates a response containing information about this object
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <param name="Graph">The graph definition</param>
		/// <param name="bIncludeBatches">Whether to include the job batches in the response</param>
		/// <param name="bIncludeLabels">Whether to include the job aggregates in the response</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response</param>
		/// <param name="bIncludeCosts">Whether to include costs of running particular agents</param>
		/// <returns>The response object</returns>
		async ValueTask<GetJobResponse> CreateJobResponseAsync(IJob Job, IGraph Graph, bool bIncludeBatches, bool bIncludeLabels, bool bIncludeAcl, bool bIncludeCosts)
		{
			GetThinUserInfoResponse? StartedByUserInfo = null;
			if (Job.StartedByUserId != null)
			{
				StartedByUserInfo = new GetThinUserInfoResponse(await UserCollection.GetCachedUserAsync(Job.StartedByUserId.Value));
			}

			GetThinUserInfoResponse? AbortedByUserInfo = null;
			if (Job.AbortedByUserId != null)
			{
				AbortedByUserInfo = new GetThinUserInfoResponse(await UserCollection.GetCachedUserAsync(Job.AbortedByUserId.Value));
			}

			GetAclResponse? AclResponse = null;
			if (bIncludeAcl && Job.Acl != null)
			{
				AclResponse = new GetAclResponse(Job.Acl);
			}

			GetJobResponse Response = new GetJobResponse(Job, StartedByUserInfo, AbortedByUserInfo, AclResponse);
			if (bIncludeBatches || bIncludeLabels)
			{
				if (bIncludeBatches)
				{
					Response.Batches = new List<GetBatchResponse>();
					foreach (IJobStepBatch Batch in Job.Batches)
					{
						Response.Batches.Add(await CreateBatchResponseAsync(Batch, bIncludeCosts));
					}
				}
				if (bIncludeLabels)
				{
					Response.Labels = new List<GetLabelStateResponse>();
					Response.DefaultLabel = Job.GetLabelStateResponses(Graph, Response.Labels);
				}
			}
			return Response;
		}

		/// <summary>
		/// Get the response object for a batch
		/// </summary>
		/// <param name="Batch"></param>
		/// <param name="bIncludeCosts"></param>
		/// <returns></returns>
		async ValueTask<GetBatchResponse> CreateBatchResponseAsync(IJobStepBatch Batch, bool bIncludeCosts)
		{
			List<GetStepResponse> Steps = new List<GetStepResponse>();
			foreach (IJobStep Step in Batch.Steps)
			{
				Steps.Add(await CreateStepResponseAsync(Step));
			}

			double? AgentRate = null;
			if (Batch.AgentId != null && bIncludeCosts)
			{
				AgentRate = await AgentService.GetRateAsync(Batch.AgentId.Value);
			}

			return new GetBatchResponse(Batch, Steps, AgentRate);
		}

		/// <summary>
		/// Get the response object for a step
		/// </summary>
		/// <param name="Step"></param>
		/// <returns></returns>
		async ValueTask<GetStepResponse> CreateStepResponseAsync(IJobStep Step)
		{
			GetThinUserInfoResponse? AbortedByUserInfo = null;
			if (Step.AbortedByUserId != null)
			{
				AbortedByUserInfo = new GetThinUserInfoResponse(await UserCollection.GetCachedUserAsync(Step.AbortedByUserId.Value));
			}

			GetThinUserInfoResponse? RetriedByUserInfo = null;
			if (Step.RetriedByUserId != null)
			{
				RetriedByUserInfo = new GetThinUserInfoResponse(await UserCollection.GetCachedUserAsync(Step.RetriedByUserId.Value));
			}

			return new GetStepResponse(Step, AbortedByUserInfo, RetriedByUserInfo);
		}

		/// <summary>
		/// Gets information about the graph for a specific job.
		/// </summary>
		/// <param name="JobId">Id of the job to find</param>
		/// <param name="Filter">Filter for the fields to return</param>
		/// <returns>Information about the requested job</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/graph")]
		[ProducesResponseType(typeof(GetGraphResponse), 200)]
		public async Task<ActionResult<object>> GetJobGraphAsync(JobId JobId, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid(AclAction.ViewJob, Job.Id);
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			return PropertyFilter.Apply(new GetGraphResponse(Graph), Filter);
		}

		/// <summary>
		/// Gets timing information about the graph for a specific job.
		/// </summary>
		/// <param name="JobId">Id of the job to find</param>
		/// <param name="Filter">Filter for the fields to return</param>
		/// <returns>Information about the requested job</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/timing")]
		[ProducesResponseType(typeof(GetJobTimingResponse), 200)]
		public async Task<ActionResult<object>> GetJobTimingAsync(JobId JobId, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}

			IJobTiming JobTiming = await JobService.GetJobTimingAsync(Job);
			IGraph Graph = await JobService.GetGraphAsync(Job);
			return PropertyFilter.Apply(await CreateJobTimingResponse(Job, Graph, JobTiming), Filter);
		}

		private async Task<GetJobTimingResponse> CreateJobTimingResponse(IJob Job, IGraph Graph, IJobTiming JobTiming, bool IncludeJobResponse = false)
		{
			Dictionary<INode, TimingInfo> NodeToTimingInfo = Job.GetTimingInfo(Graph, JobTiming);

			Dictionary<string, GetStepTimingInfoResponse> Steps = new Dictionary<string, GetStepTimingInfoResponse>();
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				foreach (IJobStep Step in Batch.Steps)
				{
					INode Node = Graph.Groups[Batch.GroupIdx].Nodes[Step.NodeIdx];
					Steps[Step.Id.ToString()] = new GetStepTimingInfoResponse(Node.Name, NodeToTimingInfo[Node]);
				}
			}

			List<GetLabelTimingInfoResponse> Labels = new List<GetLabelTimingInfoResponse>();
			foreach (ILabel Label in Graph.Labels)
			{
				TimingInfo TimingInfo = TimingInfo.Max(Label.GetDependencies(Graph.Groups).Select(x => NodeToTimingInfo[x]));
				Labels.Add(new GetLabelTimingInfoResponse(Label, TimingInfo));
			}

			GetJobResponse? JobResponse = null;
			if (IncludeJobResponse)
			{
				JobResponse = await CreateJobResponseAsync(Job, Graph, true, true, false, true);
			}

			return new GetJobTimingResponse(Job, JobResponse, Steps, Labels);
		}
		
		/// <summary>
		/// Find timing information about the graph for multiple jobs
		/// </summary>
		/// <param name="StreamId">The stream to search in</param>
		/// <param name="Templates">List of templates to find</param>
		/// <param name="Filter">Filter for the fields to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>Job timings for each job ID</returns>
		[HttpGet]
		[Route("/api/v1/jobs/timing")]
		[ProducesResponseType(typeof(FindJobTimingsResponse), 200)]
		public async Task<ActionResult<object>> FindJobTimingsAsync(
			[FromQuery] string? StreamId = null,
			[FromQuery(Name = "template")] string[]? Templates = null,
			[FromQuery] PropertyFilter? Filter = null,
			[FromQuery] int Count = 100)
		{
			if (StreamId == null)
			{
				return BadRequest("Missing/invalid query parameter streamId");
			}

			TemplateRefId[] TemplateRefIds = Templates switch
			{
				{ Length: > 0 } => Templates.Select(x => new TemplateRefId(x)).ToArray(),
				_ => Array.Empty<TemplateRefId>()
			};

			List<IJob> Jobs = await JobService.FindJobsByStreamWithTemplatesAsync(new StreamId(StreamId), TemplateRefIds, Count: Count, ConsistentRead: false);

			Dictionary<string, GetJobTimingResponse> JobTimings = await Jobs.ToAsyncEnumerable()
				.WhereAwait(async Job => await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
				.ToDictionaryAwaitAsync(x => ValueTask.FromResult(x.Id.ToString()), async Job =>
				{
					IJobTiming JobTiming = await JobService.GetJobTimingAsync(Job);
					IGraph Graph = await JobService.GetGraphAsync(Job);
					return await CreateJobTimingResponse(Job, Graph, JobTiming, true);
				});
			
			return PropertyFilter.Apply(new FindJobTimingsResponse(JobTimings), Filter);
		}

		/// <summary>
		/// Gets information about the template for a specific job.
		/// </summary>
		/// <param name="JobId">Id of the job to find</param>
		/// <param name="Filter">Filter for the fields to return</param>
		/// <returns>Information about the requested job</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/template")]
		[ProducesResponseType(typeof(GetTemplateResponse), 200)]
		public async Task<ActionResult<object>> GetJobTemplateAsync(JobId JobId, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null || Job.TemplateHash == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}

			ITemplate? Template = await TemplateCollection.GetAsync(Job.TemplateHash);
			if(Template == null)
			{
				return NotFound(Job.StreamId, Job.TemplateId);
			}

			return new GetTemplateResponse(Template).ApplyFilter(Filter);
		}

		/// <summary>
		/// Find jobs matching a criteria
		/// </summary>
		/// <param name="Ids">The job ids to return</param>
		/// <param name="Name">Name of the job to find</param>
		/// <param name="Templates">List of templates to find</param>
		/// <param name="StreamId">The stream to search for</param>
		/// <param name="MinChange">The minimum changelist number</param>
		/// <param name="MaxChange">The maximum changelist number</param>
		/// <param name="IncludePreflight">Whether to include preflight jobs</param>
		/// <param name="PreflightOnly">Whether to only include preflight jobs</param>
		/// <param name="PreflightChange">The preflighted changelist</param>
		/// <param name="StartedByUserId">User id for which to include jobs</param>
		/// <param name="PreflightStartedByUserId">User id for which to include preflight jobs</param>
		/// <param name="MinCreateTime">Minimum creation time</param>
		/// <param name="MaxCreateTime">Maximum creation time</param>
		/// <param name="ModifiedBefore">If specified, only jobs updated before the give time will be returned</param>
		/// <param name="ModifiedAfter">If specified, only jobs updated after the give time will be returned</param>
		/// <param name="Target">Target to filter the returned jobs by</param>
		/// <param name="State">Filter state of the returned jobs</param>
		/// <param name="Outcome">Filter outcome of the returned jobs</param>
		/// <param name="Filter">Filter for properties to return</param>
		/// <param name="Index">Index of the first result to be returned</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of jobs</returns>
		[HttpGet]
		[Route("/api/v1/jobs")]
		[ProducesResponseType(typeof(List<GetJobResponse>), 200)]
		public async Task<ActionResult<List<object>>> FindJobsAsync(
			[FromQuery(Name = "Id")] string[]? Ids = null,
			[FromQuery] string? StreamId = null,
			[FromQuery] string? Name = null,
			[FromQuery(Name = "template")] string[]? Templates = null,
			[FromQuery] int? MinChange = null,
			[FromQuery] int? MaxChange = null,
			[FromQuery] bool IncludePreflight = true,
			[FromQuery] bool? PreflightOnly = null,
			[FromQuery] int? PreflightChange = null,
			[FromQuery] string? PreflightStartedByUserId = null,
			[FromQuery] string? StartedByUserId = null,
			[FromQuery] DateTimeOffset? MinCreateTime = null,
			[FromQuery] DateTimeOffset? MaxCreateTime = null,
			[FromQuery] DateTimeOffset? ModifiedBefore = null,
			[FromQuery] DateTimeOffset? ModifiedAfter = null,
			[FromQuery] string? Target = null,
			[FromQuery] JobStepState[]? State = null,
			[FromQuery] JobStepOutcome[]? Outcome = null, 
			[FromQuery] PropertyFilter? Filter = null,
			[FromQuery] int Index = 0,
			[FromQuery] int Count = 100)
		{
			JobId[]? JobIdValues = (Ids == null) ? (JobId[]?)null : Array.ConvertAll(Ids, x => new JobId(x));
			StreamId? StreamIdValue = (StreamId == null)? (StreamId?)null : new StreamId(StreamId);
			
			TemplateRefId[]? TemplateRefIds = (Templates != null && Templates.Length > 0) ? Templates.Select(x => new TemplateRefId(x)).ToArray() : null;

			if (IncludePreflight == false)
			{
				PreflightChange = 0;
			}

			UserId? PreflightStartedByUserIdValue = null;

			if (PreflightStartedByUserId != null)
			{
				PreflightStartedByUserIdValue = new UserId(PreflightStartedByUserId);
			}

			UserId? StartedByUserIdValue = null;

			if (StartedByUserId != null)
			{
				StartedByUserIdValue = new UserId(StartedByUserId);
			}

			List<IJob> Jobs;
			using (IScope _ = GlobalTracer.Instance.BuildSpan("FindJobs").StartActive())
			{
				Jobs = await JobService.FindJobsAsync(JobIdValues, StreamIdValue, Name, TemplateRefIds, MinChange,
					MaxChange, PreflightChange, PreflightOnly, PreflightStartedByUserIdValue, StartedByUserIdValue, MinCreateTime?.UtcDateTime, MaxCreateTime?.UtcDateTime, Target, State, Outcome,
					ModifiedBefore, ModifiedAfter, Index, Count, false);
			}

			StreamPermissionsCache PermissionsCache = new StreamPermissionsCache();

			List<object> Responses = new List<object>();
			foreach (IJob Job in Jobs)
			{
				using IScope JobScope = GlobalTracer.Instance.BuildSpan("JobIteration").StartActive();
				JobScope.Span.SetTag("jobId", Job.Id.ToString());

				if (Job.GraphHash == null)
				{
					Logger.LogWarning("Job {JobId} has no GraphHash", Job.Id);
					continue;
				}

				bool ViewJobAuthorized;
				using (IScope _ = GlobalTracer.Instance.BuildSpan("AuthorizeViewJob").StartActive())
				{
					ViewJobAuthorized = await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, PermissionsCache);
				}
				
				if (ViewJobAuthorized)
				{
					IGraph Graph;
					using (IScope _ = GlobalTracer.Instance.BuildSpan("GetGraph").StartActive())
					{
						Graph = await JobService.GetGraphAsync(Job);
					}

					bool bIncludeAcl;
					using (IScope _ = GlobalTracer.Instance.BuildSpan("AuthorizeViewPermissions").StartActive())
					{
						bIncludeAcl = await JobService.AuthorizeAsync(Job, AclAction.ViewPermissions, User, PermissionsCache);
					}

					bool bIncludeCosts;
					using (IScope _ = GlobalTracer.Instance.BuildSpan("AuthorizeViewCosts").StartActive())
					{
						bIncludeCosts = await JobService.AuthorizeAsync(Job, AclAction.ViewCosts, User, PermissionsCache);
					}

					using (IScope _ = GlobalTracer.Instance.BuildSpan("CreateResponse").StartActive())
					{
						Responses.Add(await CreateJobResponseAsync(Job, Graph, bIncludeAcl, bIncludeCosts, Filter));
					}
				}
			}
			return Responses;
		}

		/// <summary>
		/// Find jobs for a stream with given templates, sorted by creation date
		/// </summary>
		/// <param name="StreamId">The stream to search for</param>
		/// <param name="Templates">List of templates to find</param>
		/// <param name="PreflightStartedByUserId">User id for which to include preflight jobs</param>
		/// <param name="MaxCreateTime">Maximum creation time</param>
		/// <param name="ModifiedAfter">If specified, only jobs updated after the given time will be returned</param>
		/// <param name="Filter">Filter for properties to return</param>
		/// <param name="Index">Index of the first result to be returned</param>
		/// <param name="Count">Number of results to return</param>
		/// <param name="ConsistentRead">If a read to the primary database is required, for read consistency. Usually not required.</param>
		/// <returns>List of jobs</returns>
		[HttpGet]
		[Route("/api/v1/jobs/streams/{StreamId}")]
		[ProducesResponseType(typeof(List<GetJobResponse>), 200)]
		public async Task<ActionResult<List<object>>> FindJobsByStreamWithTemplatesAsync(
			string StreamId,
			[FromQuery(Name = "template")] string[] Templates,
			[FromQuery] string? PreflightStartedByUserId = null,
			[FromQuery] DateTimeOffset? MaxCreateTime = null,
			[FromQuery] DateTimeOffset? ModifiedAfter = null,
			[FromQuery] PropertyFilter? Filter = null,
			[FromQuery] int Index = 0,
			[FromQuery] int Count = 100,
			[FromQuery] bool ConsistentRead = false)
		{
			StreamId StreamIdValue = new StreamId(StreamId);
			TemplateRefId[] TemplateRefIds = Templates.Select(x => new TemplateRefId(x)).ToArray();
			UserId? PreflightStartedByUserIdValue = PreflightStartedByUserId != null ? new UserId(PreflightStartedByUserId) : null;
			Count = Math.Min(1000, Count);

			List<IJob> Jobs = await JobService.FindJobsByStreamWithTemplatesAsync(StreamIdValue, TemplateRefIds, PreflightStartedByUserIdValue, MaxCreateTime, ModifiedAfter, Index, Count, ConsistentRead);
			return await CreateAuthorizedJobResponses(Jobs, Filter);
		}

		private async Task<List<object>> CreateAuthorizedJobResponses(List<IJob> Jobs, PropertyFilter? Filter = null)
		{
			StreamPermissionsCache PermissionsCache = new ();
			List<object> Responses = new ();
			foreach (IJob Job in Jobs)
			{
				using IScope JobScope = GlobalTracer.Instance.BuildSpan("JobIteration").StartActive();
				JobScope.Span.SetTag("jobId", Job.Id.ToString());
				
				bool ViewJobAuthorized;
				using (IScope _ = GlobalTracer.Instance.BuildSpan("AuthorizeViewJob").StartActive())
				{
					ViewJobAuthorized = await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, PermissionsCache);
				}
				
				if (ViewJobAuthorized)
				{
					IGraph Graph;
					using (IScope _ = GlobalTracer.Instance.BuildSpan("GetGraph").StartActive())
					{
						Graph = await JobService.GetGraphAsync(Job);
					}

					bool bIncludeAcl;
					using (IScope _ = GlobalTracer.Instance.BuildSpan("AuthorizeViewPermissions").StartActive())
					{
						bIncludeAcl = await JobService.AuthorizeAsync(Job, AclAction.ViewPermissions, User, PermissionsCache);
					}

					bool bIncludeCosts;
					using (IScope _ = GlobalTracer.Instance.BuildSpan("AuthorizeViewCosts").StartActive())
					{
						bIncludeCosts = await JobService.AuthorizeAsync(Job, AclAction.ViewCosts, User, PermissionsCache);
					}

					using (IScope _ = GlobalTracer.Instance.BuildSpan("CreateResponse").StartActive())
					{
						Responses.Add(await CreateJobResponseAsync(Job, Graph, bIncludeAcl, bIncludeCosts, Filter));
					}
				}
			}

			
			return Responses;
		}

		/// <summary>
		/// Adds an array of nodes to be executed for a job
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="Requests">Properties of the new nodes</param>
		/// <returns>Id of the new job</returns>
		[HttpPost]
		[Route("/api/v1/jobs/{JobId}/groups")]
		public async Task<ActionResult> CreateGroupsAsync(JobId JobId, [FromBody] List<NewGroup> Requests)
		{
			Dictionary<string, int> ExpectedDurationCache = new Dictionary<string, int>();

			for (; ; )
			{
				IJob? Job = await JobService.GetJobAsync(JobId);
				if (Job == null)
				{
					return NotFound(JobId);
				}
				if (!await JobService.AuthorizeAsync(Job, AclAction.ExecuteJob, User, null))
				{
					return Forbid(AclAction.ExecuteJob, JobId);
				}

				IGraph Graph = await JobService.GetGraphAsync(Job);
				Graph = await Graphs.AppendAsync(Graph, Requests, null, null);

				IJob? NewJob = await JobService.TryUpdateGraphAsync(Job, Graph);
				if (NewJob != null)
				{
					return Ok();
				}
			}
		}

		/// <summary>
		/// Gets the nodes to be executed for a job
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of nodes to be executed</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/groups")]
		[ProducesResponseType(typeof(List<GetGroupResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetGroupsAsync(JobId JobId, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			return Graph.Groups.ConvertAll(x => new GetGroupResponse(x, Graph.Groups).ApplyFilter(Filter));
		}

		/// <summary>
		/// Gets the nodes in a group to be executed for a job
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="GroupIdx">The group index</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of nodes to be executed</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/groups/{GroupIdx}")]
		[ProducesResponseType(typeof(GetGroupResponse), 200)]
		public async Task<ActionResult<object>> GetGroupAsync(JobId JobId, int GroupIdx, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			if (GroupIdx < 0 || GroupIdx >= Graph.Groups.Count)
			{
				return NotFound(JobId, GroupIdx);
			}

			return new GetGroupResponse(Graph.Groups[GroupIdx], Graph.Groups).ApplyFilter(Filter);
		}

		/// <summary>
		/// Gets the nodes for a particular group
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="GroupIdx">Index of the group containing the node to update</param>
		/// <param name="Filter">Filter for the properties to return</param>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/groups/{GroupIdx}/nodes")]
		[ProducesResponseType(typeof(List<GetNodeResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetNodesAsync(JobId JobId, int GroupIdx, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			if (GroupIdx < 0 || GroupIdx >= Graph.Groups.Count)
			{
				return NotFound(JobId, GroupIdx);
			}

			return Graph.Groups[GroupIdx].Nodes.ConvertAll(x => new GetNodeResponse(x, Graph.Groups).ApplyFilter(Filter));
		}

		/// <summary>
		/// Gets a particular node definition
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="GroupIdx">Index of the group containing the node to update</param>
		/// <param name="NodeIdx">Index of the node to update</param>
		/// <param name="Filter">Filter for the properties to return</param>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/groups/{GroupIdx}/nodes/{NodeIdx}")]
		[ProducesResponseType(typeof(GetNodeResponse), 200)]
		public async Task<ActionResult<object>> GetNodeAsync(JobId JobId, int GroupIdx, int NodeIdx, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			if (GroupIdx < 0 || GroupIdx >= Graph.Groups.Count)
			{
				return NotFound(JobId, GroupIdx);
			}
			if(NodeIdx < 0 || NodeIdx >= Graph.Groups[GroupIdx].Nodes.Count)
			{
				return NotFound(JobId, GroupIdx, NodeIdx);
			}

			return new GetNodeResponse(Graph.Groups[GroupIdx].Nodes[NodeIdx], Graph.Groups).ApplyFilter(Filter);
		}

		/// <summary>
		/// Gets the steps currently scheduled to be executed for a job
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of nodes to be executed</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/batches")]
		[ProducesResponseType(typeof(List<GetBatchResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetBatchesAsync(JobId JobId, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}

			StreamPermissionsCache Cache = new StreamPermissionsCache();
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, Cache))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}

			bool bIncludeCosts = await JobService.AuthorizeAsync(Job, AclAction.ViewCosts, User, Cache);

			List<object> Responses = new List<object>();
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				GetBatchResponse Response = await CreateBatchResponseAsync(Batch, bIncludeCosts);
				Responses.Add(Response.ApplyFilter(Filter));
			}
			return Responses;
		}

		/// <summary>
		/// Updates the state of a jobstep
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="BatchId">Unique id for the step</param>
		/// <param name="Request">Updates to apply to the node</param>
		[HttpPut]
		[Route("/api/v1/jobs/{JobId}/batches/{BatchId}")]
		public async Task<ActionResult> UpdateBatchAsync(JobId JobId, SubResourceId BatchId, [FromBody] UpdateBatchRequest Request)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}

			IJobStepBatch? Batch = Job.Batches.FirstOrDefault(x => x.Id == BatchId);
			if (Batch == null)
			{
				return NotFound(JobId, BatchId);
			}
			if (Batch.SessionId == null || !User.HasSessionClaim(Batch.SessionId.Value))
			{
				return Forbid("Missing session claim for job {JobId} batch {BatchId}", JobId, BatchId);
			}

			IJob? NewJob = await JobService.UpdateBatchAsync(Job, BatchId, Request.LogId?.ToObjectId<ILogFile>(), Request.State);
			if (NewJob == null)
			{
				return NotFound(JobId);
			}
			return Ok();
		}

		/// <summary>
		/// Gets a particular step currently scheduled to be executed for a job
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="BatchId">Unique id for the step</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of nodes to be executed</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/batches/{BatchId}")]
		[ProducesResponseType(typeof(GetBatchResponse), 200)]
		public async Task<ActionResult<object>> GetBatchAsync(JobId JobId, SubResourceId BatchId, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}

			StreamPermissionsCache Cache = new StreamPermissionsCache();
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, Cache))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}

			bool bIncludeCosts = await JobService.AuthorizeAsync(Job, AclAction.ViewCosts, User, Cache);

			IGraph Graph = await JobService.GetGraphAsync(Job);
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				if (Batch.Id == BatchId)
				{
					GetBatchResponse Response = await CreateBatchResponseAsync(Batch, bIncludeCosts);
					return Response.ApplyFilter(Filter);
				}
			}

			return NotFound(JobId, BatchId);
		}

		/// <summary>
		/// Gets the steps currently scheduled to be executed for a job
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="BatchId">Unique id for the batch</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of nodes to be executed</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/batches/{BatchId}/steps")]
		[ProducesResponseType(typeof(List<GetStepResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetStepsAsync(JobId JobId, SubResourceId BatchId, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				if (Batch.Id == BatchId)
				{
					List<object> Responses = new List<object>();
					foreach (IJobStep Step in Batch.Steps)
					{
						GetStepResponse Response = await CreateStepResponseAsync(Step);
						Responses.Add(Response.ApplyFilter(Filter));
					}
					return Responses;
				}
			}

			return NotFound(JobId, BatchId);
		}

		/// <summary>
		/// Updates the state of a jobstep
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="BatchId">Unique id for the batch</param>
		/// <param name="StepId">Unique id for the step</param>
		/// <param name="Request">Updates to apply to the node</param>
		[HttpPut]
		[Route("/api/v1/jobs/{JobId}/batches/{BatchId}/steps/{StepId}")]
		public async Task<ActionResult<UpdateStepResponse>> UpdateStepAsync(JobId JobId, SubResourceId BatchId, SubResourceId StepId, [FromBody] UpdateStepRequest Request)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}

			// Check permissions for updating this step. Only the agent executing the step can modify the state of it.
			if (Request.State != JobStepState.Unspecified || Request.Outcome != JobStepOutcome.Unspecified)
			{
				IJobStepBatch? Batch = Job.Batches.FirstOrDefault(x => x.Id == BatchId);
				if (Batch == null)
				{
					return NotFound(JobId, BatchId);
				}
				if (!Batch.SessionId.HasValue || !User.HasSessionClaim(Batch.SessionId.Value))
				{
					return Forbid();
				}
			}
			if (Request.Retry != null || Request.Priority != null)
			{
				if (!await JobService.AuthorizeAsync(Job, AclAction.RetryJobStep, User, null))
				{
					return Forbid(AclAction.RetryJobStep, JobId);
				}
			}
			if (Request.Properties != null)
			{
				if (!await JobService.AuthorizeAsync(Job, AclAction.UpdateJob, User, null))
				{
					return Forbid(AclAction.UpdateJob, JobId);
				}
			}

			UserId? RetryByUser = (Request.Retry.HasValue && Request.Retry.Value) ? User.GetUserId() : null;
			UserId? AbortByUser = (Request.AbortRequested.HasValue && Request.AbortRequested.Value) ? User.GetUserId() : null;

			try
			{
				IJob? NewJob = await JobService.UpdateStepAsync(Job, BatchId, StepId, Request.State, Request.Outcome, Request.AbortRequested, AbortByUser, Request.LogId?.ToObjectId<ILogFile>(), null, RetryByUser, Request.Priority, null, Request.Properties);
				if (NewJob == null)
				{
					return NotFound(JobId);
				}

				UpdateStepResponse Response = new UpdateStepResponse();
				if (Request.Retry ?? false)
				{
					JobStepRefId? RetriedStepId = FindRetriedStep(Job, BatchId, StepId);
					if (RetriedStepId != null)
					{
						Response.BatchId = RetriedStepId.Value.BatchId.ToString();
						Response.StepId = RetriedStepId.Value.StepId.ToString();
					}
				}
				return Response;
			}
			catch (RetryNotAllowedException Ex)
			{
				return BadRequest(Ex.Message);
			}
		}

		/// <summary>
		/// Find the first retried step after the given step
		/// </summary>
		/// <param name="Job">The job being run</param>
		/// <param name="BatchId">Batch id of the last step instance</param>
		/// <param name="StepId">Step id of the last instance</param>
		/// <returns>The retried step information</returns>
		static JobStepRefId? FindRetriedStep(IJob Job, SubResourceId BatchId, SubResourceId StepId)
		{
			NodeRef? LastNodeRef = null;
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				if ((LastNodeRef == null && Batch.Id == BatchId) || (LastNodeRef != null && Batch.GroupIdx == LastNodeRef.GroupIdx))
				{
					foreach (IJobStep Step in Batch.Steps)
					{
						if (LastNodeRef == null && Step.Id == StepId)
						{
							LastNodeRef = new NodeRef(Batch.GroupIdx, Step.NodeIdx);
						}
						else if (LastNodeRef != null && Step.NodeIdx == LastNodeRef.NodeIdx)
						{
							return new JobStepRefId(Job.Id, Batch.Id, Step.Id);
						}
					}
				}
			}
			return null;
		}

		/// <summary>
		/// Gets a particular step currently scheduled to be executed for a job
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="BatchId">Unique id for the batch</param>
		/// <param name="StepId">Unique id for the step</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of nodes to be executed</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/batches/{BatchId}/steps/{StepId}")]
		[ProducesResponseType(typeof(GetStepResponse), 200)]
		public async Task<ActionResult<object>> GetStepAsync(JobId JobId, SubResourceId BatchId, SubResourceId StepId, [FromQuery] PropertyFilter? Filter = null)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}

			foreach (IJobStepBatch Batch in Job.Batches)
			{
				if (Batch.Id == BatchId)
				{
					foreach (IJobStep Step in Batch.Steps)
					{
						if (Step.Id == StepId)
						{
							GetStepResponse Response = await CreateStepResponseAsync(Step);
							return Response.ApplyFilter(Filter);
						}
					}
					return NotFound(JobId, BatchId, StepId);
				}
			}

			return NotFound(JobId, BatchId);
		}

		/// <summary>
		/// Updates notifications for a specific job.
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="BatchId">Unique id for the batch</param>
		/// <param name="StepId">Unique id for the step</param>
		/// <param name="Request">The notification request</param>
		/// <returns>Information about the requested job</returns>
		[HttpPut]
		[Route("/api/v1/jobs/{JobId}/batches/{BatchId}/steps/{StepId}/notifications")]
		public async Task<ActionResult> UpdateStepNotificationsAsync(JobId JobId, SubResourceId BatchId, SubResourceId StepId, [FromBody] UpdateNotificationsRequest Request)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.CreateSubscription, User, null))
			{
				return Forbid(AclAction.CreateSubscription, JobId);
			}

			if (!Job.TryGetBatch(BatchId, out IJobStepBatch? Batch))
			{
				return NotFound(JobId, BatchId);
			}

			if (!Batch.TryGetStep(StepId, out IJobStep? Step))
			{
				return NotFound(JobId, BatchId, StepId);
			}

			ObjectId? TriggerId = Step.NotificationTriggerId;
			if (TriggerId == null)
			{
				TriggerId = ObjectId.GenerateNewId();
				if (await JobService.UpdateStepAsync(Job, BatchId, StepId, JobStepState.Unspecified, JobStepOutcome.Unspecified, null, null, null, TriggerId, null, null, null) == null)
				{
					return NotFound(JobId, BatchId, StepId);
				}
			}

			await NotificationService.UpdateSubscriptionsAsync(TriggerId.Value, User, Request.Email, Request.Slack);
			return Ok();
		}

		/// <summary>
		/// Gets information about a specific job.
		/// </summary>
		/// <param name="JobId">Id of the job to find</param>
		/// <param name="BatchId">Unique id for the batch</param>
		/// <param name="StepId">Unique id for the step</param>
		/// <returns>Information about the requested job</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/batches/{BatchId}/steps/{StepId}/notifications")]
		public async Task<ActionResult<GetNotificationResponse>> GetStepNotificationsAsync(JobId JobId, SubResourceId BatchId, SubResourceId StepId)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}

			IJobStep? Step;
			if (!Job.TryGetBatch(BatchId, out IJobStepBatch? Batch))
			{
				return NotFound(JobId, BatchId);
			}
			if (!Batch.TryGetStep(StepId, out Step))
			{
				return NotFound(JobId, BatchId, StepId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.CreateSubscription, User, null))
			{
				return Forbid(AclAction.CreateSubscription, JobId);
			}

			INotificationSubscription? Subscription;
			if (Step.NotificationTriggerId == null)
			{
				Subscription = null;
			}
			else
			{
				Subscription = await NotificationService.GetSubscriptionsAsync(Step.NotificationTriggerId.Value, User);
			}
			return new GetNotificationResponse(Subscription);
		}

		/// <summary>
		/// Gets a particular step currently scheduled to be executed for a job
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="BatchId">Unique id for the batch</param>
		/// <param name="StepId">Unique id for the step</param>
		/// <param name="Name"></param>
		/// <returns>List of nodes to be executed</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/batches/{BatchId}/steps/{StepId}/artifacts/{*Name}")]
		public async Task<ActionResult> GetArtifactAsync(JobId JobId, SubResourceId BatchId, SubResourceId StepId, string Name)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}
			if (!Job.TryGetBatch(BatchId, out IJobStepBatch? Batch))
			{
				return NotFound(JobId, BatchId);
			}
			if (!Batch.TryGetStep(StepId, out _))
			{
				return NotFound(JobId, BatchId, StepId);
			}

			List<IArtifact> Artifacts = await ArtifactCollection.GetArtifactsAsync(JobId, StepId, Name);
			if (Artifacts.Count == 0)
			{
				return NotFound();
			}

			IArtifact Artifact = Artifacts[0];
			return new FileStreamResult(await ArtifactCollection.OpenArtifactReadStreamAsync(Artifact), Artifact.MimeType);
		}

		/// <summary>
		/// Gets a particular step currently scheduled to be executed for a job
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="BatchId">Unique id for the batch</param>
		/// <param name="StepId">Unique id for the step</param>
		/// <returns>List of nodes to be executed</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/batches/{BatchId}/steps/{StepId}/trace")]
		public async Task<ActionResult> GetStepTraceAsync(JobId JobId, SubResourceId BatchId, SubResourceId StepId)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid(AclAction.ViewJob, JobId);
			}
			if (!Job.TryGetBatch(BatchId, out IJobStepBatch? Batch))
			{
				return NotFound(JobId, BatchId);
			}
			if (!Batch.TryGetStep(StepId, out _))
			{
				return NotFound(JobId, BatchId, StepId);
			}

			List<IArtifact> Artifacts = await ArtifactCollection.GetArtifactsAsync(JobId, StepId, null);
			foreach (IArtifact Artifact in Artifacts)
			{
				if (Artifact.Name.Equals("trace.json", StringComparison.OrdinalIgnoreCase))
				{
					return new FileStreamResult(await ArtifactCollection.OpenArtifactReadStreamAsync(Artifact), "text/json");
				}
			}
			return NotFound();
		}

		/// <summary>
		/// Updates notifications for a specific label.
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="LabelIndex">Index for the label</param>
		/// <param name="Request">The notification request</param>
		[HttpPut]
		[Route("/api/v1/jobs/{JobId}/labels/{LabelIndex}/notifications")]
		public async Task<ActionResult> UpdateLabelNotificationsAsync(JobId JobId, int LabelIndex, [FromBody] UpdateNotificationsRequest Request)
		{
			ObjectId TriggerId;
			for (; ; )
			{
				IJob? Job = await JobService.GetJobAsync(JobId);
				if (Job == null)
				{
					return NotFound(JobId);
				}
				if (!await JobService.AuthorizeAsync(Job, AclAction.CreateSubscription, User, null))
				{
					return Forbid(AclAction.CreateSubscription, JobId);
				}

				ObjectId NewTriggerId;
				if (Job.LabelIdxToTriggerId.TryGetValue(LabelIndex, out NewTriggerId))
				{
					TriggerId = NewTriggerId;
					break;
				}

				NewTriggerId = ObjectId.GenerateNewId();

				IJob? NewJob = await JobService.UpdateJobAsync(Job, LabelIdxToTriggerId: new KeyValuePair<int, ObjectId>(LabelIndex, NewTriggerId));
				if (NewJob != null)
				{
					TriggerId = NewTriggerId;
					break;
				}
			}

			await NotificationService.UpdateSubscriptionsAsync(TriggerId, User, Request.Email, Request.Slack);
			return Ok();
		}

		/// <summary>
		/// Gets notification info about a specific label in a job.
		/// </summary>
		/// <param name="JobId">Id of the job to find</param>
		/// <param name="LabelIndex">Index for the label</param>
		/// <returns>Notification info for the requested label in the job</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/labels/{LabelIndex}/notifications")]
		public async Task<ActionResult<GetNotificationResponse>> GetLabelNotificationsAsync(JobId JobId, int LabelIndex)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound(JobId);
			}

			INotificationSubscription? Subscription;
			if (!Job.LabelIdxToTriggerId.ContainsKey(LabelIndex))
			{
				Subscription = null;
			}
			else
			{
				Subscription = await NotificationService.GetSubscriptionsAsync(Job.LabelIdxToTriggerId[LabelIndex], User);
			}
			return new GetNotificationResponse(Subscription);
		}
	}
}
