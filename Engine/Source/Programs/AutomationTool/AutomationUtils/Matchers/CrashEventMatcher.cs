// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.RegularExpressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

#nullable enable

namespace AutomationUtils.Matchers
{
	/// <summary>
	/// Matcher for engine crashes
	/// </summary>
	class CrashEventMatcher : ILogEventMatcher
	{
		public LogEventMatch? Match(ILogCursor cursor)
		{
			if (cursor.IsMatch("begin: stack for UAT"))
			{
				for (int maxOffset = 1; maxOffset < 100; maxOffset++)
				{
					if (cursor.IsMatch(maxOffset, "end: stack for UAT"))
					{
						LogEventBuilder builder = new LogEventBuilder(cursor, lineCount: maxOffset + 1);
						return builder.ToMatch(LogEventPriority.BelowNormal, GetLogLevel(cursor), KnownLogEvents.Engine_Crash);
					}
				}
			}
			if (cursor.IsMatch("AutomationTool: Stack:"))
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				while (builder.Current.IsMatch(1, "AutomationTool: Stack:"))
				{
					builder.MoveNext();
				}
				return builder.ToMatch(LogEventPriority.Low, LogLevel.Error, KnownLogEvents.AutomationTool_Crash);
			}

			Match? match;
			if (cursor.TryMatch(@"ExitCode=(3|139|255)(?!\d)", out match))
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				builder.Annotate("exitCode", match.Groups[1]);
				return builder.ToMatch(LogEventPriority.Low, LogLevel.Error, KnownLogEvents.AutomationTool_CrashExitCode);
			}
			return null;
		}

		static LogLevel GetLogLevel(ILogCursor cursor)
		{
			if(cursor.IsMatch(0, "[Ee]rror:"))
			{
				return LogLevel.Error;
			}
			else if(cursor.IsMatch(0, "[Ww]arning:"))
			{
				return LogLevel.Warning;
			}
			else
			{
				return LogLevel.Information;
			}
		}
	}
}
