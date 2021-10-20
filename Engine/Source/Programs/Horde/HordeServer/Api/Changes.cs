// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
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
		/// Constructor
		/// </summary>
		/// <param name="ChangeSummary">The commit to construct from</param>
		public GetChangeSummaryResponse(ChangeSummary ChangeSummary)
		{
			this.Number = ChangeSummary.Number;
			this.Author = ChangeSummary.Author.Name;
			this.AuthorInfo = new GetThinUserInfoResponse(ChangeSummary.Author);
			this.Description = ChangeSummary.Description;
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
		/// List of files that were modified, relative to the stream base
		/// </summary>
		public List<string> Files { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ChangeDetails">The commit to construct from</param>
		public GetChangeDetailsResponse(ChangeDetails ChangeDetails)
		{
			this.Number = ChangeDetails.Number;
			this.Author = ChangeDetails.Author.Name;
			this.AuthorInfo = new GetThinUserInfoResponse(ChangeDetails.Author);
			this.Description = ChangeDetails.Description;
			this.Files = ChangeDetails.Files.ConvertAll(x => x.Path);
		}
	}
}
