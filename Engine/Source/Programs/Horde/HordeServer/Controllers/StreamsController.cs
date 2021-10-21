// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Controller for the /api/v1/streams endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class StreamsController : ControllerBase
	{
		private readonly AclService AclService;
		private readonly ProjectService ProjectService;
		private readonly StreamService StreamService;
		private readonly ITemplateCollection TemplateCollection;
		private readonly JobService JobService;
		private readonly IJobStepRefCollection JobStepRefCollection;
		private readonly IPerforceService PerforceService;
		private readonly IUserCollection UserCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		public StreamsController(AclService AclService, ProjectService ProjectService, StreamService StreamService, ITemplateCollection TemplateCollection, JobService JobService, IJobStepRefCollection JobStepRefCollection, IPerforceService PerforceService, IUserCollection UserCollection)
		{
			this.AclService = AclService;
			this.ProjectService = ProjectService;
			this.StreamService = StreamService;
			this.TemplateCollection = TemplateCollection;
			this.JobService = JobService;
			this.JobStepRefCollection = JobStepRefCollection;
			this.PerforceService = PerforceService;
			this.UserCollection = UserCollection;
		}

		/// <summary>
		/// Query all the streams for a particular project.
		/// </summary>
		/// <param name="ProjectIds">Unique id of the project to query</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about all the projects</returns>
		[HttpGet]
		[Route("/api/v1/streams")]
		[ProducesResponseType(typeof(List<GetStreamResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetStreamsAsync([FromQuery(Name = "ProjectId")] string[] ProjectIds, [FromQuery] PropertyFilter? Filter = null)
		{
			ProjectId[] ProjectIdValues = Array.ConvertAll(ProjectIds, x => new ProjectId(x));

			List<IStream> Streams = await StreamService.GetStreamsAsync(ProjectIdValues);
			ProjectPermissionsCache PermissionsCache = new ProjectPermissionsCache();

			List<GetStreamResponse> Responses = new List<GetStreamResponse>();
			foreach (IStream Stream in Streams)
			{
				if (await StreamService.AuthorizeAsync(Stream, AclAction.ViewStream, User, PermissionsCache))
				{
					GetStreamResponse Response = await CreateGetStreamResponse(Stream, PermissionsCache);
					Responses.Add(Response);
				}
			}
			return Responses.OrderBy(x => x.Order).ThenBy(x => x.Name).Select(x => PropertyFilter.Apply(x, Filter)).ToList();
		}

		/// <summary>
		/// Retrieve information about a specific stream.
		/// </summary>
		/// <param name="StreamId">Id of the stream to get information about</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/streams/{StreamId}")]
		[ProducesResponseType(typeof(GetStreamResponse), 200)]
		public async Task<ActionResult<object>> GetStreamAsync(string StreamId, [FromQuery] PropertyFilter? Filter = null)
		{
			StreamId StreamIdValue = new StreamId(StreamId);

			IStream? Stream = await StreamService.GetStreamAsync(StreamIdValue);
			if (Stream == null)
			{
				return NotFound();
			}

			ProjectPermissionsCache PermissionsCache = new ProjectPermissionsCache();
			if (!await StreamService.AuthorizeAsync(Stream, AclAction.ViewStream, User, PermissionsCache))
			{
				return Forbid();
			}

			bool bIncludeAcl = await StreamService.AuthorizeAsync(Stream, AclAction.ViewPermissions, User, PermissionsCache);
			return PropertyFilter.Apply(await CreateGetStreamResponse(Stream, PermissionsCache), Filter);
		}

		/// <summary>
		/// Create a stream response object, including all the templates
		/// </summary>
		/// <param name="Stream">Stream to create response for</param>
		/// <param name="Cache">Permissions cache</param>
		/// <returns>Response object</returns>
		async Task<GetStreamResponse> CreateGetStreamResponse(IStream Stream, ProjectPermissionsCache Cache)
		{
			bool bIncludeAcl = Stream.Acl != null && await StreamService.AuthorizeAsync(Stream, AclAction.ViewPermissions, User, Cache);

			List<GetTemplateRefResponse> ApiTemplateRefs = new List<GetTemplateRefResponse>();
			foreach (KeyValuePair<TemplateRefId, TemplateRef> Pair in Stream.Templates)
			{
				if (await StreamService.AuthorizeAsync(Stream, Pair.Value, AclAction.ViewTemplate, User, Cache))
				{
					ITemplate? Template = await TemplateCollection.GetAsync(Pair.Value.Hash);
					if (Template != null)
					{
						bool bIncludeTemplateAcl = Pair.Value.Acl != null && await StreamService.AuthorizeAsync(Stream, Pair.Value, AclAction.ViewPermissions, User, Cache);
						ApiTemplateRefs.Add(new GetTemplateRefResponse(Pair.Key, Pair.Value, Template, bIncludeTemplateAcl));
					}
				}
			}

			return Stream.ToApiResponse(bIncludeAcl, ApiTemplateRefs);
		}

		/// <summary>
		/// Gets a list of changes for a stream
		/// </summary>
		/// <param name="StreamId">The stream id</param>
		/// <param name="Min">The starting changelist number</param>
		/// <param name="Max">The ending changelist number</param>
		/// <param name="Results">Number of results to return</param>
		/// <param name="Filter">The filter to apply to the results</param>
		/// <returns>Http result code</returns>
		[HttpGet]
		[Route("/api/v1/streams/{StreamId}/changes")]
		[ProducesResponseType(typeof(List<GetChangeSummaryResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetChangesAsync(string StreamId, [FromQuery] int? Min = null, [FromQuery] int? Max = null, [FromQuery] int Results = 50, PropertyFilter? Filter = null)
		{
			StreamId StreamIdValue = new StreamId(StreamId);

			IStream? Stream = await StreamService.GetStreamAsync(StreamIdValue);
			if (Stream == null)
			{
				return NotFound();
			}
			if (!await StreamService.AuthorizeAsync(Stream, AclAction.ViewChanges, User, null))
			{
				return Forbid();
			}

			string? PerforceUser = User.GetPerforceUser();
			if(PerforceUser == null)
			{
				return Forbid();
			}

			List<ChangeSummary> Commits = await PerforceService.GetChangesAsync(Stream.ClusterName, Stream.Name, Min, Max, Results, PerforceUser);

			List<GetChangeSummaryResponse> Responses = new List<GetChangeSummaryResponse>();
			foreach (ChangeSummary Commit in Commits)
			{
				Responses.Add(new GetChangeSummaryResponse(Commit));
			}
			return Responses.ConvertAll(x => PropertyFilter.Apply(x, Filter));
		}

		/// <summary>
		/// Gets a list of changes for a stream
		/// </summary>
		/// <param name="StreamId">The stream id</param>
		/// <param name="Number">The changelist number</param>
		/// <param name="Filter">The filter to apply to the results</param>
		/// <returns>Http result code</returns>
		[HttpGet]
		[Route("/api/v1/streams/{StreamId}/changes/{Number}")]
		[ProducesResponseType(typeof(GetChangeDetailsResponse), 200)]
		public async Task<ActionResult<object>> GetChangeDetailsAsync(string StreamId, int Number, PropertyFilter? Filter = null)
		{
			StreamId StreamIdValue = new StreamId(StreamId);

			IStream? Stream = await StreamService.GetStreamAsync(StreamIdValue);
			if (Stream == null)
			{
				return NotFound();
			}
			if (!await StreamService.AuthorizeAsync(Stream, AclAction.ViewChanges, User, null))
			{
				return Forbid();
			}

			string? PerforceUser = User.GetPerforceUser();
			if(PerforceUser == null)
			{
				return Forbid();
			}

			ChangeDetails? ChangeDetails = await PerforceService.GetChangeDetailsAsync(Stream.ClusterName, Stream.Name, Number, PerforceUser);
			if(ChangeDetails == null)
			{
				return NotFound();
			}

			return PropertyFilter.Apply(new GetChangeDetailsResponse(ChangeDetails), Filter);
		}

		/// <summary>
		/// Gets the history of a step in the stream
		/// </summary>
		/// <param name="StreamId">The stream id</param>
		/// <param name="TemplateId"></param>
		/// <param name="Step">Name of the step to search for</param>
		/// <param name="Change">Maximum changelist number to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <param name="Filter">The filter to apply to the results</param>
		/// <returns>Http result code</returns>
		[HttpGet]
		[Route("/api/v1/streams/{StreamId}/history")]
		[ProducesResponseType(typeof(List<GetJobStepRefResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetStepHistoryAsync(string StreamId, [FromQuery] string TemplateId, [FromQuery] string Step, [FromQuery] int? Change = null, [FromQuery] int Count = 10, [FromQuery] PropertyFilter? Filter = null)
		{
			StreamId StreamIdValue = new StreamId(StreamId);

			IStream? Stream = await StreamService.GetStreamAsync(StreamIdValue);
			if (Stream == null)
			{
				return NotFound();
			}
			if (!await StreamService.AuthorizeAsync(Stream, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			TemplateRefId TemplateIdValue = new TemplateRefId(TemplateId);

			List<IJobStepRef> Steps = await JobStepRefCollection.GetStepsForNodeAsync(StreamIdValue, TemplateIdValue, Step, Change, true, Count);
			return Steps.ConvertAll(x => PropertyFilter.Apply(new GetJobStepRefResponse(x), Filter));
		}

		/// <summary>
		/// Deletes a stream
		/// </summary>
		/// <param name="StreamId">Id of the stream to update.</param>
		/// <returns>Http result code</returns>
		[HttpDelete]
		[Route("/api/v1/streams/{StreamId}")]
		public async Task<ActionResult> DeleteStreamAsync(string StreamId)
		{
			StreamId StreamIdValue = new StreamId(StreamId);

			IStream? Stream = await StreamService.GetStreamAsync(StreamIdValue);
			if (Stream == null)
			{
				return NotFound();
			}
			if (!await StreamService.AuthorizeAsync(Stream, AclAction.DeleteStream, User, null))
			{
				return Forbid();
			}

			await StreamService.DeleteStreamAsync(StreamIdValue);
			return new OkResult();
		}

		/// <summary>
		/// Validates that the user is allowed to specify the given template refs
		/// </summary>
		/// <param name="Requests">List of template refs</param>
		/// <param name="OutResult">On failure, receives information about the missing claim</param>
		/// <returns>True if the refs are valid, false otherwise</returns>
		bool TryAuthorizeTemplateClaims(List<CreateTemplateRefRequest>? Requests, [NotNullWhen(false)] out ActionResult? OutResult)
		{
			if (Requests != null)
			{
				foreach (CreateTemplateRefRequest Request in Requests)
				{
					if (Request.Schedule != null && Request.Schedule.Claims != null)
					{
						foreach (CreateAclClaimRequest Claim in Request.Schedule.Claims)
						{
							if (!User.HasClaim(Claim.Type, Claim.Value))
							{
								OutResult = BadRequest("User does not have the {Claim.Type}: {Claim.Value} claim");
								return false;
							}
						}
					}
				}
			}

			OutResult = null;
			return true;
		}

		/// <summary>
		/// Creates a list of template refs from a set of request objects
		/// </summary>
		/// <param name="Requests">Request objects</param>
		/// <param name="Stream">The current stream state</param>
		/// <param name="TemplateCollection">The template service</param>
		/// <returns>List of new template references</returns>
		static async Task<Dictionary<TemplateRefId, TemplateRef>> CreateTemplateRefs(List<CreateTemplateRefRequest> Requests, IStream? Stream, ITemplateCollection TemplateCollection)
		{
			Dictionary<TemplateRefId, TemplateRef> NewTemplateRefs = new Dictionary<TemplateRefId, TemplateRef>();
			foreach (CreateTemplateRefRequest Request in Requests)
			{
				// Create the template
				ITemplate NewTemplate = await TemplateCollection.AddAsync(Request.Name, Request.Priority, Request.AllowPreflights, Request.InitialAgentType, Request.SubmitNewChange, Request.Arguments, Request.Parameters.ConvertAll(x => x.ToModel()));

				// Get an identifier for the new template ref
				TemplateRefId NewTemplateRefId;
				if (Request.Id != null)
				{
					NewTemplateRefId = new TemplateRefId(Request.Id);
				}
				else
				{
					NewTemplateRefId = TemplateRefId.Sanitize(Request.Name);
				}

				// Add it to the list
				TemplateRef NewTemplateRef = new TemplateRef(NewTemplate, Request.ShowUgsBadges, Request.ShowUgsAlerts, Request.NotificationChannel, Request.NotificationChannelFilter, Request.TriageChannel, Request.Schedule?.ToModel(), Request.ChainedJobs?.ConvertAll(x => new ChainedJobTemplate(x)), Acl.Merge(null, Request.Acl));
				if (Stream != null && Stream.Templates.TryGetValue(NewTemplateRefId, out TemplateRef? OldTemplateRef))
				{
					if (OldTemplateRef.Schedule != null && NewTemplateRef.Schedule != null)
					{
						NewTemplateRef.Schedule.CopyState(OldTemplateRef.Schedule);
					}
				}
				NewTemplateRefs.Add(NewTemplateRefId, NewTemplateRef);
			}
			foreach (TemplateRef TemplateRef in NewTemplateRefs.Values)
			{
				if (TemplateRef.ChainedJobs != null)
				{
					foreach (ChainedJobTemplate ChainedJob in TemplateRef.ChainedJobs)
					{
						if (!NewTemplateRefs.ContainsKey(ChainedJob.TemplateRefId))
						{
							throw new InvalidDataException($"Invalid template ref id '{ChainedJob.TemplateRefId}");
						}
					}
				}
			}
			return NewTemplateRefs;
		}

		/// <summary>
		/// Gets all the templates for a stream
		/// </summary>
		/// <param name="StreamId">Unique id of the stream to query</param>
		/// <param name="TemplateRefId">Unique id of the template to query</param>
		/// <param name="Filter">Filter for properties to return</param>
		/// <returns>Information about all the templates</returns>
		[HttpGet]
		[Route("/api/v1/streams/{StreamId}/templates/{TemplateRefId}")]
		[ProducesResponseType(typeof(List<GetTemplateResponse>), 200)]
		public async Task<ActionResult<object>> GetTemplateAsync(string StreamId, string TemplateRefId, [FromQuery] PropertyFilter? Filter = null)
		{
			StreamId StreamIdValue = new StreamId(StreamId);
			TemplateRefId TemplateRefIdValue = new TemplateRefId(TemplateRefId);

			IStream? Stream = await StreamService.GetStreamAsync(StreamIdValue);
			if (Stream == null)
			{
				return NotFound();
			}

			TemplateRef? TemplateRef;
			if (!Stream.Templates.TryGetValue(TemplateRefIdValue, out TemplateRef))
			{
				return NotFound();
			}
			if (!await StreamService.AuthorizeAsync(Stream, TemplateRef, AclAction.ViewTemplate, User, null))
			{
				return Forbid();
			}

			ITemplate? Template = await TemplateCollection.GetAsync(TemplateRef.Hash);
			if(Template == null)
			{
				return NotFound();
			}

			return new GetTemplateResponse(Template).ApplyFilter(Filter);
		}
	}
}
