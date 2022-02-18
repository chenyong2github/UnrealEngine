// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Collections;
using HordeCommon.Rpc;
using HordeServer.Services;
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
using System.Text.Json;
using HordeServer.Models;
using EpicGames.Core;

namespace HordeServer.IssueHandlers.Impl
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
		static HashSet<EventId?> KnownSystemic = new HashSet<EventId?> {  KnownLogEvents.Systemic, KnownLogEvents.Systemic_Xge, KnownLogEvents.Systemic_Xge_Standalone, 
																		KnownLogEvents.Systemic_Xge_ServiceNotRunning, KnownLogEvents.Systemic_Xge_BuildFailed, 
																		KnownLogEvents.Systemic_SlowDDC, KnownLogEvents.Systemic_Horde, KnownLogEvents.Systemic_Horde_ArtifactUpload,
																		KnownLogEvents.Horde, KnownLogEvents.Horde_InvalidPreflight};

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="EventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? EventId)
		{
			return KnownSystemic.Contains(EventId);
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint Fingerprint, List<SuspectChange> Suspects)
		{
		}

		/// <inheritdoc/>
		public bool TryGetFingerprint(IJob Job, INode Node, ILogEventData EventData, [NotNullWhen(true)] out NewIssueFingerprint? Fingerprint)
		{
			Fingerprint = null;

			if (EventData.EventId == KnownLogEvents.ExitCode)
			{
				for (int i = 0; i < EventData.Lines.Count; i++)
				{
					string Message = EventData.Lines[i].Message;
					if (Message.Contains("AutomationTool exiting with ExitCode", StringComparison.InvariantCultureIgnoreCase) || Message.Contains("BUILD FAILED", StringComparison.InvariantCultureIgnoreCase))
					{
						Fingerprint = new NewIssueFingerprint(Type, new[] { Node.Name }, null);
						return true;
					}
				}
			}

			if (!IsMatchingEventId(EventData.EventId))
			{				
				return false;
			}

			Fingerprint = new NewIssueFingerprint(Type, new[] { Node.Name }, null);
			return true;
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint Fingerprint, IssueSeverity Severity)
		{
			string Type = (Severity == IssueSeverity.Warning) ? "Systemic warnings" : "Systemic errors";
			string NodeName = Fingerprint.Keys.FirstOrDefault() ?? "(unknown)";
			return $"{Type} in {NodeName}";
		}

	}
}
