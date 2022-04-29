// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace UnrealBuildTool
{
	/// <summary>
	/// Scoped timer, start is in the constructor, end in Dispose. Best used with using(ScopedTimer Timer = new ScopedTimer()). Suports nesting.
	/// </summary>
	public class ScopedTimer : IDisposable
	{
		DateTime StartTime;
		string TimerName;
		LogEventType Verbosity;
		bool bIncreaseIndent;
		static int Indent = 0;
		static object IndentLock = new object();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of the block being measured</param>
		/// <param name="InVerbosity">Verbosity for output messages</param>
		/// <param name="bIncreaseIndent">Whether gobal indent should be increased or not; set to false when running a scope in parallel. Message will still be printed indented relative to parent scope.</param>
		public ScopedTimer(string Name, LogEventType InVerbosity = LogEventType.Verbose, bool bIncreaseIndent = true)
		{
			TimerName = Name;
			if (bIncreaseIndent)
			{
				lock (IndentLock)
				{
					Indent++;
				}
			}
			Verbosity = InVerbosity;
			StartTime = DateTime.UtcNow;
			this.bIncreaseIndent = bIncreaseIndent;
		}

		/// <summary>
		/// Prints out the timing message
		/// </summary>
		public void Dispose()
		{
			double TotalSeconds = (DateTime.UtcNow - StartTime).TotalSeconds;
			int LogIndent = Indent;
			if (bIncreaseIndent)
			{
				lock (IndentLock)
				{
					LogIndent = --Indent;
				}
			}
			StringBuilder IndentText = new StringBuilder(LogIndent * 2);
			IndentText.Append(' ', LogIndent * 2);

			Log.WriteLine(Verbosity, "{0}{1} took {2}s", IndentText.ToString(), TimerName, TotalSeconds);
		}
	}
}
