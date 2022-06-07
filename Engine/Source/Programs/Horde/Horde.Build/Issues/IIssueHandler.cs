// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using EpicGames.Core;
using Horde.Build.Jobs;
using Horde.Build.Jobs.Graphs;
using Horde.Build.Logs;
using Horde.Build.Perforce;

namespace Horde.Build.Issues
{
	/// <summary>
	/// Information about a changelist and a value ranking the likelihood that it caused an issue
	/// </summary>
	class SuspectChange
	{
		/// <summary>
		/// Set of extensions to treat as code
		/// </summary>
		static readonly HashSet<StringView> s_codeExtensions = new HashSet<StringView>(StringViewComparer.OrdinalIgnoreCase)
		{
			".c",
			".cc",
			".cpp",
			".inl",
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
		static readonly HashSet<StringView> s_contentExtensions = new HashSet<StringView>(StringViewComparer.OrdinalIgnoreCase)
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
		/// <param name="details">The changelist details</param>
		public SuspectChange(ChangeDetails details)
		{
			Details = details;

			foreach (ChangeFile file in details.Files)
			{
				int idx = file.Path.LastIndexOf('.');
				if (idx != -1)
				{
					StringView extension = new StringView(file.Path, idx);
					if (s_codeExtensions.Contains(extension))
					{
						ContainsCode = true;
					}
					if (s_contentExtensions.Contains(extension))
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
		/// <param name="fileToCheck">The file to look for</param>
		/// <returns>True if the change modifies the given file</returns>
		public bool ModifiesFile(string fileToCheck)
		{
			foreach (ChangeFile file in Details.Files)
			{
				if (file.Path.EndsWith(fileToCheck, StringComparison.OrdinalIgnoreCase) && (file.Length == fileToCheck.Length || file.Path[file.Path.Length - fileToCheck.Length - 1] == '/'))
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
		/// <param name="job">The job that spawned the event</param>
		/// <param name="node">Node that was executed</param>
		/// <param name="annotations"></param>
		/// <param name="eventData">The event data</param>
		/// <param name="fingerprint">Receives the fingerprint on success</param>
		/// <returns>True if the match is successful</returns>
		bool TryGetFingerprint(IJob job, INode node, IReadOnlyNodeAnnotations annotations, ILogEventData eventData, [NotNullWhen(true)] out NewIssueFingerprint? fingerprint);

		/// <summary>
		/// Rank all the suspect changes for a given fingerprint
		/// </summary>
		/// <param name="fingerprint">The issue fingerprint</param>
		/// <param name="suspects">Potential suspects</param>
		void RankSuspects(IIssueFingerprint fingerprint, List<SuspectChange> suspects);

		/// <summary>
		/// Gets the summary text for an issue
		/// </summary>
		/// <param name="fingerprint">The fingerprint</param>
		/// <param name="severity">Severity of the issue</param>
		/// <returns>The summary text</returns>
		string GetSummary(IIssueFingerprint fingerprint, IssueSeverity severity);
	}
}
