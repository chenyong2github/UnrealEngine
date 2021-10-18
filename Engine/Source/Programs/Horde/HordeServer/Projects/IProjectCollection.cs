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

namespace HordeServer.Collections
{
	using ProjectId = StringId<IProject>;

	/// <summary>
	/// Interface for a collection of project documents
	/// </summary>
	public interface IProjectCollection
	{
		/// <summary>
		/// Updates the project configuration
		/// </summary>
		/// <param name="Id">The project id</param>
		/// <param name="ConfigPath">Path to the config file used to configure this project</param>
		/// <param name="Revision">The config file revision</param>
		/// <param name="Order">Order of the project</param>
		/// <param name="Config">The configuration</param>
		/// <returns>New project instance</returns>
		Task<IProject?> AddOrUpdateAsync(ProjectId Id, string ConfigPath, string Revision, int Order, ProjectConfig Config);

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
		/// Gets the logo for a project
		/// </summary>
		/// <param name="ProjectId">The project id</param>
		/// <returns>The project logo document</returns>
		Task<IProjectLogo?> GetLogoAsync(ProjectId ProjectId);

		/// <summary>
		/// Sets the logo for a project
		/// </summary>
		/// <param name="ProjectId">The project id</param>
		/// <param name="Path">Path to the source file</param>
		/// <param name="Revision">Revision of the file</param>
		/// <param name="MimeType"></param>
		/// <param name="Data"></param>
		/// <returns></returns>
		Task SetLogoAsync(ProjectId ProjectId, string Path, string Revision, string MimeType, byte[] Data);

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
