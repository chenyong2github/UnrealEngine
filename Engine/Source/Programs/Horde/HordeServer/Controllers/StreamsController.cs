// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;

using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;
using HordeServer.Collections;
using System.Security.Claims;
using System.IO;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/streams endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class StreamsController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the ACL service
		/// </summary>
		private readonly AclService AclService;

		/// <summary>
		/// Singleton instance of the stream service
		/// </summary>
		private readonly ProjectService ProjectService;

		/// <summary>
		/// Singleton instance of the stream service
		/// </summary>
		private readonly StreamService StreamService;

		/// <summary>
		/// Singleton instance of the template service
		/// </summary>
		private readonly TemplateService TemplateService;

		/// <summary>
		/// Singleton instance of the job service
		/// </summary>
		private readonly JobService JobService;

		/// <summary>
		/// Collection of jobstep refs
		/// </summary>
		private readonly IJobStepRefCollection JobStepRefCollection;

		/// <summary>
		/// Singleton instance of the perforce service
		/// </summary>
		private readonly IPerforceService PerforceService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service</param>
		/// <param name="ProjectService">The project service</param>
		/// <param name="StreamService">The stream service</param>
		/// <param name="TemplateService">The template service</param>
		/// <param name="JobService">The job service</param>
		/// <param name="JobStepRefCollection">The jobstep ref collection</param>
		/// <param name="PerforceService">The perforce service</param>
		public StreamsController(AclService AclService, ProjectService ProjectService, StreamService StreamService, TemplateService TemplateService, JobService JobService, IJobStepRefCollection JobStepRefCollection, IPerforceService PerforceService)
		{
			this.AclService = AclService;
			this.ProjectService = ProjectService;
			this.StreamService = StreamService;
			this.TemplateService = TemplateService;
			this.JobService = JobService;
			this.JobStepRefCollection = JobStepRefCollection;
			this.PerforceService = PerforceService;
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
					ITemplate? Template = await TemplateService.GetTemplateAsync(Pair.Value.Hash);
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

			List<ChangeSummary> Commits = await PerforceService.GetChangesAsync(Stream.Name, Min, Max, Results, PerforceUser);
			return Commits.ConvertAll(x => PropertyFilter.Apply(new GetChangeSummaryResponse(x), Filter));
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

			ChangeDetails? ChangeDetails = await PerforceService.GetChangeDetailsAsync(Stream.Name, Number, PerforceUser);
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

			await StreamService.DeleteStreamAsync(StreamIdValue, JobService);
			return new OkResult();
		}

		/// <summary>
		/// Update a stream's properties.
		/// </summary>
		/// <param name="StreamId">Id of the stream to update.</param>
		/// <param name="Request">Properties on the stream to update</param>
		/// <returns>Http result code</returns>
		[HttpPut]
		[Route("/api/v1/streams/{StreamId}")]
		public async Task<ActionResult> SetStreamAsync(string StreamId, [FromBody] SetStreamRequest Request)
		{
			ActionResult? ClaimError;
			if (!TryAuthorizeTemplateClaims(Request.Templates, out ClaimError))
			{
				return ClaimError;
			}

			StreamId StreamIdValue = new StreamId(StreamId);
			ProjectId ProjectIdValue = new ProjectId(Request.ProjectId);
			for (; ;)
			{
				IStream? Stream = await StreamService.GetStreamAsync(StreamIdValue);
				if (Stream == null)
				{
					IProject? Project = await ProjectService.GetProjectAsync(ProjectIdValue);
					if (Project == null)
					{
						return BadRequest("Invalid project id");
					}
					if (!await ProjectService.AuthorizeAsync(Project, AclAction.CreateStream, User, null))
					{
						return Forbid();
					}
				}
				else
				{
					if (ProjectIdValue != Stream.ProjectId)
					{
						return BadRequest("Cannot modify project id after a stream has been created");
					}

					ProjectPermissionsCache PermissionsCache = new ProjectPermissionsCache();
					if (!await StreamService.AuthorizeAsync(Stream, AclAction.UpdateStream, User, PermissionsCache))
					{
						return Forbid();
					}
					if (Request.Acl != null && !await StreamService.AuthorizeAsync(Stream, AclAction.ChangePermissions, User, PermissionsCache))
					{
						return Forbid();
					}
				}

				try
				{
					Stream = await SetStreamAsync(ProjectIdValue, StreamIdValue, Stream, null, Request, StreamService, TemplateService);
				}
				catch(InvalidStreamException Ex)
				{
					return BadRequest(Ex.Message);
				}

				if(Stream != null)
				{
					return Ok();
				}
			}
		}

		internal static async Task<IStream?> SetStreamAsync(ProjectId ProjectId, StreamId StreamId, IStream? Stream, int? ConfigChange, SetStreamRequest Request, StreamService StreamService, TemplateService TemplateService)
		{
			List<StreamTab>? Tabs = null;
			if (Request.Tabs != null)
			{
				Tabs = Request.Tabs.ConvertAll(x => StreamTab.FromRequest(x));
			}

			Dictionary<string, AgentType>? AgentTypes = null;
			if (Request.AgentTypes != null)
			{
				AgentTypes = Request.AgentTypes.Where(x => x.Value != null).ToDictionary(x => x.Key, x => new AgentType(x.Value!));
			}

			Dictionary<string, WorkspaceType>? WorkspaceTypes = null;
			if (Request.WorkspaceTypes != null)
			{
				WorkspaceTypes = Request.WorkspaceTypes.Where(x => x.Value != null).ToDictionary(x => x.Key, x => new WorkspaceType(x.Value!));
			}

			Dictionary<string, string>? Properties = null;
			if (Request.Properties != null)
			{
				Properties = Request.Properties.Where(x => x.Value != null).ToDictionary(x => x.Key, x => x.Value!);
			}

			Dictionary<TemplateRefId, TemplateRef>? TemplateRefs = null;
			if (Request.Templates != null)
			{
				TemplateRefs = await CreateTemplateRefs(Request.Templates, Stream, TemplateService);
			}

			Acl? Acl = null;
			if (Request.Acl != null)
			{
				Acl = Acl.Merge(null, Request.Acl);
			}

			DefaultPreflight? DefaultPreflight = Request.DefaultPreflight?.ToModel();
			if (DefaultPreflight == null && Request.DefaultPreflightTemplate != null)
			{
				DefaultPreflight = new DefaultPreflight(new TemplateRefId(Request.DefaultPreflightTemplate), null);
			}

			if (Stream == null)
			{
				return await StreamService.TryCreateStreamAsync(StreamId, Request.Name, ProjectId, Request.ConfigPath, Request.Order, Request.NotificationChannel, Request.NotificationChannelFilter, Request.TriageChannel, DefaultPreflight, Tabs, AgentTypes, WorkspaceTypes, TemplateRefs, Properties, Acl);
			}
			else
			{
				return await StreamService.TryReplaceStreamAsync(Stream, Request.Name, Request.ConfigPath, ConfigChange, Request.Order, Request.NotificationChannel, Request.NotificationChannelFilter, Request.TriageChannel, DefaultPreflight, Tabs, AgentTypes, WorkspaceTypes, TemplateRefs, Properties, Acl, Request.PausedUntil, Request.PauseComment);
			}
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
		/// <param name="TemplateService">The template service</param>
		/// <returns>List of new template references</returns>
		static async Task<Dictionary<TemplateRefId, TemplateRef>> CreateTemplateRefs(List<CreateTemplateRefRequest> Requests, IStream? Stream, TemplateService TemplateService)
		{
			Dictionary<TemplateRefId, TemplateRef> NewTemplateRefs = new Dictionary<TemplateRefId, TemplateRef>();
			foreach (CreateTemplateRefRequest Request in Requests)
			{
				// Create the template
				ITemplate NewTemplate = await TemplateService.CreateTemplateAsync(Request.Name, Request.Priority, Request.AllowPreflights, Request.InitialAgentType, Request.SubmitNewChange, Request.Counters, Request.Arguments, Request.Parameters.ConvertAll(x => x.ToModel()));

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

			ITemplate? Template = await TemplateService.GetTemplateAsync(TemplateRef.Hash);
			if(Template == null)
			{
				return NotFound();
			}

			return new GetTemplateResponse(Template).ApplyFilter(Filter);
		}
	}
}
