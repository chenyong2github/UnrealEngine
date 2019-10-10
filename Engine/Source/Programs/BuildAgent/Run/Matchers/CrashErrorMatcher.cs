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
	class CrashErrorMatcher : IErrorMatcher
	{
		public ErrorMatch Match(ReadOnlyLineBuffer Input)
		{
			if(Regex.IsMatch(Input[0], "begin: stack for UAT"))
			{
				for (int Idx = 1; Idx < 100; Idx++)
				{
					if(Regex.IsMatch(Input[Idx], "end: stack for UAT"))
					{
						return new ErrorMatch(ErrorSeverity.Error, ErrorPriority.BelowNormal, "crash", Input, 0, Idx);
					}
				}
			}
			if(Input.IsMatch("AutomationTool: Stack:"))
			{
				int EndOffset = Input.MatchForwards(0, "AutomationTool: Stack:");
				return new ErrorMatch(ErrorSeverity.Error, ErrorPriority.Low, "Crash", Input, 0, EndOffset);
			}
			if (Input.IsMatch("ExitCode=(3|139|255)"))
			{
				return new ErrorMatch(ErrorSeverity.Error, ErrorPriority.Low, "Crash", Input);
			}
			return null;
		}
	}
}
