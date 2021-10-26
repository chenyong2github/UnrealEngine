// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Summary information for a change
	/// </summary>
	[DebuggerDisplay("{Number}: {Description}")]
	public class ChangeSummary
	{
		/// <summary>
		/// The changelist number
		/// </summary>
		public int Number { get; set; }

		/// <summary>
		/// Author of the change
		/// </summary>
		public IUser Author { get; set; }

		/// <summary>
		/// The base path for modified files
		/// </summary>
		public string Path { get; set; }

		/// <summary>
		/// Abbreviated changelist description
		/// </summary>
		public string Description { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Number">Changelist number</param>
		/// <param name="Author">Author of the change</param>
		/// <param name="Path">Base path for modified files</param>
		/// <param name="Description">Changelist description</param>
		public ChangeSummary(int Number, IUser Author, string Path, string Description)
		{
			this.Number = Number;
			this.Author = Author;
			this.Path = Path;
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
	/// Modified file in a changelist
	/// </summary>
	public class ChangeFile
	{
		/// <summary>
		/// Path to the file
		/// </summary>
		public string Path { get; set; }

		/// <summary>
		/// Path to the file within the depot
		/// </summary>
		public string DepotPath { get; set; }

		/// <summary>
		/// Revision of the file. A value of -1 indicates that the file was deleted.
		/// </summary>
		public int Revision { get; set; }

		/// <summary>
		/// Length of the file
		/// </summary>
		public long Length { get; set; }

		/// <summary>
		/// MD5 digest of the file
		/// </summary>
		public Md5Hash? Digest { get; set; }

		/// <summary>
		/// The file type
		/// </summary>
		public string Type { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Path"></param>
		/// <param name="DepotPath"></param>
		/// <param name="Revision"></param>
		/// <param name="Length"></param>
		/// <param name="Digest"></param>
		/// <param name="Type"></param>
		public ChangeFile(string Path, string DepotPath, int Revision, long Length, Md5Hash? Digest, string Type)
		{
			this.Path = Path;
			this.DepotPath = DepotPath;
			this.Revision = Revision;
			this.Length = Length;
			this.Digest = Digest;
			this.Type = Type;
		}
	}

	/// <summary>
	/// Information about a commit
	/// </summary>
	[DebuggerDisplay("{Number}: {Description}")]
	public class ChangeDetails
	{
		/// <summary>
		/// Set of extensions to treat as code
		/// </summary>
		static readonly HashSet<string> CodeExtensions = new HashSet<string>
		{
			".c",
			".cc",
			".cpp",
			".m",
			".mm",
			".rc",
			".cs",
			".csproj",
			".h",
			".hpp",
			".inl",
			".usf",
			".ush",
			".uproject",
			".uplugin",
			".sln"
		};

		/// <summary>
		/// The source changelist number
		/// </summary>
		public int Number { get; set; }

		/// <summary>
		/// Name of the user that authored this change
		/// </summary>
		public IUser Author { get; set; }

		/// <summary>
		/// The base path for modified files
		/// </summary>
		public string Path { get; set; }

		/// <summary>
		/// The description text
		/// </summary>
		public string Description { get; set; }

		/// <summary>
		/// List of files that were modified, relative to the stream base
		/// </summary>
		public List<ChangeFile> Files { get; set; }

		/// <summary>
		/// Date that the change was submitted
		/// </summary>
		public DateTime Date { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Number">Changelist number</param>
		/// <param name="Author">Author of the change</param>
		/// <param name="Path">Base path for modified files</param>
		/// <param name="Description">Changelist description</param>
		/// <param name="Files">List of files modified, relative to the stream base</param>
		/// <param name="Date">Date that the change was submitted</param>
		public ChangeDetails(int Number, IUser Author, string Path, string Description, List<ChangeFile> Files, DateTime Date)
		{
			this.Number = Number;
			this.Author = Author;
			this.Path = Path;
			this.Description = Description;
			this.Files = Files;
			this.Date = Date;
		}

		/// <summary>
		/// Determines if this change is a code change
		/// </summary>
		/// <returns>True if this change is a code change</returns>
		public ChangeContentFlags GetContentFlags()
		{
			ChangeContentFlags Scope = 0;

			// Check whether the files are code or content
			foreach (ChangeFile File in Files)
			{
				if (CodeExtensions.Any(Extension => File.Path.EndsWith(Extension, StringComparison.OrdinalIgnoreCase)))
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
