// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Run;
using BuildAgent.Run.Interfaces;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Run.Listeners
{
	/// <summary>
	/// Debug error listener. Just prints the error output to the log.
	/// </summary>
	class DebugOutputListener : IErrorListener
	{
		/// <summary>
		/// Dispose of this object
		/// </summary>
		public void Dispose()
		{
		}

		/// <summary>
		/// Called when an error is matched
		/// </summary>
		/// <param name="Error">The matched error</param>
		public void OnErrorMatch(ErrorMatch Error)
		{
			Console.WriteLine("Error of type '{0}' at line {1}:", Error.Type, Error.MinLineNumber);
			foreach(string Line in Error.Lines)
			{
				Console.WriteLine("Text: |{0}|", Line);
			}
			if (Error.Properties.Count > 0)
			{
				Console.WriteLine("Properties:");
				foreach (KeyValuePair<string, string> Pair in Error.Properties)
				{
					Console.WriteLine("  {0} = {1}", Pair.Key, Pair.Value);
				}
			}
			Console.WriteLine();
		}
	}
}
