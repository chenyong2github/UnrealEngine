// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text.RegularExpressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Parser.Matchers
{
	/// <summary>
	/// Matches compile errors and annotates with the source file path and revision
	/// </summary>
	class CompileEventMatcher : ILogEventMatcher
	{
		const string FilePattern =
			@"(?<file>" +
				// optional drive letter
				@"(?:[a-zA-Z]:)?" +
				// any non-colon character
				@"[^:(\s]+" +
				// any path character
				@"[^:<>*?""]+" +
				// valid source file extension
				@"\.(?:(?i)(?:h|c|cc|cpp|inc|inl|cs|targets))" +
			@")";

		const string VisualCppLocationPattern =
			@"\(" +
				@"(?<line>\d+)" + // line number
				@"(?:,(?<column>\d+))?" + // optional column number
			@"\)";

		const string VisualCppSeverity =
			@"(?<severity>fatal error|error|warning)(?: (?<code>[A-Z]+[0-9]+))?";

		const string ClangLocationPattern =
			@":" +
				@"(?<line>\d+)" + // line number
				@"(?::(?<column>\d+))?" + // optional column number
			@"";

		const string ClangSeverity =
			@"(?<severity>error|warning)";

		static readonly string[] s_invalidExtensions =
		{
			".obj",
			".dll",
			".exe"
		};

		const string DefaultSourceFileBaseDir = "Engine/Source";

		/// <inheritdoc/>
		public LogEventMatch? Match(ILogCursor input)
		{
			// Match the prelude to any error
			int maxOffset = 0;
			while (input.IsMatch(maxOffset, "^\\s*(?:In (member )?function|In file included from)"))
			{
				maxOffset++;
			}

			// Do the match in two phases so we can early out if the strings "error" or "warning" are not present. The patterns before these strings can
			// produce many false positives, making them very slow to execute.
			if (input.IsMatch(maxOffset, "error|warning"))
			{
				LogEventBuilder builder = new LogEventBuilder(input, maxOffset + 1);

				// Try to match a Visual C++ diagnostic
				LogEventMatch? eventMatch;
				if(TryMatchVisualCppEvent(builder, out eventMatch))
				{
					LogEvent newEvent = eventMatch!.Events[eventMatch.Events.Count - 1];

					// If warnings as errors is enabled, upgrade any following warnings to errors.
					object? codeProp;
					if(newEvent.Properties != null && newEvent.Properties.TryGetValue("code", out codeProp) && codeProp is LogValue code && code.Text == "C2220")
					{
						ILogCursor nextCursor = builder.Next;
						while (nextCursor.CurrentLine != null)
						{
							LogEventBuilder nextBuilder = new LogEventBuilder(nextCursor);

							LogEventMatch? nextMatch;
							if (!TryMatchVisualCppEvent(nextBuilder, out nextMatch))
							{
								break;
							}
							foreach (LogEvent matchEvent in nextMatch.Events)
							{
								matchEvent.Level = LogLevel.Error;
							}
							eventMatch.Events.AddRange(nextMatch.Events);

							nextCursor = nextBuilder.Next;
						}
					}
					return eventMatch;
				}

				// Try to match a Clang diagnostic
				Match? match;
				if (builder.Current.TryMatch($"^\\s*{FilePattern}\\s*{ClangLocationPattern}:\\s*{ClangSeverity}\\s*:", out match) && IsSourceFile(match))
				{
					LogLevel level = GetLogLevelFromSeverity(match);

					builder.AnnotateSourceFile(match.Groups["file"], "Engine/Source");
					builder.Annotate(match.Groups["severity"], LogEventMarkup.Severity);
					builder.TryAnnotate(match.Groups["line"], LogEventMarkup.LineNumber);
					builder.TryAnnotate(match.Groups["column"], LogEventMarkup.ColumnNumber);

					string indent = ExtractIndent(input[0]!);

					while (builder.Current.TryMatch(1, $"^(?:{indent} |{indent}\\s*{FilePattern}\\s*{ClangLocationPattern}\\s*note:| *$)", out match))
					{
						builder.MoveNext();

						Group fileGroup = match.Groups["file"];
						if (fileGroup.Success)
						{
							builder.AnnotateSourceFile(fileGroup, DefaultSourceFileBaseDir);
							builder.TryAnnotate(match.Groups["line"], LogEventMarkup.LineNumber);
						}
					}

					return builder.ToMatch(LogEventPriority.High, level, KnownLogEvents.Compiler);
				}
			}
			return null;
		}

		bool TryMatchVisualCppEvent(LogEventBuilder builder, [NotNullWhen(true)] out LogEventMatch? outEvent)
		{
			Match? match;
			if(!builder.Current.TryMatch($"^\\s*(?:ERROR: |WARNING: )?{FilePattern}(?:{VisualCppLocationPattern})? ?:\\s+{VisualCppSeverity}:", out match) || !IsSourceFile(match))
			{
				outEvent = null;
				return false;
			}

			LogLevel level = GetLogLevelFromSeverity(match);
			builder.Annotate(match.Groups["severity"], LogEventMarkup.Severity);

			string sourceFileBaseDir = DefaultSourceFileBaseDir;

			Group codeGroup = match.Groups["code"];
			if (codeGroup.Success)
			{
				builder.Annotate(codeGroup, LogEventMarkup.ErrorCode);

				if (codeGroup.Value.StartsWith("CS", StringComparison.Ordinal))
				{
					Match? projectMatch;
					if (builder.Current.TryMatch(@"\[(?<project>[^[\]]+)]\s*$", out projectMatch))
					{
						builder.AnnotateSourceFile(projectMatch.Groups[1], "");
						sourceFileBaseDir = GetPlatformAgnosticDirectoryName(projectMatch.Groups[1].Value) ?? sourceFileBaseDir;
					}
				}
				else if (codeGroup.Value.StartsWith("MSB", StringComparison.Ordinal))
				{
					if (codeGroup.Value.Equals("MSB3026", StringComparison.Ordinal))
					{
						outEvent = builder.ToMatch(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_MSBuild);
						return true;
					}

					Match? projectMatch;
					if (builder.Current.TryMatch(@"\[(?<file>[^[\]]+)]\s*$", out projectMatch))
					{
						builder.AnnotateSourceFile(projectMatch.Groups[1], "");
						outEvent = builder.ToMatch(LogEventPriority.High, level, KnownLogEvents.MSBuild);
						return true;
					}
				}
			}

			builder.AnnotateSourceFile(match.Groups["file"], sourceFileBaseDir);
			builder.TryAnnotate(match.Groups["line"], LogEventMarkup.LineNumber);
			builder.TryAnnotate(match.Groups["column"], LogEventMarkup.ColumnNumber);

			string indent = ExtractIndent(builder.Current.CurrentLine ?? String.Empty);

			while (builder.Current.TryMatch(1, $"^(?:{indent} |{indent}\\s*{FilePattern}(?:{VisualCppLocationPattern})?\\s*: note:| *$)", out match))
			{
				builder.MoveNext();

				Group group = match.Groups["file"];
				if (group.Success)
				{
					builder.AnnotateSourceFile(group, DefaultSourceFileBaseDir);
					builder.TryAnnotate(match.Groups["line"], LogEventMarkup.LineNumber);
					builder.AddProperty("note", true);
				}
			}

			string pattern = $"^{indent} |: note:";
			while (builder.Current.IsMatch(1, pattern))
			{
				builder.MoveNext();
			}

			outEvent = builder.ToMatch(LogEventPriority.High, level, KnownLogEvents.Compiler);
			return true;
		}

		static string? GetPlatformAgnosticDirectoryName(string fileName)
		{
			int index = fileName.LastIndexOfAny(new[] { '/', '\\' });
			if (index == -1)
			{
				return null;
			}
			else
			{
				return fileName.Substring(0, index);
			}
		}

		bool IsSourceFile(Match match)
		{
			Group group = match.Groups["file"];
			if (!group.Success)
			{
				return false;
			}

			string text = group.Value;
			if (s_invalidExtensions.Any(x => text.EndsWith(x, StringComparison.OrdinalIgnoreCase)))
			{
				return false;
			}

			return true;
		}

		static LogLevel GetLogLevelFromSeverity(Match match)
		{
			string severity = match.Groups["severity"].Value;
			if(severity.Equals("warning", StringComparison.Ordinal))
			{
				return LogLevel.Warning;
			}
			else
			{
				return LogLevel.Error;
			}
		}

		static string ExtractIndent(string line)
		{
			int length = 0;
			while(length < line.Length && line[length] == ' ')
			{
				length++;
			}
			return new string(' ', length);
		}
	}
}
