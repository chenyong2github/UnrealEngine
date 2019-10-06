// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	class LogChannelErrorMatcher : IErrorMatcher
	{
		public ErrorMatch Match(ReadOnlyLineBuffer Input)
		{
			Match Match;
			if(Input.TryMatch(@"^(\s*)([a-zA-Z_][a-zA-Z0-9_]*):\s*(Error|Warning|Display): ", out Match))
			{
				int MaxOffset = Input.MatchForwards(0, String.Format(@"^({0} | *$)", Match.Groups[1].Value));

				ErrorSeverity Severity;
				switch(Match.Groups[3].Value)
				{
					case "Error":
						Severity = ErrorSeverity.Error;
						break;
					case "Warning":
						Severity = ErrorSeverity.Warning;
						break;
					default:
						Severity = ErrorSeverity.Silent;
						break;
				}

				ErrorMatch Error = new ErrorMatch(Severity, ErrorPriority.Low, "Log", Input, 0, MaxOffset);
				Error.Properties["Channel"] = Match.Groups[2].Value;
				return Error;
			}
			return null;
		}
	}
}
