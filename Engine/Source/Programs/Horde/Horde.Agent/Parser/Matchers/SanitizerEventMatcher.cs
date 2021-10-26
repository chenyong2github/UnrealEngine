// Copyright Epic Games, Inc. All Rights Reserved.

#if false
using HordeAgent.Parser;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace HordeAgent.Parser.Matchers
{
	class SanitizerEventMatcher : IEventMatcher
	{
		public EventMatch? Match(ReadOnlyLineBuffer Input)
		{
			Match? Match;
			if (Input.TryMatch(@"^(\s*)WARNING: ThreadSanitizer:", out Match))
			{
				int EndIdx = Input.MatchForwards(0, String.Format(@"^([ ]*|{0}  .*|{0}SUMMARY:.*)\$", Match!.Groups[1].Value));
				return new EventMatch(EventSeverity.Warning, EventMatchPriority.Normal, "ThreadSanitizer", Input, 0, EndIdx);
			}
			return null;
		}
	}
}
#endif
