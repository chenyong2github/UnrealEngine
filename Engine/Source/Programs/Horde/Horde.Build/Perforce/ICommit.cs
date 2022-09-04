// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Perforce;
using Horde.Build.Streams;
using Horde.Build.Users;
using Horde.Build.Utilities;

namespace Horde.Build.Perforce
{
	using StreamId = StringId<IStream>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Stores metadata about a commit
	/// </summary>
	public interface ICommit
	{
		/// <summary>
		/// Stream containing the commit
		/// </summary>
		StreamId StreamId { get; }

		/// <summary>
		/// The changelist number
		/// </summary>
		int Number { get; }

		/// <summary>
		/// The change that this commit originates from
		/// </summary>
		int OriginalChange { get; }

		/// <summary>
		/// The author user id
		/// </summary>
		UserId AuthorId { get; }

		/// <summary>
		/// The owner of this change, if different from the author (due to Robomerge)
		/// </summary>
		UserId OwnerId { get; }

		/// <summary>
		/// Changelist description
		/// </summary>
		string Description { get; }

		/// <summary>
		/// Base path for all files in the change
		/// </summary>
		string BasePath { get; }

		/// <summary>
		/// Date/time that change was committed
		/// </summary>
		DateTime DateUtc { get; }

		/// <summary>
		/// Gets the files for this change, relative to the root of the stream
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>List of files modified by this commit</returns>
		ValueTask<IReadOnlyList<string>> GetFilesAsync(CancellationToken cancellationToken);
	}

	/// <summary>
	/// Extension methods for commits
	/// </summary>
	public static class CommitExtensions
	{
		/// <summary>
		/// Determines if this change is a code change
		/// </summary>
		/// <returns>True if this change is a code change</returns>
		public static async ValueTask<ChangeContentFlags> GetContentFlagsAsync(this ICommit commit, CancellationToken cancellationToken)
		{
			ChangeContentFlags scope = 0;

			// Check whether the files are code or content
			IReadOnlyList<string> files = await commit.GetFilesAsync(cancellationToken);
			foreach (string file in files)
			{
				if (PerforceUtils.CodeExtensions.Any(extension => file.EndsWith(extension, StringComparison.OrdinalIgnoreCase)))
				{
					scope |= ChangeContentFlags.ContainsCode;
				}
				else
				{
					scope |= ChangeContentFlags.ContainsContent;
				}

				if (scope == (ChangeContentFlags.ContainsCode | ChangeContentFlags.ContainsContent))
				{
					break;
				}
			}
			return scope;
		}
	}
}
