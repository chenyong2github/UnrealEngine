// Copyright Epic Games, Inc. All Rights Reserved.

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
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	using ProjectId = StringId<IProject>;

	/// <summary>
	/// Controller for the /api/v1/projects endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class ProjectsController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the project service
		/// </summary>
		private readonly ProjectService ProjectService;

		/// <summary>
		/// Singleton instance of the stream service
		/// </summary>
		private readonly StreamService StreamService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ProjectService">The project service</param>
		/// <param name="StreamService">The stream service</param>
		public ProjectsController(ProjectService ProjectService, StreamService StreamService)
		{
			this.ProjectService = ProjectService;
			this.StreamService = StreamService;
		}

		/// <summary>
		/// Query all the projects
		/// </summary>
		/// <param name="IncludeStreams">Whether to include streams in the response</param>
		/// <param name="IncludeCategories">Whether to include categories in the response</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about all the projects</returns>
		[HttpGet]
		[Route("/api/v1/projects")]
		[ProducesResponseType(typeof(List<GetProjectResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetProjectsAsync([FromQuery(Name = "Streams")] bool IncludeStreams = false, [FromQuery(Name = "Categories")] bool IncludeCategories = false, [FromQuery] PropertyFilter? Filter = null)
		{
			List<IProject> Projects = await ProjectService.GetProjectsAsync();
			ProjectPermissionsCache PermissionsCache = new ProjectPermissionsCache();

			List<IStream>? Streams = null;
			if (IncludeStreams || IncludeCategories)
			{
				Streams = await StreamService.GetStreamsAsync();
			}

			List<object> Responses = new List<object>();
			foreach (IProject Project in Projects)
			{
				if (await ProjectService.AuthorizeAsync(Project, AclAction.ViewProject, User, PermissionsCache))
				{
					bool bIncludeAcl = await ProjectService.AuthorizeAsync(Project, AclAction.ViewPermissions, User, PermissionsCache);
					Responses.Add(Project.ToResponse(IncludeStreams, IncludeCategories, Streams, bIncludeAcl).ApplyFilter(Filter));
				}
			}
			return Responses;
		}

		/// <summary>
		/// Retrieve information about a specific project
		/// </summary>
		/// <param name="ProjectId">Id of the project to get information about</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/projects/{ProjectId}")]
		[ProducesResponseType(typeof(List<GetProjectResponse>), 200)]
		public async Task<ActionResult<object>> GetProjectAsync(string ProjectId, [FromQuery] PropertyFilter? Filter = null)
		{
			ProjectId ProjectIdValue = new ProjectId(ProjectId);

			IProject? Project = await ProjectService.GetProjectAsync(ProjectIdValue);
			if (Project == null)
			{
				return NotFound();
			}

			ProjectPermissionsCache Cache = new ProjectPermissionsCache();
			if (!await ProjectService.AuthorizeAsync(Project, AclAction.ViewProject, User, Cache))
			{
				return Forbid();
			}

			bool bIncludeStreams = PropertyFilter.Includes(Filter, nameof(GetProjectResponse.Streams));
			bool bIncludeCategories = PropertyFilter.Includes(Filter, nameof(GetProjectResponse.Categories));

			List<IStream>? VisibleStreams = null;
			if (bIncludeStreams || bIncludeCategories)
			{
				VisibleStreams = new List<IStream>();

				List<IStream> Streams = await StreamService.GetStreamsAsync(Project.Id);
				foreach (IStream Stream in Streams)
				{
					if (await StreamService.AuthorizeAsync(Stream, AclAction.ViewStream, User, Cache))
					{
						VisibleStreams.Add(Stream);
					}
				}
			}

			bool bIncludeAcl = await ProjectService.AuthorizeAsync(Project, AclAction.ViewPermissions, User, Cache);
			return Project.ToResponse(bIncludeStreams, bIncludeCategories, VisibleStreams, bIncludeAcl).ApplyFilter(Filter);
		}

		/// <summary>
		/// Retrieve information about a specific project
		/// </summary>
		/// <param name="ProjectId">Id of the project to get information about</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/projects/{ProjectId}/logo")]
		public async Task<ActionResult<object>> GetProjectLogoAsync(string ProjectId)
		{
			ProjectId ProjectIdValue = new ProjectId(ProjectId);

			IProject? Project = await ProjectService.GetProjectAsync(ProjectIdValue);
			if (Project == null)
			{
				return NotFound();
			}
			if (!await ProjectService.AuthorizeAsync(Project, AclAction.ViewProject, User, null))
			{
				return Forbid();
			}

			IProjectLogo? ProjectLogo = await ProjectService.Collection.GetLogoAsync(ProjectIdValue);
			if (ProjectLogo == null)
			{
				return NotFound();
			}

			return new FileContentResult(ProjectLogo.Data, ProjectLogo.MimeType);
		}
	}
}
