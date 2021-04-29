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

		/// <inheritdoc/>
		public LogEvent? Match(ILogCursor Input, ILogContext Context)
		{
			Match? Match;
			if (Input.TryMatch($"^\\s*{SeverityPattern}: {FilePattern}(?:\\({LinePattern}\\))?: ", out Match))
			{
				LogLevel Level = GetLogLevelFromSeverity(Match);

				LogEventBuilder Builder = new LogEventBuilder(Input);

				LogEventLine FirstLine = Builder.Lines[0];
				FirstLine.AddSpan(Match.Groups["file"]).MarkAsSourceFile(Context, "");
				FirstLine.AddSpan(Match.Groups["severity"]).MarkAsSeverity();
				FirstLine.TryAddSpan(Match.Groups["line"])?.MarkAsLineNumber();

				EventId EventId;
				if (Input.IsMatch("copyright"))
				{
					EventId = KnownLogEvents.AutomationTool_MissingCopyright;
				}
				else
				{
					EventId = KnownLogEvents.AutomationTool_SourceFileLine;
				}

				return Builder.ToLogEvent(LogEventPriority.AboveNormal, Level, EventId);
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
