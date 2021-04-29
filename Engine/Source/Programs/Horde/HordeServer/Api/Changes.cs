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
		/// Name of the user that authored this change
		/// </summary>
		public string Author { get; set; }

		/// <summary>
		/// The description text
		/// </summary>
		public string Description { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private GetChangeSummaryResponse()
		{
			Author = String.Empty;
			Description = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ChangeSummary">The commit to construct from</param>
		public GetChangeSummaryResponse(ChangeSummary ChangeSummary)
		{
			this.Number = ChangeSummary.Number;
			this.Author = ChangeSummary.Author;
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
		/// Name of the user that authored this change
		/// </summary>
		public string Author { get; set; }

		/// <summary>
		/// The description text
		/// </summary>
		public string Description { get; set; }

		/// <summary>
		/// List of files that were modified, relative to the stream base
		/// </summary>
		public List<string> Files { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private GetChangeDetailsResponse()
		{
			Author = null!;
			Description = null!;
			Files = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ChangeDetails">The commit to construct from</param>
		public GetChangeDetailsResponse(ChangeDetails ChangeDetails)
		{
			this.Number = ChangeDetails.Number;
			this.Author = ChangeDetails.Author;
			this.Description = ChangeDetails.Description;
			this.Files = ChangeDetails.Files;
		}
	}
}
