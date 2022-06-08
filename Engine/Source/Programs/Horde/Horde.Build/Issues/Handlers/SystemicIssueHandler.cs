// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using EpicGames.Core;
using Horde.Build.Jobs;
using Horde.Build.Jobs.Graphs;
using Horde.Build.Logs;
using Microsoft.Extensions.Logging;
using MongoDB.Driver;

namespace Horde.Build.Issues.Handlers
{
	/// <summary>
	/// Instance of a particular systemic error
	/// </summary>
	class SystemicIssueHandler : IssueHandler
	{
		/// <inheritdoc/>
		public override string Type => "Systemic";

		/// <inheritdoc/>
		public override int Priority => 10;

		/// <summary>
		///  Known systemic errors
		/// </summary>
		static readonly HashSet<EventId> s_knownSystemic = new HashSet<EventId> { KnownLogEvents.Horde, KnownLogEvents.Horde_InvalidPreflight };

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="eventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId eventId)
		{
			return s_knownSystemic.Contains(eventId) || (eventId.Id >= KnownLogEvents.Systemic.Id && eventId.Id <= KnownLogEvents.Systemic_Max.Id);
		}

		/// <inheritdoc/>
		public override void RankSuspects(IIssueFingerprint fingerprint, List<SuspectChange> suspects)
		{
		}

		/// <inheritdoc/>
		public override void TagEvents(IJob job, INode node, IReadOnlyNodeAnnotations annotations, IReadOnlyList<IssueEvent> stepEvents)
		{
			foreach (IssueEvent stepEvent in stepEvents)
			{
				if (stepEvent.EventId != null && MatchEvent(stepEvent.EventData))
				{
				}
			}
		}

		static bool MatchEvent(ILogEventData eventData)
		{
			if (eventData.EventId == KnownLogEvents.ExitCode)
			{
				for (int i = 0; i < eventData.Lines.Count; i++)
				{
					string message = eventData.Lines[i].Message;

					string[] systemicExitMessages = new string[] { "AutomationTool exiting with ExitCode", "BUILD FAILED", "tool returned code" };

					for (int j = 0; j < systemicExitMessages.Length; j++)
					{
						if (message.Contains(systemicExitMessages[j], StringComparison.InvariantCultureIgnoreCase))
						{
							return true;
						}
					}
				}
			}

			return eventData.EventId != null && IsMatchingEventId(eventData.EventId.Value);
		}

		/// <inheritdoc/>
		public override string GetSummary(IIssueFingerprint fingerprint, IssueSeverity severity)
		{
			string type = (severity == IssueSeverity.Warning) ? "Systemic warnings" : "Systemic errors";
			string nodeName = fingerprint.Keys.FirstOrDefault() ?? "(unknown)";
			return $"{type} in {nodeName}";
		}
	}
}
