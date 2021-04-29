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
		public LogEvent? Match(ILogCursor Cursor, ILogContext Context)
		{
			Match? Match;
			if(Cursor.TryMatch(@"^(?<indent>\s*)Error: EngineTest.RunTests Group:(?<group>[^\s]+) \(", out Match))
			{
				string Indent = Match.Groups["indent"].Value + " ";

				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				Builder.Lines[0].AddSpan(Match.Groups["group"]);

				string Group = Match.Groups["group"].Value;

				List<string> TestNames = new List<string>();

				bool bInErrorList = false;
				List<LogEventBuilder> ChildEvents = new List<LogEventBuilder>();

				int LineCount = 1;
				while (Cursor.IsMatch(LineCount, $"^(?:{Indent}.*|\\s*)$"))
				{
					if (bInErrorList)
					{
						Match? TestNameMatch;
						if (Cursor.IsMatch(LineCount, @"^\s*#{1,3} "))
						{
							bInErrorList = false;
						}
						else if (Cursor.TryMatch(LineCount, @"^\s*#####\s+(?<friendly_name>.*):\s*(?<name>\S+)\s*", out TestNameMatch))
						{
							LogEventBuilder ChildEventBuilder = new LogEventBuilder(Cursor.Rebase(LineCount));
							ChildEventBuilder.AddProperty("group", Group);

							LogEventLine Line = ChildEventBuilder.Lines[0];
							Line.AddSpan(TestNameMatch.Groups["name"]);
							Line.AddSpan(TestNameMatch.Groups["friendly_name"]);

							ChildEvents.Add(ChildEventBuilder);
						}
						else if(ChildEvents.Count > 0 && ChildEvents[ChildEvents.Count - 1].CurrentLineNumber == Cursor.CurrentLineNumber + LineCount)
						{
							ChildEvents[ChildEvents.Count - 1].LineCount++;
						}
					}
					else
					{
						if (Builder.Cursor.IsMatch(LineCount, @"^\s*### The following tests failed:"))
						{
							bInErrorList = true;
						}
					}
					LineCount++;
				}
				Builder.LineCount = LineCount;

				LogEvent Event = Builder.ToLogEvent(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Gauntlet);
				if(ChildEvents.Count > 0)
				{
					Event.ChildEvents = ChildEvents.ConvertAll(x => x.ToLogEvent(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Gauntlet_UnitTest));
				}
				return Event;
			}

			if (Cursor.TryMatch(@"Error: Screenshot '(?<screenshot>[^']+)' test failed", out Match))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				Builder.Lines[0].AddSpan(Match.Groups["screenshot"]).MarkAsScreenshotTest();
				return Builder.ToLogEvent(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Gauntlet_ScreenshotTest);
			}

			if (Cursor.TryMatch("\\[ERROR\\] (.*)$", out Match))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				return Builder.ToLogEvent(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Generic);
			}

			return null;
		}
	}
}
