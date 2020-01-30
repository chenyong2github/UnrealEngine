// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Run;
using BuildAgent.Run.Interfaces;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace BuildAgent
{
	[AutoRegister]
	class GenericErrorMatcher : IErrorMatcher
	{
		public ErrorMatch Match(ReadOnlyLineBuffer Input)
		{
			if (Input.IsMatch(@"^\s*FATAL:"))
			{
				return new ErrorMatch(ErrorSeverity.Error, ErrorPriority.Low, "Generic", Input);
			}
			if (Input.IsMatch(@"(?<!\w)(ERROR|[Ee]rror)( (\([^)]+\)|\[[^\]]+\]))?: "))
			{
				int MaxOffset = Input.MatchForwards(0, String.Format(@"^({0} | *$)", ExtractIndent(Input[0])));
				return new ErrorMatch(ErrorSeverity.Error, ErrorPriority.Lowest, "Generic", Input, 0, MaxOffset);
			}
			if (Input.IsMatch(@"(?<!\w)(WARNING|[Ww]arning)( (\([^)]+\)|\[[^\]]+\]))?: "))
			{
				int MaxOffset = Input.MatchForwards(0, String.Format(@"^({0} | *$)", ExtractIndent(Input[0])));
				return new ErrorMatch(ErrorSeverity.Warning, ErrorPriority.Lowest, "Generic", Input, 0, MaxOffset);
			}
			if (Input.IsMatch(@"[Ee]rror [A-Z]\d+\s:"))
			{
				return new ErrorMatch(ErrorSeverity.Error, ErrorPriority.Lowest, "Generic", Input);
			}
			return null;
		}

		static string ExtractIndent(string Line)
		{
			int Length = 0;
			while (Length < Line.Length && Line[Length] == ' ')
			{
				Length++;
			}
			return new string(' ', Length);
		}
	}
}
