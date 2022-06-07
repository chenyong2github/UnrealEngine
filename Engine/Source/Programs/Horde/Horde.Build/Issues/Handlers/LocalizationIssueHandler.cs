// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using EpicGames.Core;
using Horde.Build.Collections;
using Horde.Build.Models;
using Horde.Build.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Driver;

namespace Horde.Build.IssueHandlers.Impl
{
	/// <summary>
	/// Instance of a localization error
	/// </summary>
	class LocalizationIssueHandler : IIssueHandler
	{
		/// <inheritdoc/>
		public string Type => "Localization";

		/// <inheritdoc/>
		public int Priority => 10;

		/// <summary>
		/// Filenames containing errors or warnings
		/// </summary>
		public HashSet<string> FileNames { get; set; } = new HashSet<string>();

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="eventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? eventId)
		{
			return eventId == KnownLogEvents.Engine_Localization;
		}

		/// <summary>
		/// Extracts a list of source files from an event
		/// </summary>
		/// <param name="logEventData">The event data</param>
		/// <param name="sourceFiles">List of source files</param>
		public static void GetSourceFiles(ILogEventData logEventData, HashSet<string> sourceFiles)
		{
			foreach (ILogEventLine line in logEventData.Lines)
			{
				string? relativePath;
				if (line.Data.TryGetNestedProperty("properties.file.relativePath", out relativePath) || line.Data.TryGetNestedProperty("properties.file", out relativePath))
				{
					if (!relativePath.EndsWith(".manifest", StringComparison.OrdinalIgnoreCase))
					{
						int endIdx = relativePath.LastIndexOfAny(new char[] { '/', '\\' }) + 1;
						string fileName = relativePath.Substring(endIdx);
						sourceFiles.Add(fileName);
					}
				}
			}
		}

		/// <inheritdoc/>
		public bool TryGetFingerprint(IJob job, INode node, IReadOnlyNodeAnnotations annotations, ILogEventData eventData, [NotNullWhen(true)] out NewIssueFingerprint? fingerprint)
		{
			if (!IsMatchingEventId(eventData.EventId))
			{
				fingerprint = null;
				return false;
			}

			HashSet<string> newFileNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			GetSourceFiles(eventData, newFileNames);
			fingerprint = new NewIssueFingerprint(Type, newFileNames, null, null);
			return true;
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint fingerprint, List<SuspectChange> suspects)
		{
			foreach (SuspectChange suspect in suspects)
			{
				if (suspect.ContainsCode)
				{
					if (FileNames.Any(x => suspect.ModifiesFile(x)))
					{
						suspect.Rank += 20;
					}
					else
					{
						suspect.Rank += 10;
					}
				}
			}
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint fingerprint, IssueSeverity severity)
		{
			string type = (severity == IssueSeverity.Warning)? "warnings" : "errors";
			return $"Localization {type} in {StringUtils.FormatList(fingerprint.Keys.ToArray(), 2)}";
		}
	}
}
