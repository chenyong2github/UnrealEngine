// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Run;
using BuildAgent.Run.Interfaces;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Matchers
{
	[AutoRegister]
	class LinkErrorMatcher : IErrorMatcher
	{
		public ErrorMatch Match(ReadOnlyLineBuffer Input)
		{
			if(Input.IsMatch(@"error: linker command failed with exit code "))
			{
				int MinIdx = Input.MatchBackwards(0, ": In function |: undefined reference to ");
				return new ErrorMatch(ErrorSeverity.Error, ErrorPriority.Normal, "Link", Input, MinIdx, 0);
			}
			if (Input.IsMatch("Undefined symbols for architecture"))
			{
				int MaxOffset = Input.MatchForwardsUntil(0, "ld: symbol");
				return new ErrorMatch(ErrorSeverity.Error, ErrorPriority.Normal, "Link", Input, 0, MaxOffset);
			}
			if (Input.IsMatch(@"LINK : fatal error"))
			{
				return new ErrorMatch(ErrorSeverity.Error, ErrorPriority.Normal, "Link", Input);
			}
			return null;
		}
	}
}
