// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Run.Interfaces
{
	/// <summary>
	/// Interface for a class which matches error strings
	/// </summary>
	interface IErrorMatcher
	{
		/// <summary>
		/// Attempt to match an error from the given input buffer
		/// </summary>
		/// <param name="Input">The input buffer</param>
		/// <returns>Information about the error that was matched, or null if an error was not matched</returns>
		ErrorMatch Match(ReadOnlyLineBuffer Input);
	}
}
