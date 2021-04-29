// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace HordeAgent.Parser.Interfaces
{
	/// <summary>
	/// Interface for a class which matches error strings
	/// </summary>
	interface ILogEventMatcher
	{
		/// <summary>
		/// Attempt to match an error from the given input buffer
		/// </summary>
		/// <param name="Cursor">The input buffer</param>
		/// <param name="Context">The input context</param>
		/// <returns>Information about the error that was matched, or null if an error was not matched</returns>
		LogEvent? Match(ILogCursor Cursor, ILogContext Context);
	}
}
