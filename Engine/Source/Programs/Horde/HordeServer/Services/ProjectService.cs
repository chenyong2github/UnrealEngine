// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;

using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;

namespace HordeServer.Services
{
	/// <summary>
	/// Cache of information about job ACLs
	/// </summary>
	public class ProjectPermissionsCache : GlobalPermissionsCache
	{
		/// <summary>
		/// Map of project id to permissions for that project
		/// </summary>
		public Dictionary<ProjectId, IProjectPermissions?> Projects { get; } = new Dictionary<ProjectId, IProjectPermissions?>();
	}

	/// <summary>
	/// Wraps functionality for manipulating projects
	/// </summary>
	public class ProjectService
	{
		/// <summary>
		/// The ACL service
		/// </summary>
		AclService AclService;

		/// <summary>
		/// Collection of project documents
		/// </summary>
		IProjectCollection Projects;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service</param>
		/// <param name="Projects">Collection of project documents</param>
		public ProjectService(AclService AclService, IProjectCollection Projects)
		{
			this.AclService = AclService;
			this.Projects = Projects;
		}

		/// <summary>
		/// Attempts to create a new project
		/// </summary>
		/// <param name="Id">Identifier for the new project</param>
		/// <param name="Name">Name of the new project</param>
		/// <param name="Order">Order to display this project on the dashboard</param>
		/// <param name="Categories">Categories for the new project</param>
		/// <param name="Properties">Properties for the new project</param>
		/// <returns>The new project document</returns>
		public Task<IProject?> TryCreateProjectAsync(ProjectId Id, string Name, int? Order, List<StreamCategory>? Categories, Dictionary<string, string>? Properties)
		{
			return Projects.TryAddAsync(Id, Name, Order, Categories, Properties);
		}

		/// <summary>
		/// Updates an existing project
		/// </summary>
		/// <param name="ProjectId">Unique id of the project</param>
		/// <param name="NewName">The new name for the project</param>
		/// <param name="NewOrder">The new order for this project on the dashboard</param>
		/// <param name="NewCategories">List of categories for the project</param>
		/// <param name="NewProperties">Properties on the project to update. Any properties with a value of null will be removed.</param>
		/// <param name="NewAcl">New acl for the project</param>
		/// <returns>Async task object</returns>
		public Task UpdateProjectAsync(ProjectId ProjectId, string? NewName, int? NewOrder, List<StreamCategory>? NewCategories, Dictionary<string, string>? NewProperties, Acl? NewAcl)
		{
			return Projects.UpdateAsync(ProjectId, NewName, NewOrder, NewCategories, NewProperties, NewAcl);
		}

		/// <summary>
		/// Gets all the available projects
		/// </summary>
		/// <returns>List of project documents</returns>
		public Task<List<IProject>> GetProjectsAsync()
		{
			return Projects.FindAllAsync();
		}

		/// <summary>
		/// Gets a project by ID
		/// </summary>
		/// <param name="ProjectId">Unique id of the project</param>
		/// <returns>The project document</returns>
		public Task<IProject?> GetProjectAsync(ProjectId ProjectId)
		{
			return Projects.GetAsync(ProjectId);
		}

		/// <summary>
		/// Gets a project's permissions info by ID
		/// </summary>
		/// <param name="ProjectId">Unique id of the project</param>
		/// <returns>The project document</returns>
		public Task<IProjectPermissions?> GetProjectPermissionsAsync(ProjectId ProjectId)
		{
			return Projects.GetPermissionsAsync(ProjectId);
		}

		/// <summary>
		/// Deletes a project by id
		/// </summary>
		/// <param name="ProjectId">Unique id of the project</param>
		/// <param name="StreamService">The stream service singleton</param>
		/// <param name="JobService">The job service singleton</param>
		public async Task DeleteProjectAsync(ProjectId ProjectId, StreamService StreamService, JobService JobService)
		{
			await StreamService.DeleteStreamsForProjectAsync(ProjectId, JobService);
			await Projects.DeleteAsync(ProjectId);
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular project
		/// </summary>
		/// <param name="Acl">Acl for the project to check</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">Cache for the scope table</param>
		/// <returns>True if the action is authorized</returns>
		private Task<bool> AuthorizeAsync(Acl? Acl, AclAction Action, ClaimsPrincipal User, GlobalPermissionsCache? Cache)
		{
			bool? Result = Acl?.Authorize(Action, User);
			if (Result == null)
			{
				return AclService.AuthorizeAsync(Action, User, Cache);
			}
			else
			{
				return Task.FromResult(Result.Value);
			}
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular project
		/// </summary>
		/// <param name="Project">The project to check</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">Cache for the scope table</param>
		/// <returns>True if the action is authorized</returns>
		public Task<bool> AuthorizeAsync(IProject Project, AclAction Action, ClaimsPrincipal User, GlobalPermissionsCache? Cache)
		{
			return AuthorizeAsync(Project.Acl, Action, User, Cache);
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular project
		/// </summary>
		/// <param name="ProjectId">The project id to check</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">Cache for project permissions</param>
		/// <returns>True if the action is authorized</returns>
		public async Task<bool> AuthorizeAsync(ProjectId ProjectId, AclAction Action, ClaimsPrincipal User, ProjectPermissionsCache? Cache)
		{
			IProjectPermissions? Permissions;
			if (Cache == null)
			{
				Permissions = await GetProjectPermissionsAsync(ProjectId);
			}
			else if (!Cache.Projects.TryGetValue(ProjectId, out Permissions))
			{
				Permissions = await GetProjectPermissionsAsync(ProjectId);
				Cache.Projects.Add(ProjectId, Permissions);
			}
			return await AuthorizeAsync(Permissions?.Acl, Action, User, Cache);
		}
	}
}
