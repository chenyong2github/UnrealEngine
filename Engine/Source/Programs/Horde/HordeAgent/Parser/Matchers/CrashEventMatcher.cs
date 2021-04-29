// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Parser;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace HordeAgent.Parser.Matchers
{
	/// <summary>
	/// Matcher for engine crashes
	/// </summary>
	class CrashEventMatcher : ILogEventMatcher
	{
		public LogEvent? Match(ILogCursor Cursor, ILogContext Context)
		{
			if (Cursor.IsMatch("begin: stack for UAT"))
			{
				for (int MaxOffset = 1; MaxOffset < 100; MaxOffset++)
				{
					if (Cursor.IsMatch(MaxOffset, "end: stack for UAT"))
					{
						LogEventBuilder Builder = new LogEventBuilder(Cursor);
						Builder.MaxOffset = MaxOffset;
						return Builder.ToLogEvent(LogEventPriority.BelowNormal, GetLogLevel(Cursor), KnownLogEvents.Engine_Crash);
					}
				}
			}
			if (Cursor.IsMatch("AutomationTool: Stack:"))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				while (Cursor.IsMatch(Builder.MaxOffset + 1, "AutomationTool: Stack:"))
				{
					Builder.MaxOffset++;
				}
				return Builder.ToLogEvent(LogEventPriority.Low, LogLevel.Error, KnownLogEvents.AutomationTool_Crash);
			}

			Match? Match;
			if (Cursor.TryMatch("ExitCode=(3|139|255)", out Match))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				Builder.Lines[0].AddSpan(Match.Groups[1], "exitCode");
				return Builder.ToLogEvent(LogEventPriority.Low, LogLevel.Error, KnownLogEvents.AutomationTool_CrashExitCode);
			}
			return null;
		}

		static LogLevel GetLogLevel(ILogCursor Cursor)
		{
			if(Cursor.IsMatch(0, "[Ee]rror:"))
			{
				return LogLevel.Error;
			}
			else if(Cursor.IsMatch(0, "[Ww]arning:"))
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
