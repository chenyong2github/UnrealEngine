// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.RegularExpressions;

namespace HordeAgent.Parser.Matchers
{
	/// <summary>
	/// Matches compile errors and annotates with the source file path and revision
	/// </summary>
	class SourceFileLineEventMatcher : ILogEventMatcher
	{
		const string SeverityPattern =
			"(?<severity>ERROR|WARNING)";

		const string FilePattern =
			@"(?<file>" +
				// optional drive letter
				@"(?:[a-zA-Z]:)?" +
				// any non-colon character
				@"[^:(\s]+" +
				// any filename character (not whitespace or slash)
				@"[^:(\s\\/]" +
			@")";

		const string LinePattern =
			@"(?<line>\d+)";

		ILogContext Context;

		public SourceFileLineEventMatcher(ILogContext Context)
		{
			this.Context = Context;
		}

		/// <inheritdoc/>
		public LogEventMatch? Match(ILogCursor Input)
		{
			Match? Match;
			if (Input.TryMatch($"^\\s*{SeverityPattern}: {FilePattern}(?:\\({LinePattern}\\))?: ", out Match))
			{
				LogLevel Level = GetLogLevelFromSeverity(Match);

				LogEventBuilder Builder = new LogEventBuilder(Input);

				Builder.AnnotateSourceFile(Match.Groups["file"], Context, "");
				Builder.Annotate(Match.Groups["severity"], LogEventMarkup.Severity);
				Builder.TryAnnotate(Match.Groups["line"], LogEventMarkup.LineNumber);

				EventId EventId;
				if (Input.IsMatch("copyright"))
				{
					EventId = KnownLogEvents.AutomationTool_MissingCopyright;
				}
				else
				{
					EventId = KnownLogEvents.AutomationTool_SourceFileLine;
				}

				return Builder.ToMatch(LogEventPriority.AboveNormal, Level, EventId);
			}
			return null;
		}

		static LogLevel GetLogLevelFromSeverity(Match Match)
		{
			string Severity = Match.Groups["severity"].Value;
			if (Severity.Equals("WARNING", StringComparison.Ordinal))
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
