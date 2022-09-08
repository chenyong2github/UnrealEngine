// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using Horde.Build.Users;

namespace Horde.Build.Perforce
{
	/// <summary>
	/// Information about a submitted changelist
	/// </summary>
	public class GetChangeSummaryResponse
	{
		/// <summary>
		/// The source changelist number
		/// </summary>
		public int Number { get; set; }

		/// <summary>
		/// Name of the user that authored this change [DEPRECATED]
		/// </summary>
		public string Author { get; set; }

		/// <summary>
		/// Information about the change author
		/// </summary>
		public GetThinUserInfoResponse AuthorInfo { get; set; }

		/// <summary>
		/// The description text
		/// </summary>
		public string Description { get; set; }

		/// <summary>
		/// Tags for this commit
		/// </summary>
		public List<CommitTag> Tags { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="commit">The commit to construct from</param>
		/// <param name="author">Author of the commit</param>
		/// <param name="tags">Tags for the commit</param>
		public GetChangeSummaryResponse(ICommit commit, IUser author, IReadOnlyList<CommitTag> tags)
		{
			Number = commit.Number;
			Author = author.Name;
			AuthorInfo = new GetThinUserInfoResponse(author);
			Description = commit.Description;
			Tags = new List<CommitTag>(tags);
		}
	}

	/// <summary>
	/// Information about a submitted changelist
	/// </summary>
	public class GetChangeDetailsResponse
	{
		/// <summary>
		/// The source changelist number
		/// </summary>
		public int Number { get; set; }

		/// <summary>
		/// Name of the user that authored this change [DEPRECATED]
		/// </summary>
		public string Author { get; set; }

		/// <summary>
		/// Information about the user that authored this change
		/// </summary>
		public GetThinUserInfoResponse AuthorInfo { get; set; }

		/// <summary>
		/// The description text
		/// </summary>
		public string Description { get; set; }

		/// <summary>
		/// Tags for this commit
		/// </summary>
		public List<CommitTag> Tags { get; set; }

		/// <summary>
		/// List of files that were modified, relative to the stream base
		/// </summary>
		public List<string> Files { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="commit">The commit to construct from</param>
		/// <param name="author">Author of the change</param>
		/// <param name="tags">Tags for the commit</param>
		/// <param name="files">Files modified by the commit</param>
		public GetChangeDetailsResponse(ICommit commit, IUser author, IReadOnlyList<CommitTag> tags, IReadOnlyList<string> files)
		{
			Number = commit.Number;
			Author = author.Name;
			AuthorInfo = new GetThinUserInfoResponse(author);
			Description = commit.Description;
			Tags = new List<CommitTag>(tags);
			Files = new List<string>(files);
		}
	}
}
