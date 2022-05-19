// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.RegularExpressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

#nullable enable

namespace AutomationUtils.Matchers
{
	/// <summary>
	/// Matcher for Gradle errors
	/// </summary>
	class GradleEventMatcher : ILogEventMatcher
	{
		public LogEventMatch? Match(ILogCursor cursor)
		{
			if (cursor.IsMatch(@"^\s*FAILURE:"))
			{
				int maxOffset = 1;
				for (; ; )
				{
					int newMaxOffset = maxOffset;
					while (cursor.IsMatch(newMaxOffset, @"^\s*$"))
					{
						newMaxOffset++;
					}

					Match? match;
					if (!cursor.TryMatch(newMaxOffset, @"^(\s*)\*", out match))
					{
						break;
					}
					maxOffset = newMaxOffset + 1;

					while (cursor.IsMatch(maxOffset, $"^{match.Groups[1].Value}"))
					{
						maxOffset++;
					}
				}

				LogEventBuilder builder = new LogEventBuilder(cursor, maxOffset);
				return builder.ToMatch(LogEventPriority.Normal, LogLevel.Error, KnownLogEvents.AutomationTool);
			}

			return null;
		}
	}
}

