// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using MongoDB.Driver.Core.Authentication;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	/// <summary>
	/// Collection of stream documents
	/// </summary>
	public interface IUgsMetadataCollection
	{
		/// <summary>
		/// Finds or adds metadata for a given changelist
		/// </summary>
		/// <param name="Stream">The stream containing the change</param>
		/// <param name="Change">The changelist number</param>
		/// <param name="Project">Project identifier</param>
		/// <returns>The metadata instance</returns>
		Task<IUgsMetadata> FindOrAddAsync(string Stream, int Change, string? Project);

		/// <summary>
		/// Adds information to a change
		/// </summary>
		/// <param name="Metadata">The existing metadata</param>
		/// <param name="UserName">Name of the user posting the change</param>
		/// <param name="Synced">Time that the change was synced</param>
		/// <param name="Vote">Vote for this change</param>
		/// <param name="Investigating">Whether the user is investigating the change</param>
		/// <param name="Starred">Whether the change is starred</param>
		/// <param name="Comment">New comment from this user</param>
		/// <returns>Async task task</returns>
		Task<IUgsMetadata> UpdateUserAsync(IUgsMetadata Metadata, string UserName, bool? Synced, UgsUserVote? Vote, bool? Investigating, bool? Starred, string? Comment);

		/// <summary>
		/// Updates the state of a badge
		/// </summary>
		/// <param name="Metadata">The existing metadata</param>
		/// <param name="Name">Name of the badge</param>
		/// <param name="Url">Url to link to for the badge</param>
		/// <param name="State">State of the badge</param>
		/// <returns>Async task</returns>
		Task<IUgsMetadata> UpdateBadgeAsync(IUgsMetadata Metadata, string Name, Uri? Url, UgsBadgeState State);

		/// <summary>
		/// Searches for metadata updates
		/// </summary>
		/// <param name="Stream">The stream to search</param>
		/// <param name="MinChange">Minimum changelist number</param>
		/// <param name="MaxChange">Maximum changelist number</param>
		/// <param name="AfterTicks">Last query time</param>
		/// <returns>List of metadata updates</returns>
		Task<List<IUgsMetadata>> FindAsync(string Stream, int MinChange, int? MaxChange = null, long? AfterTicks = null);
	}
}
