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
	/// Instance of a particular compile error
	/// </summary>
	class CompileIssueHandler : IIssueHandler
	{
		/// <inheritdoc/>
		public string Type => "Compile";

		/// <inheritdoc/>
		public int Priority => 10;

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="EventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? EventId)
		{
			return EventId == KnownLogEvents.Compiler || EventId == KnownLogEvents.AutomationTool_SourceFileLine || EventId == KnownLogEvents.MSBuild;
		}

		/// <summary>
		/// Extracts a list of source files from an event
		/// </summary>
		/// <param name="Event">The event data</param>
		/// <param name="SourceFiles">List of source files</param>
		static void GetSourceFiles(ILogEventData Event, List<string> SourceFiles)
		{
			foreach (ILogEventLine Line in Event.Lines)
			{
				JsonElement Properties;
				if (Line.Data.TryGetProperty("properties", out Properties) && Properties.ValueKind == JsonValueKind.Object)
				{
					foreach (JsonProperty Property in Properties.EnumerateObject())
					{
						if (Property.NameEquals("file") && Property.Value.ValueKind == JsonValueKind.String)
						{
							AddSourceFile(SourceFiles, Property.Value.GetString());
						}
						if (Property.Value.HasStringProperty("type", "SourceFile") && Property.Value.TryGetStringProperty("relativePath", out string? Value))
						{
							AddSourceFile(SourceFiles, Value);
						}
					}
				}
			}
		}

		/// <summary>
		/// Add a new source file to a list of unique source files
		/// </summary>
		/// <param name="SourceFiles">List of source files</param>
		/// <param name="RelativePath">File to add</param>
		static void AddSourceFile(List<string> SourceFiles, string RelativePath)
		{
			int EndIdx = RelativePath.LastIndexOfAny(new char[] { '/', '\\' }) + 1;

			string FileName = RelativePath.Substring(EndIdx);
			if (!SourceFiles.Any(x => x.Equals(FileName, StringComparison.OrdinalIgnoreCase)))
			{
				SourceFiles.Add(FileName);
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

			List<string> NewFileNames = new List<string>();
			GetSourceFiles(EventData, NewFileNames);
			Fingerprint = new NewIssueFingerprint(Type, NewFileNames, null);
			return true;
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint Fingerprint, IssueSeverity Severity)
		{
			string Type = (Severity == IssueSeverity.Warning) ? "Compile warnings" : "Compile errors";
			string List = StringUtils.FormatList(Fingerprint.Keys.ToArray(), 2);
			return $"{Type} in {List}";
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint Fingerprint, List<SuspectChange> Suspects)
		{
			foreach (SuspectChange Change in Suspects)
			{
				if (Change.ContainsCode)
				{
					if (Fingerprint.Keys.Any(x => Change.ModifiesFile(x)))
					{
						Change.Rank += 20;
					}
					else
					{
						Change.Rank += 10;
					}
				}
			}
		}
	}
}
