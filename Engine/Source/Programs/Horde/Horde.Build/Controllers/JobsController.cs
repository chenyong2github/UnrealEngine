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
	public class JobsController : ControllerBase
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
		private ILogger<JobsController> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public JobsController(AclService AclService, IGraphCollection Graphs, IPerforceService Perforce, StreamService StreamService, JobService JobService, ITemplateCollection TemplateCollection, IArtifactCollection ArtifactCollection, IUserCollection UserCollection, INotificationService NotificationService, ILogger<JobsController> Logger)
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
				return BadRequest("Invalid StreamId parameter");
			}
			if (!await StreamService.AuthorizeAsync(Stream, AclAction.CreateJob, User, null))
			{
				return Forbid();
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
				return Forbid();
			}

			ITemplate? Template = await TemplateCollection.GetAsync(TemplateRef.Hash);
			if (Template == null)
			{
				return BadRequest($"Missing template referenced by {Create.TemplateId}");
			}
			if (!Template.AllowPreflights && Create.PreflightChange > 0)
			{
				return BadRequest("Template does not allow preflights");
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

			// Create the job
			IJob Job = await JobService.CreateJobAsync(null, Stream, TemplateRefId, Template.Id, Graph, Name, Change, CodeChange, Create.PreflightChange, null, User.GetUserId(), Priority, Create.AutoSubmit, Create.UpdateIssues, TemplateRef.ChainedJobs, TemplateRef.ShowUgsBadges, TemplateRef.ShowUgsAlerts, TemplateRef.NotificationChannel, TemplateRef.NotificationChannelFilter, Arguments);
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
			IList<IJob> Jobs = await JobService.FindJobsAsync(StreamId: Stream.Id, Templates: new[] { TemplateId }, Target: Target, State: new[] { JobStepState.Completed }, Outcome: Outcomes.ToArray(), Count: 1);
			if (Jobs.Count == 0)
			{
				Logger.LogInformation("Unable to find successful build of {TemplateRefId} target {Target}. Using latest change instead", TemplateId, Target);
				return await Perforce.GetLatestChangeAsync(Stream.ClusterName, Stream.Name, null);
			}
			else
			{
				Logger.LogInformation("Last successful build of {TemplateRefId} target {Target} was job {JobId} at change {Change}", TemplateId, Target, Jobs[0].Id, Jobs[0].Change);
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
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.DeleteJob, User, null))
			{
				return Forbid();
			}
			if (!await JobService.DeleteJobAsync(Job))
			{
				return NotFound();
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
				return NotFound();
			}

			StreamPermissionsCache PermissionsCache = new StreamPermissionsCache();
			if (!await JobService.AuthorizeAsync(Job, AclAction.UpdateJob, User, PermissionsCache))
			{
				return Forbid();
			}
			if (Request.Acl != null && !await JobService.AuthorizeAsync(Job, AclAction.ChangePermissions, User, PermissionsCache))
			{
				return Forbid();
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
				return NotFound();
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
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.CreateSubscription, User, null))
			{
				return Forbid();
			}

			ObjectId TriggerId = Job.NotificationTriggerId ?? ObjectId.GenerateNewId();

			Job = await JobService.UpdateJobAsync(Job, null, null, null, null, TriggerId, null, null);
			if (Job == null)
			{
				return NotFound();
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
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.CreateSubscription, User, null))
			{
				return Forbid();
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
				return NotFound();
			}

			StreamPermissionsCache Cache = new StreamPermissionsCache();
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, Cache))
			{
				return Forbid();
			}
			if (ModifiedAfter != null && Job.UpdateTimeUtc <= ModifiedAfter.Value)
			{
				return new Dictionary<string, object>();
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			bool bIncludeAcl = await JobService.AuthorizeAsync(Job, AclAction.ViewPermissions, User, Cache);
			return await CreateJobResponseAsync(Job, Graph, bIncludeAcl, Filter);
		}

		/// <summary>
		/// Creates a response containing information about this object
		/// </summary>
		/// <param name="Job">The job document</param>
		/// <param name="Graph">The graph for this job</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Object containing the requested properties</returns>
		async Task<object> CreateJobResponseAsync(IJob Job, IGraph Graph, bool bIncludeAcl, PropertyFilter? Filter)
		{
			if (Filter == null)
			{
				return await CreateJobResponseAsync(Job, Graph, true, true, bIncludeAcl);
			}
			else
			{
				return Filter.ApplyTo(await CreateJobResponseAsync(Job, Graph, Filter.Includes(nameof(GetJobResponse.Batches)), Filter.Includes(nameof(GetJobResponse.Labels)) || Filter.Includes(nameof(GetJobResponse.DefaultLabel)), bIncludeAcl));
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
		/// <returns>The response object</returns>
		async ValueTask<GetJobResponse> CreateJobResponseAsync(IJob Job, IGraph Graph, bool bIncludeBatches, bool bIncludeLabels, bool bIncludeAcl)
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
						Response.Batches.Add(await CreateBatchResponseAsync(Batch));
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
		/// <returns></returns>
		async ValueTask<GetBatchResponse> CreateBatchResponseAsync(IJobStepBatch Batch)
		{
			List<GetStepResponse> Steps = new List<GetStepResponse>();
			foreach (IJobStep Step in Batch.Steps)
			{
				Steps.Add(await CreateStepResponseAsync(Step));
			}
			return new GetBatchResponse(Batch, Steps);
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
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
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
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			IJobTiming JobTiming = await JobService.GetJobTimingAsync(Job);

			IGraph Graph = await JobService.GetGraphAsync(Job);
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

			return PropertyFilter.Apply(new GetJobTimingResponse(Steps, Labels), Filter);
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
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			ITemplate? Template = await TemplateCollection.GetAsync(Job.TemplateHash);
			if(Template == null)
			{
				return NotFound();
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
					MaxChange, PreflightChange, PreflightStartedByUserIdValue, StartedByUserIdValue, MinCreateTime?.UtcDateTime, MaxCreateTime?.UtcDateTime, Target, State, Outcome,
					ModifiedBefore, ModifiedAfter, Index, Count);
			}

			StreamPermissionsCache PermissionsCache = new StreamPermissionsCache();

			List<object> Responses = new List<object>();
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

					using (IScope _ = GlobalTracer.Instance.BuildSpan("CreateResponse").StartActive())
					{
						Responses.Add(await CreateJobResponseAsync(Job, Graph, bIncludeAcl, Filter));
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
					return NotFound();
				}
				if (!await JobService.AuthorizeAsync(Job, AclAction.ExecuteJob, User, null))
				{
					return Forbid();
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
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
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
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			if (GroupIdx < 0 || GroupIdx >= Graph.Groups.Count)
			{
				return NotFound();
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
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			if (GroupIdx < 0 || GroupIdx >= Graph.Groups.Count)
			{
				return NotFound();
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
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			if (GroupIdx < 0 || GroupIdx >= Graph.Groups.Count || NodeIdx < 0 || NodeIdx >= Graph.Groups[GroupIdx].Nodes.Count)
			{
				return NotFound();
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
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);

			List<object> Responses = new List<object>();
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				GetBatchResponse Response = await CreateBatchResponseAsync(Batch);
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
		public async Task<ActionResult> UpdateBatchAsync(JobId JobId, string BatchId, [FromBody] UpdateBatchRequest Request)
		{
			SubResourceId BatchIdValue = BatchId.ToSubResourceId();

			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound();
			}

			IJobStepBatch? Batch = Job.Batches.FirstOrDefault(x => x.Id == BatchIdValue);
			if (Batch == null)
			{
				return NotFound();
			}
			if (Batch.SessionId == null || !User.HasSessionClaim(Batch.SessionId.Value))
			{
				return Forbid();
			}

			IJob? NewJob = await JobService.UpdateBatchAsync(Job, BatchIdValue, Request.LogId?.ToObjectId<ILogFile>(), Request.State);
			if (NewJob == null)
			{
				return NotFound();
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
		public async Task<ActionResult<object>> GetBatchAsync(JobId JobId, string BatchId, [FromQuery] PropertyFilter? Filter = null)
		{
			SubResourceId BatchIdValue = BatchId.ToSubResourceId();

			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				if (Batch.Id == BatchIdValue)
				{
					GetBatchResponse Response = await CreateBatchResponseAsync(Batch);
					return Response.ApplyFilter(Filter);
				}
			}

			return NotFound();
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
		public async Task<ActionResult<List<object>>> GetStepsAsync(JobId JobId, string BatchId, [FromQuery] PropertyFilter? Filter = null)
		{
			SubResourceId BatchIdValue = BatchId.ToSubResourceId();

			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				if (Batch.Id == BatchIdValue)
				{
					INodeGroup Group = Graph.Groups[Batch.GroupIdx];

					List<object> Responses = new List<object>();
					foreach (IJobStep Step in Batch.Steps)
					{
						GetStepResponse Response = await CreateStepResponseAsync(Step);
						Responses.Add(Response.ApplyFilter(Filter));
					}
					return Responses;
				}
			}

			return NotFound();
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
		public async Task<ActionResult<UpdateStepResponse>> UpdateStepAsync(JobId JobId, string BatchId, string StepId, [FromBody] UpdateStepRequest Request)
		{
			SubResourceId BatchIdValue = BatchId.ToSubResourceId();
			SubResourceId StepIdValue = StepId.ToSubResourceId();

			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound();
			}

			// Check permissions for updating this step. Only the agent executing the step can modify the state of it.
			if (Request.State != JobStepState.Unspecified || Request.Outcome != JobStepOutcome.Unspecified)
			{
				IJobStepBatch? Batch = Job.Batches.FirstOrDefault(x => x.Id == BatchIdValue);
				if (Batch == null)
				{
					return NotFound();
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
					return Forbid();
				}
			}
			if (Request.Properties != null)
			{
				if (!await JobService.AuthorizeAsync(Job, AclAction.UpdateJob, User, null))
				{
					return Forbid();
				}
			}

			UserId? RetryByUser = (Request.Retry.HasValue && Request.Retry.Value) ? User.GetUserId() : null;
			UserId? AbortByUser = (Request.AbortRequested.HasValue && Request.AbortRequested.Value) ? User.GetUserId() : null;

			try
			{
				IJob? NewJob = await JobService.UpdateStepAsync(Job, BatchIdValue, StepIdValue, Request.State, Request.Outcome, Request.AbortRequested, AbortByUser, Request.LogId?.ToObjectId<ILogFile>(), null, RetryByUser, Request.Priority, null, Request.Properties);
				if (NewJob == null)
				{
					return NotFound();
				}

				UpdateStepResponse Response = new UpdateStepResponse();
				if (Request.Retry ?? false)
				{
					JobStepRefId? RetriedStepId = FindRetriedStep(Job, BatchIdValue, StepIdValue);
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
		public async Task<ActionResult<object>> GetStepAsync(JobId JobId, string BatchId, string StepId, [FromQuery] PropertyFilter? Filter = null)
		{
			SubResourceId BatchIdValue = BatchId.ToSubResourceId();
			SubResourceId StepIdValue = StepId.ToSubResourceId();

			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			foreach (IJobStepBatch Batch in Job.Batches)
			{
				if (Batch.Id == BatchIdValue)
				{
					foreach (IJobStep Step in Batch.Steps)
					{
						if (Step.Id == StepIdValue)
						{
							GetStepResponse Response = await CreateStepResponseAsync(Step);
							return Response.ApplyFilter(Filter);
						}
					}
					break;
				}
			}

			return NotFound();
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
		public async Task<ActionResult> UpdateStepNotificationsAsync(JobId JobId, string BatchId, string StepId, [FromBody] UpdateNotificationsRequest Request)
		{
			SubResourceId BatchIdValue = BatchId.ToSubResourceId();
			SubResourceId StepIdValue = StepId.ToSubResourceId();

			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.CreateSubscription, User, null))
			{
				return Forbid();
			}

			if (!Job.TryGetBatch(BatchIdValue, out IJobStepBatch? Batch))
			{
				return NotFound();
			}

			if (!Batch.TryGetStep(StepIdValue, out IJobStep? Step))
			{
				return NotFound();
			}

			ObjectId? TriggerId = Step.NotificationTriggerId;
			if (TriggerId == null)
			{
				TriggerId = ObjectId.GenerateNewId();
				if (await JobService.UpdateStepAsync(Job, BatchIdValue, StepIdValue, JobStepState.Unspecified, JobStepOutcome.Unspecified, null, null, null, TriggerId, null, null, null) == null)
				{
					return NotFound();
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
		public async Task<ActionResult<GetNotificationResponse>> GetStepNotificationsAsync(JobId JobId, string BatchId, string StepId)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound();
			}

			IJobStep? Step;
			if(!Job.TryGetStep(BatchId.ToSubResourceId(), StepId.ToSubResourceId(), out Step))
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.CreateSubscription, User, null))
			{
				return Forbid();
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
		public async Task<ActionResult> GetArtifactAsync(JobId JobId, string BatchId, string StepId, string Name)
		{
			SubResourceId BatchIdValue = BatchId.ToSubResourceId();
			SubResourceId StepIdValue = StepId.ToSubResourceId();

			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}
			if (!Job.TryGetStep(BatchId.ToSubResourceId(), StepId.ToSubResourceId(), out _))
			{
				return NotFound();
			}

			List<IArtifact> Artifacts = await ArtifactCollection.GetArtifactsAsync(JobId, StepIdValue, Name);
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
		public async Task<ActionResult> GetStepTraceAsync(JobId JobId, string BatchId, string StepId)
		{
			SubResourceId BatchIdValue = BatchId.ToSubResourceId();
			SubResourceId StepIdValue = StepId.ToSubResourceId();

			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}
			if (!Job.TryGetStep(BatchId.ToSubResourceId(), StepId.ToSubResourceId(), out _))
			{
				return NotFound();
			}

			List<IArtifact> Artifacts = await ArtifactCollection.GetArtifactsAsync(JobId, StepIdValue, null);
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
					return NotFound();
				}
				if (!await JobService.AuthorizeAsync(Job, AclAction.CreateSubscription, User, null))
				{
					return Forbid();
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
				return NotFound();
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
