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
	class ExceptionErrorMatcher : IErrorMatcher
	{
		public ErrorMatch Match(ReadOnlyLineBuffer Input)
		{
			if (Input.IsMatch(@"^\s*Unhandled Exception: "))
			{
				int MaxOffset = Input.MatchForwards(0, @"^\s*at ");
				return new ErrorMatch(ErrorSeverity.Error, ErrorPriority.Low, "Exception", Input, 0, MaxOffset);
			}
			return null;
		}
	}
}
