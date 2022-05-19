// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.RegularExpressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

#nullable enable

namespace AutomationUtils.Matchers
{
	/// <summary>
	/// Matcher for Gauntlet unit tests
	/// </summary>
	class GauntletEventMatcher : ILogEventMatcher
	{
		public LogEventMatch? Match(ILogCursor cursor)
		{
			Match? match;
			if(cursor.TryMatch(@"^(?<indent>\s*)Error: EngineTest.RunTests Group:(?<group>[^\s]+) \(", out match))
			{
				string indent = match.Groups["indent"].Value + " ";

				LogEventBuilder builder = new LogEventBuilder(cursor);
				builder.Annotate(match.Groups["group"]);

				string group = match.Groups["group"].Value;

				bool inErrorList = false;
				//				List<LogEventBuilder> ChildEvents = new List<LogEventBuilder>();

				//				int LineCount = 1;
				EventId eventId = KnownLogEvents.Gauntlet;
				while (builder.Next.IsMatch($"^(?:{indent}.*|\\s*)$"))
				{
					builder.MoveNext();
					if (inErrorList)
					{
						Match? testNameMatch;
						if (builder.Current.IsMatch(@"^\s*#{1,3} "))
						{
							inErrorList = false;
						}
						else if (builder.Current.TryMatch(@"^\s*#####\s+(?<friendly_name>.*):\s*(?<name>\S+)\s*", out testNameMatch))
						{
							builder.AddProperty("group", group);//.Annotate().AddProperty("group", Group);

							builder.Annotate(testNameMatch.Groups["name"]);
							builder.Annotate(testNameMatch.Groups["friendly_name"]);

							eventId = KnownLogEvents.Gauntlet_UnitTest;//							ChildEvents.Add(ChildEventBuilder);
						}
					}
					else
					{
						if (builder.Current.IsMatch(@"^\s*### The following tests failed:"))
						{
							inErrorList = true;
						}
					}
				}

				return builder.ToMatch(LogEventPriority.High, LogLevel.Error, eventId);
			}

			if (cursor.TryMatch(@"Error: Screenshot '(?<screenshot>[^']+)' test failed", out match))
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				builder.Annotate(match.Groups["screenshot"], LogEventMarkup.ScreenshotTest);//.MarkAsScreenshotTest();
				return builder.ToMatch(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Gauntlet_ScreenshotTest);
			}

			if (cursor.TryMatch("\\[ERROR\\] (.*)$", out _))
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				return builder.ToMatch(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Generic);
			}

			return null;
		}
	}
}
