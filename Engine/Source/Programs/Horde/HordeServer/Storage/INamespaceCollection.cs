// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Claims;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Storage
{
	using NamespaceId = StringId<INamespace>;

	/// <summary>
	/// Interface for a collection of namespace documents
	/// </summary>
	public interface INamespaceCollection
	{
		/// <summary>
		/// Adds or updates a bucket configuration
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="Namespace">New settings for the bucket</param>
		Task AddOrUpdateAsync(NamespaceId NamespaceId, NamespaceConfig Namespace);

		/// <summary>
		/// Attempts to get a namespace by identifier
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <returns>Namespace definition</returns>
		Task<INamespace?> GetAsync(NamespaceId NamespaceId);

		/// <summary>
		/// Remove a namespace
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		Task RemoveAsync(NamespaceId NamespaceId);

		/// <summary>
		/// Find all namespaces
		/// </summary>
		/// <returns>List of namespaces</returns>
		Task<List<INamespace>> FindAsync();

		/// <summary>
		/// Authorize a user to perform the given action. May use cached data.
		/// </summary>
		/// <param name="NamespaceId">The namespace id</param>
		/// <param name="User">User to authorize</param>
		/// <param name="Action">Required permissions</param>
		/// <returns>True if the user is authorized to perform the given action</returns>
		Task<bool> AuthorizeAsync(NamespaceId NamespaceId, ClaimsPrincipal User, AclAction Action);
	}
}
