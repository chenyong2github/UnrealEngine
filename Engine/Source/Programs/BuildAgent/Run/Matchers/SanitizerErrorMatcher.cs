// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Run;
using BuildAgent.Run.Interfaces;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace BuildAgent.Matchers
{
	[AutoRegister]
	class SanitizerErrorMatcher : IErrorMatcher
	{
		public ErrorMatch Match(ReadOnlyLineBuffer Input)
		{
			Match Match;
			if (Input.TryMatch(@"^(\s*)WARNING: ThreadSanitizer:", out Match))
			{
				int EndIdx = Input.MatchForwards(0, String.Format(@"^([ ]*|{0}  .*|{0}SUMMARY:.*)\$", Match.Groups[1].Value));
				return new ErrorMatch(ErrorSeverity.Warning, ErrorPriority.Normal, "ThreadSanitizer", Input, 0, EndIdx);
			}
			return null;
		}
	}
}
