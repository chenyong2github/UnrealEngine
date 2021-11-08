// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace HordeAgent.Parser.Matchers
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

		static string ClangOrVisualCppLinePattern =
			$"(?:{VisualCppLocationPattern}|{ClangLocationPattern})";

		static readonly string[] InvalidExtensions =
		{
			".obj",
			".dll",
			".exe"
		};

		const string DefaultSourceFileBaseDir = "Engine/Source";

		ILogContext Context;

		public CompileEventMatcher(ILogContext Context)
		{
			this.Context = Context;
		}

		/// <inheritdoc/>
		public LogEventMatch? Match(ILogCursor Input)
		{
			// Match the prelude to any error
			int MaxOffset = 0;
			while (Input.IsMatch(MaxOffset, "^\\s*(?:In (member )?function|In file included from)"))
			{
				MaxOffset++;
			}

			// Do the match in two phases so we can early out if the strings "error" or "warning" are not present. The patterns before these strings can
			// produce many false positives, making them very slow to execute.
			if (Input.IsMatch(MaxOffset, "error|warning"))
			{
				LogEventBuilder Builder = new LogEventBuilder(Input, MaxOffset + 1);

				// Try to match a Visual C++ diagnostic
				LogEventMatch? EventMatch;
				if(TryMatchVisualCppEvent(Builder, out EventMatch))
				{
					LogEvent NewEvent = EventMatch!.Events[EventMatch.Events.Count - 1];

					// If warnings as errors is enabled, upgrade any following warnings to errors.
					object? CodeProp;
					if(NewEvent.Properties != null && NewEvent.Properties.TryGetValue("code", out CodeProp) && CodeProp is LogValue Code && Code.Text == "C2220")
					{
						ILogCursor NextCursor = Builder.Next;
						while (NextCursor.CurrentLine != null)
						{
							LogEventBuilder NextBuilder = new LogEventBuilder(NextCursor);

							LogEventMatch? NextMatch;
							if (!TryMatchVisualCppEvent(NextBuilder, out NextMatch))
							{
								break;
							}
							foreach (LogEvent Event in NextMatch.Events)
							{
								Event.Level = LogLevel.Error;
							}
							EventMatch.Events.AddRange(NextMatch.Events);

							NextCursor = NextBuilder.Next;
						}
					}
					return EventMatch;
				}

				// Try to match a Clang diagnostic
				Match? Match;
				if (Builder.Current.TryMatch($"^\\s*{FilePattern}\\s*{ClangLocationPattern}:\\s*{ClangSeverity}\\s*:", out Match) && IsSourceFile(Match))
				{
					LogLevel Level = GetLogLevelFromSeverity(Match);

					Builder.AnnotateSourceFile(Match.Groups["file"], Context, "Engine/Source");
					Builder.Annotate(Match.Groups["severity"], LogEventMarkup.Severity);
					Builder.TryAnnotate(Match.Groups["line"], LogEventMarkup.LineNumber);
					Builder.TryAnnotate(Match.Groups["column"], LogEventMarkup.ColumnNumber);

					string Indent = ExtractIndent(Input[0]!);

					while (Builder.Current.TryMatch(1, $"^(?:{Indent} |{Indent}\\s*{FilePattern}\\s*{ClangLocationPattern}\\s*note:| *$)", out Match))
					{
						Builder.MoveNext();

						Group FileGroup = Match.Groups["file"];
						if (FileGroup.Success)
						{
							Builder.AnnotateSourceFile(FileGroup, Context, DefaultSourceFileBaseDir);
							Builder.TryAnnotate(Match.Groups["line"], LogEventMarkup.LineNumber);
						}
					}

					return Builder.ToMatch(LogEventPriority.High, Level, KnownLogEvents.Compiler);
				}
			}
			return null;
		}

		bool TryMatchVisualCppEvent(LogEventBuilder Builder, [NotNullWhen(true)] out LogEventMatch? OutEvent)
		{
			Match? Match;
			if(!Builder.Current.TryMatch($"^\\s*(?:ERROR: |WARNING: )?{FilePattern}(?:{VisualCppLocationPattern})? ?:\\s+{VisualCppSeverity}:", out Match) || !IsSourceFile(Match))
			{
				OutEvent = null;
				return false;
			}

			LogLevel Level = GetLogLevelFromSeverity(Match);
			Builder.Annotate(Match.Groups["severity"], LogEventMarkup.Severity);

			string SourceFileBaseDir = DefaultSourceFileBaseDir;

			Group CodeGroup = Match.Groups["code"];
			if (CodeGroup.Success)
			{
				Builder.Annotate(CodeGroup, LogEventMarkup.ErrorCode);

				if (CodeGroup.Value.StartsWith("CS", StringComparison.Ordinal))
				{
					Match? ProjectMatch;
					if (Builder.Current.TryMatch(@"\[(?<project>[^[\]]+)]\s*$", out ProjectMatch))
					{
						Builder.AnnotateSourceFile(ProjectMatch.Groups[1], Context, "");
						SourceFileBaseDir = GetPlatformAgnosticDirectoryName(ProjectMatch.Groups[1].Value) ?? SourceFileBaseDir;
					}
				}
				else if (CodeGroup.Value.StartsWith("MSB", StringComparison.Ordinal))
				{
					if (CodeGroup.Value.Equals("MSB3026", StringComparison.Ordinal))
					{
						OutEvent = Builder.ToMatch(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_MSBuild);
						return true;
					}

					Match? ProjectMatch;
					if (Builder.Current.TryMatch(@"\[(?<file>[^[\]]+)]\s*$", out ProjectMatch))
					{
						Builder.AnnotateSourceFile(ProjectMatch.Groups[1], Context, "");
						OutEvent = Builder.ToMatch(LogEventPriority.High, Level, KnownLogEvents.MSBuild);
						return true;
					}
				}
			}

			Builder.AnnotateSourceFile(Match.Groups["file"], Context, SourceFileBaseDir);
			Builder.TryAnnotate(Match.Groups["line"], LogEventMarkup.LineNumber);
			Builder.TryAnnotate(Match.Groups["column"], LogEventMarkup.ColumnNumber);

			string Indent = ExtractIndent(Builder.Current.CurrentLine ?? String.Empty);

			while (Builder.Current.TryMatch(1, $"^(?:{Indent} |{Indent}\\s*{FilePattern}(?:{VisualCppLocationPattern})?\\s*: note:| *$)", out Match))
			{
				Builder.MoveNext();

				Group Group = Match.Groups["file"];
				if (Group.Success)
				{
					Builder.AnnotateSourceFile(Group, Context, DefaultSourceFileBaseDir);
					Builder.TryAnnotate(Match.Groups["line"], LogEventMarkup.LineNumber);
					Builder.AddProperty("note", true);
				}
			}

			string Pattern = $"^{Indent} |: note:";
			while (Builder.Current.IsMatch(1, Pattern))
			{
				Builder.MoveNext();
			}

			OutEvent = Builder.ToMatch(LogEventPriority.High, Level, KnownLogEvents.Compiler);
			return true;
		}

		static string? GetPlatformAgnosticDirectoryName(string FileName)
		{
			int Index = FileName.LastIndexOfAny(new[] { '/', '\\' });
			if (Index == -1)
			{
				return null;
			}
			else
			{
				return FileName.Substring(0, Index);
			}
		}

		bool IsSourceFile(Match Match)
		{
			Group Group = Match.Groups["file"];
			if (!Group.Success)
			{
				return false;
			}

			string Text = Group.Value;
			if (InvalidExtensions.Any(x => Text.EndsWith(x, StringComparison.OrdinalIgnoreCase)))
			{
				return false;
			}

			return true;
		}

		static LogLevel GetLogLevelFromSeverity(Match Match)
		{
			string Severity = Match.Groups["severity"].Value;
			if(Severity.Equals("warning", StringComparison.Ordinal))
			{
				return LogLevel.Warning;
			}
			else
			{
				return LogLevel.Error;
			}
		}

		static string ExtractIndent(string Line)
		{
			int Length = 0;
			while(Length < Line.Length && Line[Length] == ' ')
			{
				Length++;
			}
			return new string(' ', Length);
		}
	}
}
