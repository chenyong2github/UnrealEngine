// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Connections.Features;
using Microsoft.Extensions.Logging;
using Serilog.Formatting.Json;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Cryptography.X509Certificates;
using System.Text.Json;
using System.Threading.Tasks;
using System.Diagnostics.CodeAnalysis;
using HordeServer.Collections;
using EpicGames.Core;

namespace HordeServer.IssueHandlers.Impl
{
	/// <summary>
	/// Instance of a particular Gauntlet error
	/// </summary>
	class GauntletIssueHandler : IIssueHandler
	{
		/// <summary>
		/// Prefix for unit test keys
		/// </summary>
		const string UnitTestPrefix = "UnitTest:";

		/// <summary>
		/// Prefix for screenshot test keys
		/// </summary>
		const string ScreenshotTestPrefix = "Screenshot:";

		/// <inheritdoc/>
		public string Type => "Gauntlet";

		/// <inheritdoc/>
		public int Priority => 10;

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="EventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? EventId)
		{
			return EventId == KnownLogEvents.Gauntlet_UnitTest || EventId == KnownLogEvents.Gauntlet_ScreenshotTest;
		}

		/// <summary>
		/// Parses symbol names from a log event
		/// </summary>
		/// <param name="EventData">The log event data</param>
		/// <param name="UnitTestNames">Receives a set of the unit test names</param>
		private static void GetUnitTestNames(ILogEventData EventData, HashSet<string> UnitTestNames)
		{
			foreach (ILogEventLine Line in EventData.Lines)
			{
				string? Group = null;
				string? Name = null;

				string? Value;
				if (Line.Data.TryGetNestedProperty("properties.group", out Value))
				{
					Group = Value;
				}
				if (Line.Data.TryGetNestedProperty("properties.name", out Value))
				{
					Name = Value;
				}

				if (Group != null && Name != null)
				{
					UnitTestNames.Add($"{UnitTestPrefix}:{Group}/{Name}");
				}
			}
		}

		/// <summary>
		/// Parses screenshot test names from a log event
		/// </summary>
		/// <param name="EventData">The event data</param>
		/// <param name="ScreenshotTestNames">Receives the parsed screenshot test names</param>
		private static void GetScreenshotTestNames(ILogEventData EventData, HashSet<string> ScreenshotTestNames)
		{
			foreach (ILogEventLine Line in EventData.Lines)
			{
				if (Line.Data.TryGetProperty("properties", JsonValueKind.Object, out JsonElement Properties))
				{
					foreach (JsonProperty Property in Properties.EnumerateObject())
					{
						JsonElement Value = Property.Value;
						if (Value.ValueKind == JsonValueKind.Object && Value.HasStringProperty("type", "Screenshot") && Value.TryGetStringProperty("name", out string? Name))
						{
							ScreenshotTestNames.Add($"{ScreenshotTestPrefix}:{Name}");
						}
					}
				}
			}
		}

		/// <inheritdoc/>
		public bool TryGetFingerprint(IJob Job, INode Node, ILogEventData EventData, [NotNullWhen(true)] out NewIssueFingerprint? Fingerprint)
		{
			if(!IsMatchingEventId(EventData.EventId))
			{
				Fingerprint = null;
				return false;
			}

			HashSet<string> Keys = new HashSet<string>();
			GetUnitTestNames(EventData, Keys);
			GetScreenshotTestNames(EventData, Keys);

			if (Keys.Count == 0)
			{
				Fingerprint = null;
				return false;
			}

			Fingerprint = new NewIssueFingerprint(Type, Keys, null);
			return true;
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint Fingerprint, List<SuspectChange> Changes)
		{
			foreach (SuspectChange Change in Changes)
			{
				if (Change.ContainsCode)
				{
					Change.Rank += 10;
				}
			}
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint Fingerprint, IssueSeverity Severity)
		{
			List<string> UnitTestNames = Fingerprint.Keys.Where(x => x.StartsWith(UnitTestPrefix, StringComparison.Ordinal)).Select(x => x.Substring(UnitTestPrefix.Length + 1)).ToList();
			if (UnitTestNames.Count > 0)
			{
				HashSet<string> GroupNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
				HashSet<string> TestNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
				foreach (string UnitTestName in UnitTestNames)
				{
					int Idx = UnitTestName.IndexOf('/', StringComparison.OrdinalIgnoreCase);
					if (Idx != -1)
					{
						GroupNames.Add(UnitTestName.Substring(0, Idx));
					}
					TestNames.Add(UnitTestName.Substring(Idx + 1));
				}

				if (GroupNames.Count == 1)
				{
					return $"{GroupNames.First()} test failures: {StringUtils.FormatList(TestNames.ToArray(), 3)}";
				}
				else
				{
					return $"{StringUtils.FormatList(GroupNames.OrderBy(x => x).ToArray(), 100)} test failures";
				}
			}

			List<string> ScreenshotTestNames = Fingerprint.Keys.Where(x => x.StartsWith(ScreenshotTestPrefix, StringComparison.Ordinal)).Select(x => x.Substring(ScreenshotTestPrefix.Length + 1)).ToList();
			if (ScreenshotTestNames.Count > 0)
			{
				return $"Screenshot test failures: {StringUtils.FormatList(ScreenshotTestNames.ToArray(), 3)}";
			}

			return "Test failures";
		}
	}
}
