// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text.Json;
using EpicGames.Core;
using Horde.Build.Jobs;
using Horde.Build.Jobs.Graphs;
using Horde.Build.Logs;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Issues.Handlers
{
	/// <summary>
	/// Instance of a Perforce case mismatch error
	/// </summary>
	class PerforceCaseIssueHandler : IIssueHandler
	{
		/// <inheritdoc/>
		public string Type => "PerforceCase";

		/// <inheritdoc/>
		public int Priority => 10;

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="eventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? eventId)
		{
			return eventId == KnownLogEvents.AutomationTool_PerforceCase;
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint fingerprint, List<SuspectChange> changes)
		{
			foreach (SuspectChange change in changes)
			{
				if(change.Details.Files.Any(x => fingerprint.Keys.Contains(x.DepotPath)))
				{
					change.Rank += 30;
				}
			}
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint fingerprint, IssueSeverity severity)
		{
			return $"Inconsistent case for {StringUtils.FormatList(fingerprint.Keys.Select(x => x.Substring(x.LastIndexOf('/') + 1)).ToArray(), 3)}";
		}

		/// <summary>
		/// Extracts a list of source files from an event
		/// </summary>
		/// <param name="logEventData">The event data</param>
		/// <param name="depotFiles">List of source files</param>
		static void GetSourceFiles(ILogEventData logEventData, HashSet<string> depotFiles)
		{
			foreach (ILogEventLine line in logEventData.Lines)
			{
				JsonElement properties;
				if (line.Data.TryGetProperty("properties", out properties) && properties.ValueKind == JsonValueKind.Object)
				{
					foreach (JsonProperty property in properties.EnumerateObject())
					{
						if (property.NameEquals("File") && property.Value.ValueKind == JsonValueKind.String)
						{
							depotFiles.Add(property.Value.GetString()!);
						}
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

			HashSet<string> newFileNames = new HashSet<string>();
			GetSourceFiles(eventData, newFileNames);

			fingerprint = new NewIssueFingerprint(Type, newFileNames, null, null);
			return true;
		}
	}
}
