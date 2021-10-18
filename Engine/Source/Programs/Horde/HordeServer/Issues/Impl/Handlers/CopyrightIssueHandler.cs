// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Collections;
using HordeCommon.Rpc;
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
	class CopyrightIssueHandler : IIssueHandler
	{
		/// <inheritdoc/>
		public string Type => "Copyright";

		/// <inheritdoc/>
		public int Priority => 10;

		/// <summary>
		/// Filenames containing errors or warnings
		/// </summary>
		public HashSet<string> FileNames { get; set; } = new HashSet<string>();

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="EventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? EventId)
		{
			return EventId == KnownLogEvents.AutomationTool_MissingCopyright;
		}

		/// <summary>
		/// Extracts a list of source files from an event
		/// </summary>
		/// <param name="Event">The event data</param>
		/// <param name="SourceFiles">List of source files</param>
		public static void GetSourceFiles(ILogEventData Event, HashSet<string> SourceFiles)
		{
			foreach (ILogEventLine Line in Event.Lines)
			{
				string? RelativePath;
				if (Line.Data.TryGetNestedProperty("properties.file.relativePath", out RelativePath) || Line.Data.TryGetNestedProperty("properties.file", out RelativePath))
				{
					int EndIdx = RelativePath.LastIndexOfAny(new char[] { '/', '\\' }) + 1;
					string FileName = RelativePath.Substring(EndIdx);
					SourceFiles.Add(FileName);
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

			HashSet<string> NewFileNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			GetSourceFiles(EventData, NewFileNames);
			Fingerprint = new NewIssueFingerprint(Type, NewFileNames, null);
			return true;
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint Fingerprint, List<SuspectChange> Suspects)
		{
			foreach (SuspectChange Suspect in Suspects)
			{
				if (Suspect.ContainsCode)
				{
					if (FileNames.Any(x => Suspect.ModifiesFile(x)))
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

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint Fingerprint, IssueSeverity Severity)
		{
			return $"Missing copyright notice in {StringUtils.FormatList(Fingerprint.Keys.ToArray(), 2)}";
		}
	}
}
