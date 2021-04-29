using HordeServer.Api;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;

namespace HordeServer.Models
{
	/// <summary>
	/// Summary information for a change
	/// </summary>
	public class ChangeSummary
	{
		/// <summary>
		/// The changelist number
		/// </summary>
		public int Number { get; set; }

		/// <summary>
		/// Author of the change
		/// </summary>
		public string Author { get; set; }

		/// <summary>
		/// Abbreviated changelist description
		/// </summary>
		public string Description { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Number">Changelist number</param>
		/// <param name="Author">Author of the change</param>
		/// <param name="Description">Changelist description</param>
		public ChangeSummary(int Number, string Author, string Description)
		{
			this.Number = Number;
			this.Author = Author;
			this.Description = Description;
		}
	}

	/// <summary>
	/// Flags identifying content of a changelist
	/// </summary>
	[Flags]
	public enum ChangeContentFlags
	{
		/// <summary>
		/// The change contains code
		/// </summary>
		ContainsCode = 1,

		/// <summary>
		/// The change contains content
		/// </summary>
		ContainsContent = 2,
	}

	/// <summary>
	/// Information about a commit
	/// </summary>
	public class ChangeDetails
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
		private ChangeDetails()
		{
			Author = null!;
			Description = null!;
			Files = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Number">Changelist number</param>
		/// <param name="Author">Author of the change</param>
		/// <param name="Description">Changelist description</param>
		/// <param name="Files">List of files modified, relative to the stream base</param>
		public ChangeDetails(int Number, string Author, string Description, List<string> Files)
		{
			this.Number = Number;
			this.Author = Author;
			this.Description = Description;
			this.Files = Files;
		}

		/// <summary>
		/// Determines if this change is a code change
		/// </summary>
		/// <returns>True if this change is a code change</returns>
		public ChangeContentFlags GetContentFlags()
		{
			ChangeContentFlags Scope = 0;

			// Check whether the files are code or content
			string[] CodeExtensions = { ".cs", ".h", ".cpp", ".inl", ".usf", ".ush", ".uproject", ".uplugin" };
			foreach (string File in Files)
			{
				if (CodeExtensions.Any(Extension => File.EndsWith(Extension, StringComparison.OrdinalIgnoreCase)))
				{
					Scope |= ChangeContentFlags.ContainsCode;
				}
				else
				{
					Scope |= ChangeContentFlags.ContainsContent;
				}

				if (Scope == (ChangeContentFlags.ContainsCode | ChangeContentFlags.ContainsContent))
				{
					break;
				}
			}
			return Scope;
		}

		/// <summary>
		/// Attempts to parse the Robomerge source from this commit information
		/// </summary>
		/// <param name="Source">On success, receives the source information</param>
		/// <returns>True if the commit was merged from another stream</returns>
		public bool TryParseRobomergeSource([NotNullWhen(true)] out (string, int)? Source)
		{
			Match Match = Regex.Match(Description, @"#ROBOMERGE-SOURCE: CL (\d+) in (//[^ ]*)/...", RegexOptions.Multiline);
			if (Match.Success)
			{
				Source = (Match.Groups[2].Value, int.Parse(Match.Groups[1].Value, CultureInfo.InvariantCulture));
				return true;
			}
			else
			{
				Source = null;
				return false;
			}
		}
	}

	/// <summary>
	/// Summary of a file in the depot
	/// </summary>
	public class FileSummary
	{
		/// <summary>
		/// Depot path to the file
		/// </summary>
		public string DepotPath { get; set; }

		/// <summary>
		/// Whether the file exists
		/// </summary>
		public bool Exists { get; set; }

		/// <summary>
		/// Last changelist number that the file was modified
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// Error about this file, if it exists
		/// </summary>
		public string? Error { get; set; }

		/// <summary>
		/// Information about a file
		/// </summary>
		/// <param name="DepotPath">Depot </param>
		/// <param name="Exists"></param>
		/// <param name="Change"></param>
		/// <param name="Error"></param>
		public FileSummary(string DepotPath, bool Exists, int Change, string? Error = null)
		{
			this.DepotPath = DepotPath;
			this.Exists = Exists;
			this.Change = Change;
			this.Error = Error;
		}
	}
}
