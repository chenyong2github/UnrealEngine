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
		/// <summary>
		/// Prefix used to identify files that may match against modified files, but which are not the files failing to compile
		/// </summary>
		const string NotePrefix = "note:";

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
					string? Prefix = null;

					JsonElement NoteElement;
					if (Properties.TryGetProperty("note", out NoteElement) && NoteElement.GetBoolean())
					{
						Prefix = NotePrefix;
					}

					foreach (JsonProperty Property in Properties.EnumerateObject())
					{
						if (Property.NameEquals("file") && Property.Value.ValueKind == JsonValueKind.String)
						{
							AddSourceFile(SourceFiles, Property.Value.GetString()!, Prefix);
						}
						if (Property.Value.HasStringProperty("$type", "SourceFile") && Property.Value.TryGetStringProperty("relativePath", out string? Value))
						{
							AddSourceFile(SourceFiles, Value, Prefix);
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
		/// <param name="Prefix">Prefix to insert at the start of the filename</param>
		static void AddSourceFile(List<string> SourceFiles, string RelativePath, string? Prefix)
		{
			int EndIdx = RelativePath.LastIndexOfAny(new char[] { '/', '\\' }) + 1;

			string FileName = RelativePath.Substring(EndIdx);
			if (Prefix != null)
			{
				FileName = Prefix + FileName;
			}

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
			string List = StringUtils.FormatList(Fingerprint.Keys.Where(x => !x.StartsWith(NotePrefix, StringComparison.Ordinal)).ToArray(), 2);
			return $"{Type} in {List}";
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint Fingerprint, List<SuspectChange> Suspects)
		{
			List<string> FileNames = new List<string>();
			foreach (string Key in Fingerprint.Keys)
			{
				if (Key.StartsWith(NotePrefix, StringComparison.Ordinal))
				{
					FileNames.Add(Key.Substring(NotePrefix.Length));
				}
				else
				{
					FileNames.Add(Key);
				}
			}

			foreach (SuspectChange Change in Suspects)
			{
				if (Change.ContainsCode)
				{
					if (FileNames.Any(x => Change.ModifiesFile(x)))
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
