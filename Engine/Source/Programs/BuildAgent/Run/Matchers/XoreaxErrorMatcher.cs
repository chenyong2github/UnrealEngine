// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	class XoreaxErrorMatcher : IErrorMatcher
	{
		public ErrorMatch Match(ReadOnlyLineBuffer Input)
		{
			if(Input.IsMatch(@"\s*----+Build System Warning----+"))
			{
				int MaxOffset = Input.MatchForwards(0, @"^\s*[A-Za-z]");
				return new ErrorMatch(ErrorSeverity.Warning, ErrorPriority.Normal, "Xoreax", Input, 0, MaxOffset);
			}
			return null;
		}
	}
}
