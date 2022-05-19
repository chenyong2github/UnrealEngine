// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;

#nullable enable

namespace AutomationUtils.Matchers
{
	/// <summary>
	/// Matcher for editor/UAT instances exiting with an error
	/// </summary>
	class ExitCodeEventMatcher : ILogEventMatcher
	{
		public LogEventMatch? Match(ILogCursor cursor)
		{
			int numLines = 0;
			for (; ; )
			{
				if (cursor.IsMatch(numLines, "Editor terminated with exit code [1-9]"))
				{
					numLines++;
				}
				else if (cursor.IsMatch(numLines, "AutomationTool exiting with ExitCode=[1-9]"))
				{
					numLines++;
				}
				else if (cursor.IsMatch(numLines, "BUILD FAILED"))
				{
					numLines++;
				}
				else if (cursor.IsMatch(numLines, "(Error executing.+)(tool returned code)(.+)"))
				{
					numLines++;
				}
				else
				{
					break;
				}
			}

			if (numLines > 0)
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				builder.MoveNext(numLines - 1);
				return builder.ToMatch(LogEventPriority.Low, LogLevel.Error, KnownLogEvents.ExitCode);
			}
			return null;
		}
	}
}

