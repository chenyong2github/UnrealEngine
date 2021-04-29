// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;

using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;

namespace HordeServer.Collections
{
	/// <summary>
	/// Interface for a collection of project documents
	/// </summary>
	public interface IProjectCollection
	{
		/// <summary>
		/// Attempts to create a new project
		/// </summary>
		/// <param name="Id">Identifier for the new project</param>
		/// <param name="Name">Name of the new project</param>
		/// <param name="Order">Order to display this project on the dashboard</param>
		/// <param name="Categories">Categories for the new project</param>
		/// <param name="Properties">Properties for the new project</param>
		/// <returns>The new project document</returns>
		Task<IProject?> TryAddAsync(ProjectId Id, string Name, int? Order = null, List<StreamCategory>? Categories = null, Dictionary<string, string>? Properties = null);

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
		Task UpdateAsync(ProjectId ProjectId, string? NewName, int? NewOrder, List<StreamCategory>? NewCategories, Dictionary<string, string>? NewProperties, Acl? NewAcl);

		/// <summary>
		/// Gets all the available projects
		/// </summary>
		/// <returns>List of project documents</returns>
		Task<List<IProject>> FindAllAsync();

		/// <summary>
		/// Gets a project by ID
		/// </summary>
		/// <param name="ProjectId">Unique id of the project</param>
		/// <returns>The project document</returns>
		Task<IProject?> GetAsync(ProjectId ProjectId);

		/// <summary>
		/// Gets a project's permissions info by ID
		/// </summary>
		/// <param name="ProjectId">Unique id of the project</param>
		/// <returns>The project document</returns>
		Task<IProjectPermissions?> GetPermissionsAsync(ProjectId ProjectId);

		/// <summary>
		/// Deletes a project by id
		/// </summary>
		/// <param name="ProjectId">Unique id of the project</param>
		/// <returns>True if the project was deleted</returns>
		Task DeleteAsync(ProjectId ProjectId);
	}
}
