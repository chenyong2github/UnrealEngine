// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using EpicGames.Core;
using Horde.Build.Collections;
using Horde.Build.Models;
using Microsoft.Extensions.Logging;
using MongoDB.Driver;

namespace Horde.Build.IssueHandlers.Impl
{
	/// <summary>
	/// Instance of a particular systemic error
	/// </summary>
	class SystemicIssueHandler : IIssueHandler
	{
		/// <inheritdoc/>
		public string Type => "Systemic";

		/// <inheritdoc/>
		public int Priority => 10;

		/// <summary>
		///  Known systemic errors
		/// </summary>
		static readonly HashSet<EventId?> s_knownSystemic = new HashSet<EventId?> {  KnownLogEvents.Systemic, KnownLogEvents.Systemic_Xge, KnownLogEvents.Systemic_Xge_Standalone, 
																		KnownLogEvents.Systemic_Xge_ServiceNotRunning, KnownLogEvents.Systemic_Xge_BuildFailed, 
																		KnownLogEvents.Systemic_SlowDDC, KnownLogEvents.Systemic_Horde, KnownLogEvents.Systemic_Horde_ArtifactUpload,
																		KnownLogEvents.Horde, KnownLogEvents.Horde_InvalidPreflight};

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="eventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? eventId)
		{
			return s_knownSystemic.Contains(eventId);
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint fingerprint, List<SuspectChange> suspects)
		{
		}

		/// <inheritdoc/>
		public bool TryGetFingerprint(IJob job, INode node, IReadOnlyNodeAnnotations annotations, ILogEventData eventData, [NotNullWhen(true)] out NewIssueFingerprint? fingerprint)
		{
			fingerprint = null;

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
							fingerprint = new NewIssueFingerprint(Type, new[] { node.Name }, null, null);
							return true;
						}
					}
				}
			}

			if (!IsMatchingEventId(eventData.EventId))
			{				
				return false;
			}

			fingerprint = new NewIssueFingerprint(Type, new[] { node.Name }, null, null);
			return true;
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint fingerprint, IssueSeverity severity)
		{
			string type = (severity == IssueSeverity.Warning) ? "Systemic warnings" : "Systemic errors";
			string nodeName = fingerprint.Keys.FirstOrDefault() ?? "(unknown)";
			return $"{type} in {nodeName}";
		}
	}
}
