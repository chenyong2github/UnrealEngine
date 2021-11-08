// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;

namespace HordeAgent.Parser.Matchers
{
	/// <summary>
	/// Matcher for editor/UAT instances exiting with an error
	/// </summary>
	class ExitCodeEventMatcher : ILogEventMatcher
	{
		public LogEventMatch? Match(ILogCursor Cursor)
		{
			int NumLines = 0;
			for (; ; )
			{
				if (Cursor.IsMatch(NumLines, "Editor terminated with exit code [1-9]"))
				{
					NumLines++;
				}
				else if (Cursor.IsMatch(NumLines, "AutomationTool exiting with ExitCode=[1-9]"))
				{
					NumLines++;
				}
				else if (Cursor.IsMatch(NumLines, "BUILD FAILED"))
				{
					NumLines++;
				}
				else
				{
					break;
				}
			}

			if (NumLines > 0)
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				Builder.MoveNext(NumLines - 1);
				return Builder.ToMatch(LogEventPriority.Low, LogLevel.Error, KnownLogEvents.Generic);
			}
			return null;
		}
	}
}

