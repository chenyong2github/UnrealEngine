// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text.RegularExpressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

#nullable enable

namespace AutomationUtils.Matchers
{
	/// <summary>
	/// Low-priority matcher for generic error strings like "warning:" and "error:"
	/// </summary>
	class GenericEventMatcher : ILogEventMatcher
	{
		/// <inheritdoc/>
		public LogEventMatch? Match(ILogCursor cursor)
		{
			Match? match;
			if (cursor.TryMatch(@"^\s*(FATAL|fatal error):", out _))
			{
				return new LogEventBuilder(cursor).ToMatch(LogEventPriority.Low, LogLevel.Error, KnownLogEvents.Generic);
			}
			if (cursor.TryMatch(@"(?<!\w)(?i)(WARNING|ERROR) ?(\([^)]+\)|\[[^\]]+\])?: ", out match))
			{
				// Careful to match the first WARNING or ERROR in the line here.
				LogLevel level = LogLevel.Error;
				if(match.Groups[1].Value.Equals("WARNING", StringComparison.OrdinalIgnoreCase))
				{
					level = LogLevel.Warning;
				}

				LogEventBuilder builder = new LogEventBuilder(cursor);
				while (builder.Current.IsMatch(1, String.Format(@"^({0} | *$)", ExtractIndent(cursor[0]!))))
				{
					builder.MoveNext();
				}
				return builder.ToMatch(LogEventPriority.Lowest, level, KnownLogEvents.Generic);
			}
			if (cursor.IsMatch(@"[Ee]rror [A-Z]\d+\s:"))
			{
				return new LogEventBuilder(cursor).ToMatch(LogEventPriority.Lowest, LogLevel.Error, KnownLogEvents.Generic);
			}
			return null;
		}

		static string ExtractIndent(string line)
		{
			int length = 0;
			while (length < line.Length && line[length] == ' ')
			{
				length++;
			}
			return new string(' ', length);
		}
	}
}
