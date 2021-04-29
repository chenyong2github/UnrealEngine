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

		/// <inheritdoc/>
		public LogEvent? Match(ILogCursor Input, ILogContext Context)
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
				// Try to match a Visual C++ diagnostic
				LogEvent? NewEvent;
				if(TryMatchVisualCppEvent(Input, MaxOffset, Context, out NewEvent))
				{
					LogEventSpan? Code;
					if(NewEvent.TryGetSpan("code", out Code) && Code.Text == "C2220")
					{
						MaxOffset = NewEvent.MaxLineNumber + 1 - Input.CurrentLineNumber;

						List<LogEvent> ChildEvents = new List<LogEvent>();

						LogEvent? ChildEvent;
						while (TryMatchVisualCppEvent(Input.Rebase(MaxOffset), 0, Context, out ChildEvent))
						{
							ChildEvent.Level = LogLevel.Error;
							ChildEvents.Add(ChildEvent);
							MaxOffset = ChildEvent.MaxLineNumber + 1 - Input.CurrentLineNumber;
						}

						NewEvent.MaxLineNumber = Input.CurrentLineNumber + (MaxOffset - 1);
						NewEvent.ChildEvents = ChildEvents;
					}

					return NewEvent;
				}

				// Try to match a Clang diagnostic
				Match? Match;
				if (Input.TryMatch($"^\\s*{FilePattern}\\s*{ClangLocationPattern}:\\s*{ClangSeverity}\\s*:", out Match) && IsSourceFile(Match))
				{
					LogLevel Level = GetLogLevelFromSeverity(Match);

					LogEventBuilder Builder = new LogEventBuilder(Input);

					LogEventLine FirstLine = Builder.Lines[0];
					FirstLine.AddSpan(Match.Groups["file"]).MarkAsSourceFile(Context, "Engine/Source");
					FirstLine.AddSpan(Match.Groups["severity"]).MarkAsSeverity();
					FirstLine.TryAddSpan(Match.Groups["line"])?.MarkAsLineNumber();
					FirstLine.TryAddSpan(Match.Groups["column"])?.MarkAsLineNumber();

					string Indent = ExtractIndent(Input[0]!);

					int NoteIdx = 1;
					while (Builder.TryMatch($"^(?:{Indent} |{Indent}\\s*{FilePattern}\\s*{ClangLocationPattern}\\s*note:| *$)", out Match))
					{
						LogEventLine Line = Builder.AddLine();

						Group FileGroup = Match.Groups["file"];
						if (FileGroup.Success)
						{
							Line.AddSpan(FileGroup, $"note{NoteIdx}").MarkAsSourceFile(Context, DefaultSourceFileBaseDir);
							Line.TryAddSpan(Match.Groups["line"], $"line{NoteIdx}")?.MarkAsLineNumber();
							NoteIdx++;
						}
					}

					return Builder.ToLogEvent(LogEventPriority.High, Level, KnownLogEvents.Compiler);
				}
			}
			return null;
		}

		bool TryMatchVisualCppEvent(ILogCursor Input, int MaxOffset, ILogContext Context, [NotNullWhen(true)] out LogEvent? OutEvent)
		{
			Match? Match;
			if (!Input.TryMatch(MaxOffset, $"^\\s*(?:ERROR: |WARNING: )?{FilePattern}(?:{VisualCppLocationPattern})? ?:\\s+{VisualCppSeverity}:", out Match) || !IsSourceFile(Match))
			{
				OutEvent = null;
				return false;
			}

			LogLevel Level = GetLogLevelFromSeverity(Match);

			LogEventBuilder Builder = new LogEventBuilder(Input);
			Builder.MaxOffset = MaxOffset;

			LogEventLine FirstLine = Builder.Lines[Builder.MaxOffset];

			FirstLine.AddSpan(Match.Groups["severity"]).MarkAsSeverity();

			string SourceFileBaseDir = DefaultSourceFileBaseDir;

			Group CodeGroup = Match.Groups["code"];
			if (CodeGroup.Success)
			{
				FirstLine.AddSpan(CodeGroup).MarkAsErrorCode();

				if (CodeGroup.Value.StartsWith("CS", StringComparison.Ordinal))
				{
					Match? ProjectMatch;
					if (Input.TryMatch(@"\[(?<project>[^[\]]+)]\s*$", out ProjectMatch))
					{
						FirstLine.AddSpan(ProjectMatch.Groups[1]).MarkAsSourceFile(Context, "");
						SourceFileBaseDir = GetPlatformAgnosticDirectoryName(ProjectMatch.Groups[1].Value) ?? SourceFileBaseDir;
					}
				}
				else if (CodeGroup.Value.StartsWith("MSB", StringComparison.Ordinal))
				{
					if (CodeGroup.Value.Equals("MSB3026", StringComparison.Ordinal))
					{
						OutEvent = Builder.ToLogEvent(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_MSBuild);
						return true;
					}

					Match? ProjectMatch;
					if (Input.TryMatch(@"\[(?<file>[^[\]]+)]\s*$", out ProjectMatch))
					{
						FirstLine.AddSpan(ProjectMatch.Groups[1]).MarkAsSourceFile(Context, "");
						OutEvent = Builder.ToLogEvent(LogEventPriority.High, Level, KnownLogEvents.MSBuild);
						return true;
					}
				}
			}

			FirstLine.AddSpan(Match.Groups["file"]).MarkAsSourceFile(Context, SourceFileBaseDir);
			FirstLine.TryAddSpan(Match.Groups["line"])?.MarkAsLineNumber();
			FirstLine.TryAddSpan(Match.Groups["column"])?.MarkAsColumnNumber();

			string Indent = ExtractIndent(Input[0] ?? String.Empty);

			int NoteIdx = 1;
			while (Builder.TryMatch($"^(?:{Indent} |{Indent}\\s*{FilePattern}(?:{VisualCppLocationPattern})?\\s*: note:| *$)", out Match))
			{
				LogEventLine Line = Builder.AddLine();

				Group Group = Match.Groups["file"];
				if (Group.Success)
				{
					Line.AddSpan(Group, $"note{NoteIdx}").MarkAsSourceFile(Context, DefaultSourceFileBaseDir);
					Line.TryAddSpan(Match.Groups["line"], $"line{NoteIdx}")?.MarkAsLineNumber();
					NoteIdx++;
				}
			}

			string Pattern = $"^{Indent} |: note:";
			while (Input.IsMatch(Builder.MaxOffset + 1, Pattern))
			{
				Builder.AddLine();
			}

			OutEvent = Builder.ToLogEvent(LogEventPriority.High, Level, KnownLogEvents.Compiler);
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
