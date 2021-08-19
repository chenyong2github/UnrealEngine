// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Security.Policy;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace HordeServer.IssueHandlers.Impl
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
		/// <param name="EventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? EventId)
		{
			return EventId == KnownLogEvents.Engine_AssetLog;
		}

		/// <summary>
		/// Adds all the assets from the given log event
		/// </summary>
		/// <param name="Event">The log event to parse</param>
		/// <param name="AssetNames">Receives the referenced asset names</param>
		public static void GetAssetNames(ILogEventData Event, HashSet<string> AssetNames)
		{
			foreach (ILogEventLine Line in Event.Lines)
			{
				string? RelativePath;
				if (Line.Data.TryGetNestedProperty("properties.asset.relativePath", out RelativePath))
				{
					int EndIdx = RelativePath.LastIndexOfAny(new char[] { '/', '\\' }) + 1;
					string FileName = RelativePath.Substring(EndIdx);
					AssetNames.Add(FileName);
				}
			}
		}

		/// <inheritdoc/>
		public bool TryGetFingerprint(IJob Job, INode Node, ILogEventData EventData, [NotNullWhen(true)] out NewIssueFingerprint? Fingerprint)
		{
			if (!IsMatchingEventId(EventData.EventId))
			{
				Fingerprint = null;
				return false;
			}

			HashSet<string> NewAssetNames = new HashSet<string>();
			GetAssetNames(EventData, NewAssetNames);
			Fingerprint = new NewIssueFingerprint(Type, NewAssetNames, null);
			return true;
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint Fingerprint, IssueSeverity Severity)
		{
			string Type = (Severity == IssueSeverity.Warning) ? "Warnings" : "Errors";
			string List = StringUtils.FormatList(Fingerprint.Keys.ToArray(), 2);
			return $"{Type} in {List}";
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint Fingerprint, List<SuspectChange> Suspects)
		{
			foreach (SuspectChange Suspect in Suspects)
			{
				if (Suspect.ContainsContent)
				{
					if (Suspect.Details.Files.Any(x => Fingerprint.Keys.Any(y => x.Path.Contains(y, StringComparison.OrdinalIgnoreCase))))
					{
						Suspect.Rank += 20;
					}
					else
					{
						Suspect.Rank += 10;
					}
				}
			}
		}
	}
}
