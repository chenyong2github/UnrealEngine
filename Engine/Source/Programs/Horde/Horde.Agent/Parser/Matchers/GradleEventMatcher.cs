// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System.Text.RegularExpressions;

namespace HordeAgent.Parser.Matchers
{
	/// <summary>
	/// Matcher for Gradle errors
	/// </summary>
	class GradleEventMatcher : ILogEventMatcher
	{
		public LogEventMatch? Match(ILogCursor Cursor)
		{
			if (Cursor.IsMatch(@"^\s*FAILURE:"))
			{
				int MaxOffset = 1;
				for (; ; )
				{
					int NewMaxOffset = MaxOffset;
					while (Cursor.IsMatch(NewMaxOffset, @"^\s*$"))
					{
						NewMaxOffset++;
					}

					Match? Match;
					if (!Cursor.TryMatch(NewMaxOffset, @"^(\s*)\*", out Match))
					{
						break;
					}
					MaxOffset = NewMaxOffset + 1;

					while (Cursor.IsMatch(MaxOffset, $"^{Match.Groups[1].Value}"))
					{
						MaxOffset++;
					}
				}

				LogEventBuilder Builder = new LogEventBuilder(Cursor, MaxOffset);
				return Builder.ToMatch(LogEventPriority.Normal, LogLevel.Error, KnownLogEvents.AutomationTool);
			}

			return null;
		}
	}
}

