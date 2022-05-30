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
	/// Instance of a particular compile error
	/// </summary>
	class ContentIssueHandler : IIssueHandler
	{
		/// <inheritdoc/>
		public string Type => "Content";

		/// <inheritdoc/>
		public int Priority => 10;

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="eventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? eventId)
		{
			return eventId == KnownLogEvents.Engine_AssetLog;
		}

		/// <summary>
		/// Adds all the assets from the given log event
		/// </summary>
		/// <param name="eventData">The log event to parse</param>
		/// <param name="assetNames">Receives the referenced asset names</param>
		public static void GetAssetNames(ILogEventData eventData, HashSet<string> assetNames)
		{
			foreach (ILogEventLine line in eventData.Lines)
			{
				string? relativePath;
				if (line.Data.TryGetNestedProperty("properties.asset.relativePath", out relativePath))
				{
					int endIdx = relativePath.LastIndexOfAny(new char[] { '/', '\\' }) + 1;
					string fileName = relativePath.Substring(endIdx);
					assetNames.Add(fileName);
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

			HashSet<string> newAssetNames = new HashSet<string>();
			GetAssetNames(eventData, newAssetNames);
			fingerprint = new NewIssueFingerprint(Type, newAssetNames, null, null);
			return true;
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint fingerprint, IssueSeverity severity)
		{
			string type = (severity == IssueSeverity.Warning) ? "Warnings" : "Errors";
			string list = StringUtils.FormatList(fingerprint.Keys.ToArray(), 2);
			return $"{type} in {list}";
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint fingerprint, List<SuspectChange> suspects)
		{
			foreach (SuspectChange suspect in suspects)
			{
				if (suspect.ContainsContent)
				{
					if (suspect.Details.Files.Any(x => fingerprint.Keys.Any(y => x.Path.Contains(y, StringComparison.OrdinalIgnoreCase))))
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
	}
}
