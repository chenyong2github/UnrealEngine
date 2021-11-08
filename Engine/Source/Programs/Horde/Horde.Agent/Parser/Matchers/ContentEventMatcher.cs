// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;

namespace HordeAgent.Parser.Matchers
{
	/// <summary>
	/// Matches compile errors and annotates with the source file path and revision
	/// </summary>
	class ContentEventMatcher : ILogEventMatcher
	{
		const string Pattern = 
			@"^\s*" + 
			@"(?<channel>[a-zA-Z0-9]+):\s+" + 
			@"(?<severity>Error:|Warning:)\s+" + 
			@"(?:\[AssetLog\] )?" + 
			@"(?<asset>(?:[a-zA-Z]:)?[^:]+(?:.uasset|.umap)):\s*" + 
			@"(?<message>.*)";

		/// <inheritdoc/>
		public LogEventMatch? Match(ILogCursor Input)
		{
			// Do the match in two phases so we can early out if the strings "error" or "warning" are not present. The patterns before these strings can
			// produce many false positives, making them very slow to execute.
			if (Input.IsMatch("Error:|Warning:"))
			{
				Match? Match;
				if(Input.TryMatch(Pattern, out Match))
				{
					LogEventBuilder Builder = new LogEventBuilder(Input);

					Builder.Annotate(Match.Groups["channel"], LogEventMarkup.Channel);
					Builder.Annotate(Match.Groups["severity"], LogEventMarkup.Severity);
					//					Builder.Annotate(Match.Groups["asset"]).MarkAsAsset(Context);
					Builder.Annotate(Match.Groups["message"], LogEventMarkup.Message);

					return Builder.ToMatch(LogEventPriority.AboveNormal, GetLogLevelFromSeverity(Match), KnownLogEvents.Engine_AssetLog);
				}
			}
			return null;
		}

		static LogLevel GetLogLevelFromSeverity(Match Match)
		{
			string Severity = Match.Groups["severity"].Value;
			if (Severity.Equals("Warning:", StringComparison.Ordinal))
			{
				return LogLevel.Warning;
			}
			else
			{
				return LogLevel.Error;
			}
		}
	}
}
