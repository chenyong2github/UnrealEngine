// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Models;
using HordeServer.Collections;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using System.Diagnostics.CodeAnalysis;

namespace HordeServer.IssueHandlers
{
	/// <summary>
	/// Information about a changelist and a value ranking the likelihood that it caused an issue
	/// </summary>
	class SuspectChange
	{
		/// <summary>
		/// Set of extensions to treat as code
		/// </summary>
		static readonly HashSet<StringView> CodeExtensions = new HashSet<StringView>(StringViewComparer.OrdinalIgnoreCase)
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
		/// Set of file extensions to treat as content
		/// </summary>
		static readonly HashSet<StringView> ContentExtensions = new HashSet<StringView>(StringViewComparer.OrdinalIgnoreCase)
		{
			".uasset",
			".umap",
			".ini"
		};

		/// <summary>
		/// The change detials
		/// </summary>
		public ChangeDetails Details { get; set; }

		/// <summary>
		/// Rank for the change. A value less than or equal to zero indicates a lack of culpability, positive values indicate
		/// the possibility of a change being the culprit.
		/// </summary>
		public int Rank { get; set; }

		/// <summary>
		/// Whether the change contains code
		/// </summary>
		public bool ContainsCode { get; }

		/// <summary>
		/// Whether the change modifies content
		/// </summary>
		public bool ContainsContent { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Details">The changelist details</param>
		public SuspectChange(ChangeDetails Details)
		{
			this.Details = Details;

			foreach (ChangeFile File in Details.Files)
			{
				int Idx = File.Path.LastIndexOf('.');
				if (Idx != -1)
				{
					StringView Extension = new StringView(File.Path, Idx);
					if (CodeExtensions.Contains(Extension))
					{
						ContainsCode = true;
					}
					if (ContentExtensions.Contains(Extension))
					{
						ContainsContent = true;
					}
					if (ContainsCode && ContainsContent)
					{
						break;
					}
				}
			}
		}

		/// <summary>
		/// Determines whether this change modifies the given file
		/// </summary>
		/// <param name="FileToCheck">The file to look for</param>
		/// <returns>True if the change modifies the given file</returns>
		public bool ModifiesFile(string FileToCheck)
		{
			foreach (ChangeFile File in Details.Files)
			{
				if (File.Path.EndsWith(FileToCheck, StringComparison.OrdinalIgnoreCase) && (File.Length == FileToCheck.Length || File.Path[File.Path.Length - FileToCheck.Length - 1] == '/'))
				{
					return true;
				}
			}
			return false;
		}
	}

	/// <summary>
	/// Interface for issue matchers
	/// </summary>
	interface IIssueHandler
	{
		/// <summary>
		/// Identifier for the type of issue
		/// </summary>
		string Type { get; }

		/// <summary>
		/// Priority of this matcher
		/// </summary>
		int Priority { get; }

		/// <summary>
		/// Match the given event and produce a fingerprint
		/// </summary>
		/// <param name="Job">The job that spawned the event</param>
		/// <param name="Node">Node that was executed</param>
		/// <param name="EventData">The event data</param>
		/// <param name="Fingerprint">Receives the fingerprint on success</param>
		/// <returns>True if the match is successful</returns>
		bool TryGetFingerprint(IJob Job, INode Node, ILogEventData EventData, [NotNullWhen(true)] out NewIssueFingerprint? Fingerprint);

		/// <summary>
		/// Rank all the suspect changes for a given fingerprint
		/// </summary>
		/// <param name="Fingerprint">The issue fingerprint</param>
		/// <param name="Suspects">Potential suspects</param>
		void RankSuspects(IIssueFingerprint Fingerprint, List<SuspectChange> Suspects);

		/// <summary>
		/// Gets the summary text for an issue
		/// </summary>
		/// <param name="Fingerprint">The fingerprint</param>
		/// <param name="Severity">Severity of the issue</param>
		/// <returns>The summary text</returns>
		string GetSummary(IIssueFingerprint Fingerprint, IssueSeverity Severity);
	}
}
