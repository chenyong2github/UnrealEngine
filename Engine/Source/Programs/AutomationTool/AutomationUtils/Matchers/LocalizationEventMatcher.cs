// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.RegularExpressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

#nullable enable

namespace AutomationUtils.Matchers
{
	/// <summary>
	/// Matches events formatted as UE log channel output by the localization commandlet
	/// </summary>
	class LocalizationEventMatcher : ILogEventMatcher
	{
		static readonly Regex s_pattern = new Regex(
			@"^(\s*)" +
			@"(?:\[[\d\.\-: ]+\])*" +
			@"(?<channel>LogLocTextHelper|LogGatherTextFromSourceCommandlet):\s*" +
			@"(?<severity>Error|Warning|Display):\s+" +
			@"(?<file>([a-zA-Z]:)?[^:/\\]*[/\\][^:]+[^\)])" +
			@"(?:\((?<line>\d+)\))?" +
			@":");

		/// <inheritdoc/>
		public LogEventMatch? Match(ILogCursor input)
		{
			Match? match = s_pattern.Match(input.CurrentLine!);
			if (match.Success)
			{
				LogEventBuilder builder = new LogEventBuilder(input);
				builder.Annotate(match.Groups["channel"], LogEventMarkup.Channel);
				builder.Annotate(match.Groups["severity"], LogEventMarkup.Severity);
				builder.AnnotateSourceFile(match.Groups["file"], "Engine");
				builder.Annotate(match.Groups["line"], LogEventMarkup.LineNumber);

				if (builder.Next.TryMatch(@"^\s+", out Match? indent))
				{
					while (builder.Next.IsMatch($"^{indent.Value}"))
					{
						builder.MoveNext();
					}
				}

				LogLevel level = match.Groups["severity"].Value switch
				{
					"Error" => LogLevel.Error,
					"Warning" => LogLevel.Warning,
					_ => LogLevel.Information,
				};

				return builder.ToMatch(LogEventPriority.High, level, KnownLogEvents.Engine_Localization);

			}
			return null;
		}
	}
}
