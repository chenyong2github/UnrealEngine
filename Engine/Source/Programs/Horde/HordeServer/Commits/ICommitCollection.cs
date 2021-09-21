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
	/// Stores a collection of commits
	/// </summary>
	interface ICommitCollection
	{
		/// <summary>
		/// Adds or replaces an existing commit
		/// </summary>
		/// <param name="NewCommit">The new commit to add</param>
		/// <returns>The commit that was created</returns>
		Task<ICommit> AddOrReplaceAsync(NewCommit NewCommit);

		/// <summary>
		/// Gets a single commit
		/// </summary>
		/// <param name="Id">Identifier for the commit</param>
		Task<ICommit?> GetCommitAsync(ObjectId Id);

		/// <summary>
		/// Finds commits matching certain criteria
		/// </summary>
		/// <param name="StreamId"></param>
		/// <param name="MinChange"></param>
		/// <param name="MaxChange"></param>
		/// <param name="Index"></param>
		/// <param name="Count"></param>
		/// <returns></returns>
		Task<List<ICommit>> FindCommitsAsync(StreamId StreamId, int? MinChange = null, int? MaxChange = null, int? Index = null, int? Count = null);
	}

	/// <summary>
	/// Extension methods for <see cref="ICommitCollection"/>
	/// </summary>
	static class CommitCollectionExtensions
	{
		/// <summary>
		/// Gets a commit from a stream by changelist number
		/// </summary>
		/// <param name="CommitCollection">The commit collection</param>
		/// <param name="StreamId"></param>
		/// <param name="Change"></param>
		/// <returns></returns>
		public static async Task<ICommit?> GetCommitAsync(this ICommitCollection CommitCollection, StreamId StreamId, int Change)
		{
			List<ICommit> Commits = await CommitCollection.FindCommitsAsync(StreamId, Change, Change);
			if (Commits.Count == 0)
			{
				return null;
			}
			return Commits[0];
		}
	}
}
