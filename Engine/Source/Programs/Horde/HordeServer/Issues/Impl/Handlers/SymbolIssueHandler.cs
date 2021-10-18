// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using EpicGames.Core;

namespace HordeServer.IssueHandlers.Impl
{
	/// <summary>
	/// Instance of a particular compile error
	/// </summary>
	class SymbolIssueHandler : IIssueHandler
	{
		/// <inheritdoc/>
		public string Type => "Symbol";

		/// <inheritdoc/>
		public int Priority => 10;

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="EventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? EventId)
		{
			return EventId == KnownLogEvents.Linker_UndefinedSymbol || EventId == KnownLogEvents.Linker_DuplicateSymbol || EventId == KnownLogEvents.Linker;
		}

		/// <summary>
		/// Parses symbol names from a log event
		/// </summary>
		/// <param name="EventData">The log event data</param>
		/// <param name="SymbolNames">Receives the list of symbol names</param>
		public static void GetSymbolNames(ILogEventData EventData, SortedSet<string> SymbolNames)
		{
			foreach (ILogEventLine Line in EventData.Lines)
			{
				string? Identifier;
				if (Line.Data.TryGetNestedProperty("properties.symbol.identifier", out Identifier))
				{
					SymbolNames.Add(Identifier);
				}
			}
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint Fingerprint, List<SuspectChange> Changes)
		{
			HashSet<string> Names = new HashSet<string>();
			foreach (string Name in Fingerprint.Keys)
			{
				Names.UnionWith(Name.Split("::", StringSplitOptions.RemoveEmptyEntries));
			}

			foreach (SuspectChange Change in Changes)
			{
				if (Change.ContainsCode)
				{
					int Matches = Names.Count(x => Change.Details.Files.Any(y => y.Path.Contains(x, StringComparison.OrdinalIgnoreCase)));
					Change.Rank += 10 + (10 * Matches);
				}
			}
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint Fingerprint, IssueSeverity Severity)
		{
			HashSet<string> Symbols = Fingerprint.Keys;
			if (Symbols.Count == 1)
			{
				return $"Undefined symbol '{Symbols.First()}'";
			}
			else
			{
				return $"Undefined symbols: {StringUtils.FormatList(Symbols.ToArray(), 3)}";
			}
		}

		public bool TryGetFingerprint(IJob Job, INode Node, ILogEventData EventData, [NotNullWhen(true)] out NewIssueFingerprint? Fingerprint)
		{
			if (!IsMatchingEventId(EventData.EventId))
			{
				Fingerprint = null;
				return false;
			}

			SortedSet<string> SymbolNames = new SortedSet<string>();
			GetSymbolNames(EventData, SymbolNames);
			Fingerprint = new NewIssueFingerprint(Type, SymbolNames, null);
			return true;
		}
	}
}
