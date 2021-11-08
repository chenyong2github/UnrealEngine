// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;

namespace HordeAgent.Parser.Matchers
{
	/// <summary>
	/// Matcher for Gauntlet unit tests
	/// </summary>
	class GauntletEventMatcher : ILogEventMatcher
	{
		public LogEventMatch? Match(ILogCursor Cursor)
		{
			Match? Match;
			if(Cursor.TryMatch(@"^(?<indent>\s*)Error: EngineTest.RunTests Group:(?<group>[^\s]+) \(", out Match))
			{
				string Indent = Match.Groups["indent"].Value + " ";

				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				Builder.Annotate(Match.Groups["group"]);

				string Group = Match.Groups["group"].Value;

				List<string> TestNames = new List<string>();

				bool bInErrorList = false;
				//				List<LogEventBuilder> ChildEvents = new List<LogEventBuilder>();

				//				int LineCount = 1;
				EventId EventId = KnownLogEvents.Gauntlet;
				while (Builder.Next.IsMatch($"^(?:{Indent}.*|\\s*)$"))
				{
					Builder.MoveNext();
					if (bInErrorList)
					{
						Match? TestNameMatch;
						if (Builder.Current.IsMatch(@"^\s*#{1,3} "))
						{
							bInErrorList = false;
						}
						else if (Builder.Current.TryMatch(@"^\s*#####\s+(?<friendly_name>.*):\s*(?<name>\S+)\s*", out TestNameMatch))
						{
							Builder.AddProperty("group", Group);//.Annotate().AddProperty("group", Group);

							Builder.Annotate(TestNameMatch.Groups["name"]);
							Builder.Annotate(TestNameMatch.Groups["friendly_name"]);

							EventId = KnownLogEvents.Gauntlet_UnitTest;//							ChildEvents.Add(ChildEventBuilder);
						}
					}
					else
					{
						if (Builder.Current.IsMatch(@"^\s*### The following tests failed:"))
						{
							bInErrorList = true;
						}
					}
				}

				return Builder.ToMatch(LogEventPriority.High, LogLevel.Error, EventId);
			}

			if (Cursor.TryMatch(@"Error: Screenshot '(?<screenshot>[^']+)' test failed", out Match))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				Builder.Annotate(Match.Groups["screenshot"], LogEventMarkup.ScreenshotTest);//.MarkAsScreenshotTest();
				return Builder.ToMatch(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Gauntlet_ScreenshotTest);
			}

			if (Cursor.TryMatch("\\[ERROR\\] (.*)$", out Match))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				return Builder.ToMatch(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Generic);
			}

			return null;
		}
	}
}
