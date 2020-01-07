// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Run;
using BuildAgent.Run.Interfaces;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent.Matchers
{
	[AutoRegister]
	class CompileErrorMatcher : IErrorMatcher
	{
		const string ClangFileLinePattern =
			@"("                        +
			    @"(?:[a-zA-Z]:)?"		+	// optional drive letter
			    @"[^:]+"				+	// any non-colon character
			@")"						+
			@"(?:"						+
				@"\s*:[\s\d:,]+:"		+	// clang-style   :123:456:
				@"|"					+
				@"\([\s\d:,]+\):"		+ 	// msvc-style    (123,456):
			@")"						;

		public ErrorMatch Match(ReadOnlyLineBuffer Input)
		{
			// Do the match in two phases so we can early out if the strings "error" or "warning" are not present. The patterns before these strings can
			// produce many false positives, making them very slow to execute.
			if (Input.IsMatch("error"))
			{
				if (Input.IsMatch(@"([^(]+)(\([\d,]+\))? ?: (fatal )?error [a-zA-Z]+[\d]+"))
				{
					return ParseVisualCppMatch(Input, ErrorSeverity.Error);
				}
				if (Input.IsMatch(@"\s*" + ClangFileLinePattern + @"\s*error\s*:"))
				{
					return ParseClangMatch(Input, ErrorSeverity.Error);
				}
			}
			if (Input.IsMatch("warning"))
			{
				if (Input.IsMatch(@"([^(]+)(\([\d,]+\))? ?: warning[ :]"))
				{
					return ParseVisualCppMatch(Input, ErrorSeverity.Warning);
				}
				if (Input.IsMatch(@"\s*" + ClangFileLinePattern + @"\s*warning\s*:"))
				{
					return ParseClangMatch(Input, ErrorSeverity.Warning);
				}
			}
			return null;
		}

		static ErrorMatch ParseVisualCppMatch(ReadOnlyLineBuffer Input, ErrorSeverity Severity)
		{
			string Pattern = String.Format("^{0} |: note:", ExtractIndent(Input[0]));

			int EndIdx = 0;
			while (Input.IsMatch(EndIdx + 1, Pattern))
			{
				EndIdx++;
			}

			Log.TraceWarning("WARNING DETECTED! {0}", Input);
			return new ErrorMatch(Severity, ErrorPriority.High, "Compile", Input, 0, EndIdx);
		}

		static ErrorMatch ParseClangMatch(ReadOnlyLineBuffer Input, ErrorSeverity Severity)
		{
			string Indent = ExtractIndent(Input[0]);

			int MinOffset = Input.MatchBackwards(0, String.Format(@"^(?:{0}).*(?:In (member )?function|In file included from)", Indent));
			int MaxOffset = Input.MatchForwards(0, String.Format(@"^({0} |{0}{1}\s*note:| *\$)", Indent, ClangFileLinePattern));

			return new ErrorMatch(Severity, ErrorPriority.High, "Compile", Input, MinOffset, MaxOffset);
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
