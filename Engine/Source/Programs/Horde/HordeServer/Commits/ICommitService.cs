// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.Core;
using MongoDB.Bson;
using HordeServer.Utilities;

namespace HordeServer.Commits
{
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Provides information about commits
	/// </summary>
	interface ICommitService
	{
		/// <summary>
		/// Finds commits matching a set of criteria
		/// </summary>
		/// <param name="StreamId">The stream to query</param>
		/// <param name="MinChange">Minimum change to query</param>
		/// <param name="MaxChange">Maximum change to query</param>
		/// <param name="Paths">Paths to filter by</param>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Maximum number of results to return</param>
		/// <returns>List of commit objects</returns>
		Task<List<ICommit>> FindCommitsAsync(StreamId StreamId, int? MinChange = null, int? MaxChange = null, string[]? Paths = null, int? Index = null, int? Count = null);
	}
}
