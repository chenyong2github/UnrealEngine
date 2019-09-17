// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace IncludeTool.Support
{
	/// <summary>
	/// Implements similar functionality to TextWriter, but outputs an entire line at a time. Used as a base class for logging.
	/// </summary>
	abstract class LineBasedTextWriter
	{
		public abstract void WriteLine(string Text);

		public void WriteLine()
		{
			WriteLine("");
		}

		public void WriteLine(string Format, params object[] Args)
		{
			WriteLine(String.Format(Format, Args));
		}
	}
}
