// Copyright Epic Games, Inc. All Rights Reserved.

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
		public LogEvent? Match(ILogCursor Input, ILogContext Context)
		{
			// Do the match in two phases so we can early out if the strings "error" or "warning" are not present. The patterns before these strings can
			// produce many false positives, making them very slow to execute.
			if (Input.IsMatch("Error:|Warning:"))
			{
				Match? Match;
				if(Input.TryMatch(Pattern, out Match))
				{
					LogEventBuilder Builder = new LogEventBuilder(Input);

					LogEventLine FirstLine = Builder.Lines[0];
					FirstLine.AddSpan(Match.Groups["channel"]).MarkAsChannel();
					FirstLine.AddSpan(Match.Groups["severity"]).MarkAsSeverity();
					FirstLine.AddSpan(Match.Groups["asset"]).MarkAsAsset(Context);
					FirstLine.AddSpan(Match.Groups["message"]).MarkAsErrorMessage();

					LogLevel Level = GetLogLevelFromSeverity(Match);
					return Builder.ToLogEvent(LogEventPriority.AboveNormal, Level, KnownLogEvents.Engine_AssetLog);
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
