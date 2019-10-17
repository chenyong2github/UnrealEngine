// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace BuildAgent.Run
{
	class LineFilter
	{
		Func<string> InnerReadLine;
		int OutputEnabled;

		public LineFilter(Func<string> ReadLine)
		{
			this.InnerReadLine = ReadLine;
		}

		public string ReadLine()
		{
			string Line = InnerReadLine();
			if (Line != null)
			{
				// remove timestamps added by UAT
				Line = Regex.Replace(Line, @"^\[[0-9:.]+\] ", "");

				// remove the mono prefix for recursive UAT calls on Mac
				Line = Regex.Replace(Line, @"^mono: ([A-Za-z0-9-]+: )", "$1");

				// remove the AutomationTool prefix for recursive UAT calls anywhere
				Line = Regex.Replace(Line, @"^AutomationTool: ([A-Za-z0-9-]+: )", "$1");

				// if it was the editor, also strip any additional timestamp
				Line = Regex.Replace(Line, @"^\s*\[[0-9.:]+-[0-9.:]+\]\[\s*\d+\]", "");

				// look for a special marker to disable output
				if (Regex.IsMatch(Line, "<-- Suspend Log Parsing -->", RegexOptions.IgnoreCase))
				{
					OutputEnabled--;
				}
				else if (Regex.IsMatch(Line, "<-- Resume Log Parsing -->", RegexOptions.IgnoreCase))
				{
					OutputEnabled++;
				}
				else if (OutputEnabled < 0)
				{
					Line = "";
				}
			}
			return Line;
		}
	}
}
