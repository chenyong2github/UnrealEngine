// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Horde.Build.Streams;
using Horde.Build.Users;
using Horde.Build.Utilities;

namespace Horde.Build.Perforce
{
	using CommitId = ObjectId<ICommit>;
	using StreamId = StringId<IStream>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// New commit object
	/// </summary>
	public class NewCommit
	{
		/// <inheritdoc cref="ICommit.StreamId"/>
		public StreamId StreamId { get; set; }

		/// <inheritdoc cref="ICommit.Number"/>
		public int Change { get; set; }

		/// <inheritdoc cref="ICommit.OriginalChange"/>
		public int OriginalChange { get; set; }

		/// <inheritdoc cref="ICommit.AuthorId"/>
		public UserId AuthorId { get; set; }

		/// <inheritdoc cref="ICommit.OwnerId"/>
		public UserId? OwnerId { get; set; }

		/// <inheritdoc cref="ICommit.Description"/>
		public string Description { get; set; }

		/// <inheritdoc cref="ICommit.BasePath"/>
		public string BasePath { get; set; }

		/// <inheritdoc cref="ICommit.DateUtc"/>
		public DateTime DateUtc { get; set; }
		/*
		/// <summary>
		/// Constructor
		/// </summary>
		public NewCommit(ICommit commit)
			: this(commit.StreamId, commit.Change, commit.OriginalChange, commit.AuthorId, commit.OwnerId, commit.Description, commit.BasePath, commit.DateUtc)
		{
		}
*/
		/// <summary>
		/// Constructor
		/// </summary>
		public NewCommit(StreamId streamId, int change, int originalChange, UserId authorId, UserId ownerId, string description, string basePath, DateTime dateUtc)
		{
			StreamId = streamId;
			Change = change;
			OriginalChange = originalChange;
			AuthorId = authorId;
			OwnerId = ownerId;
			Description = description;
			BasePath = basePath;
			DateUtc = dateUtc;
		}
	}

	/// <summary>
	/// Stores a collection of commits
	/// </summary>
	public interface ICommitCollection
	{
		/// <summary>
		/// Adds or replaces an existing commit
		/// </summary>
		/// <param name="newCommit">The new commit to add</param>
		/// <returns>The commit that was created</returns>
		Task<ICommit> AddOrReplaceAsync(NewCommit newCommit);

		/// <summary>
		/// Gets a single commit
		/// </summary>
		/// <param name="id">Identifier for the commit</param>
		Task<ICommit?> GetCommitAsync(CommitId id);

		/// <summary>
		/// Finds commits matching certain criteria
		/// </summary>
		/// <param name="streamId"></param>
		/// <param name="minChange"></param>
		/// <param name="maxChange"></param>
		/// <param name="index"></param>
		/// <param name="count"></param>
		/// <returns></returns>
		Task<List<ICommit>> FindCommitsAsync(StreamId streamId, int? minChange = null, int? maxChange = null, int? index = null, int? count = null);
	}

	/// <summary>
	/// Extension methods for <see cref="ICommitCollection"/>
	/// </summary>
	static class CommitCollectionExtensions
	{
		/// <summary>
		/// Gets a commit from a stream by changelist number
		/// </summary>
		/// <param name="commitCollection">The commit collection</param>
		/// <param name="streamId"></param>
		/// <param name="change"></param>
		/// <returns></returns>
		public static async Task<ICommit?> GetCommitAsync(this ICommitCollection commitCollection, StreamId streamId, int change)
		{
			List<ICommit> commits = await commitCollection.FindCommitsAsync(streamId, change, change);
			if (commits.Count == 0)
			{
				return null;
			}
			return commits[0];
		}
	}
}
