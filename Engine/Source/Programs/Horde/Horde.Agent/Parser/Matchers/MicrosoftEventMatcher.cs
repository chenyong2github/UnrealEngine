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
	/// Matcher for generic Microsoft errors (see https://docs.microsoft.com/en-us/cpp/build/formatting-the-output-of-a-custom-build-step-or-build-event?view=msvc-160)
	/// </summary>
	class MicrosoftEventMatcher : ILogEventMatcher
	{
		ILogContext Context;

		public MicrosoftEventMatcher(ILogContext Context)
		{
			this.Context = Context;
		}

		public LogEventMatch? Match(ILogCursor Cursor)
		{
			// filename(line# [, column#]) | toolname} : [ any text ] {error | warning} code+number:localizable string [ any text ]

			Match? Match;
			if (Cursor.TryMatch(@"(?<severity>(?:error|warning)) (?<code>[a-zA-Z]+[0-9]+)\s*:", out Match))
			{
				string Prefix = Cursor.CurrentLine!.Substring(0, Match.Index);

				Match FileOrToolMatch = Regex.Match(Prefix, @"^\s*(.*[^\s])\s*:");
				if (FileOrToolMatch.Success)
				{
					LogEventBuilder Builder = new LogEventBuilder(Cursor);

					Match FileMatch = Regex.Match(FileOrToolMatch.Value, @"^\s*(?<file>.*)\((?<line>\d+)(?:, (?<column>\d+))?\)\s*:$");
					if (FileMatch.Success)
					{
						Builder.AnnotateSourceFile(FileMatch.Groups["file"], Context, "");
						Builder.Annotate(FileMatch.Groups["line"], LogEventMarkup.LineNumber);
						Builder.TryAnnotate(FileMatch.Groups["column"]);
					}
					else
					{
						Builder.Annotate("tool", FileOrToolMatch.Groups[1], LogEventMarkup.ToolName);
					}

					Group Severity = Match.Groups["severity"];
					Builder.Annotate(Severity);
					Builder.Annotate(Match.Groups["code"]);
					return Builder.ToMatch(LogEventPriority.Normal, Severity.Value.Equals("error")? LogLevel.Error : LogLevel.Warning, KnownLogEvents.Microsoft);
				}
			}
			return null;
		}
	}
}
