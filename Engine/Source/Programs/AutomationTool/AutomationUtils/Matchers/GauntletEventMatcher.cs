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
		static readonly Regex s_engineTestPattern = new Regex(@"^(?<indent>\s*)## Error: UE\.(?<target>\w+?)Automation\(RunTest=(?<group>[^\s]+)\) \(");

		static readonly Regex s_mdHeadingPattern = new Regex(@"^\s*#{1,3} ");
		static readonly Regex s_mdTestListPattern = new Regex(@"^\s*### The following tests failed:");
		static readonly Regex s_mdTestNamePattern = new Regex(@"^\s*#####\s+Error: Test '(?<name>\S+)'");
		static readonly Regex s_mdTestFullNamePattern = new Regex(@"^\s*FullName:\s+(?<fullname>\S+)");

		static readonly Regex s_genericPattern = new Regex("\\[ERROR\\] (.*)$");

		public LogEventMatch? Match(ILogCursor cursor)
		{
			Match? match;
			if(cursor.TryMatch(s_engineTestPattern, out match))
			{
				string indent = match.Groups["indent"].Value + " ";

				LogEventBuilder builder = new LogEventBuilder(cursor);
				builder.Annotate(match.Groups["group"]);

				string group = match.Groups["group"].Value;

				bool inErrorList = false;
				//				List<LogEventBuilder> ChildEvents = new List<LogEventBuilder>();

				//				int LineCount = 1;
				EventId eventId = KnownLogEvents.Gauntlet;
				while (builder.Next.CurrentLine != null && builder.IsNextLineAligned())
				{
					builder.MoveNext();
					if (inErrorList)
					{
						Match? testNameMatch;
						if (builder.Current.IsMatch(s_mdHeadingPattern))
						{
							inErrorList = false;
						}
						else if (builder.Current.TryMatch(s_mdTestNamePattern, out testNameMatch))
						{
							builder.AddProperty("group", group);//.Annotate().AddProperty("group", Group);

							builder.Annotate(testNameMatch.Groups["name"]);

							builder.MoveNext();
							Match? testFullNameMatch;
							if (builder.IsNextLineAligned() && builder.Current.TryMatch(s_mdTestFullNamePattern, out testFullNameMatch))
							{
								builder.Annotate(testFullNameMatch.Groups["fullname"]);
							}

							eventId = KnownLogEvents.Gauntlet_UnrealEngineTestEvent;//							ChildEvents.Add(ChildEventBuilder);
						}
					}
					else
					{
						if (builder.Current.IsMatch(s_mdTestListPattern))
						{
							inErrorList = true;
						}
					}
				}

				return builder.ToMatch(LogEventPriority.High, LogLevel.Error, eventId);
			}

			if (cursor.TryMatch(s_genericPattern, out _))
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				return builder.ToMatch(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Generic);
			}

			return null;
		}
	}
}
