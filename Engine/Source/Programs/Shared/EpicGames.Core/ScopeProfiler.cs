// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Reflection;

namespace EpicGames.Core
{
#if !__SCOPEPROFILER_AVAILABLE__
	
	/// <summary>
	/// A stub/no-op scope-profiler API that can be replaced by another implementation to record execution of
	/// instrumented scopes.
	/// </summary>
	public class ScopeProfiler 
	{
		public static ScopeProfiler Instance = new ScopeProfiler();
		
		public void InitializeAndStart(string ProgramName, string HostAddress, int MaxThreadCount) { }

		public void StopAndShutdown() { }

		/// <summary>
		/// Start a stacked scope. Must start and end on the same thread
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="bIsStall"></param>
		public void StartScope(string Name, bool bIsStall) { }
		
		/// <summary>
		/// End a stacked scope. Must start and end on the same thread.
		/// </summary>
		public void EndScope() { }

		/// <summary>
		/// Record an un-stacked time span at a specified time.
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="Id"></param>
		/// <param name="bStall"></param>
		/// <param name="BeginThreadId"></param>
		/// <param name="EndThreadId"></param>
		/// <param name="StartTime">start time for the span (use FastTime())</param>
		/// <param name="EndTime">end time for the span (use FastTime())</param>
		public void AddSpanAtTime(string Name, ulong Id, bool bStall, uint BeginThreadId, uint EndThreadId, ulong StartTime, ulong EndTime) { }

		/// <summary>
		/// Record a value against a particular name, to be plotted over time
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="Value"></param>
		public void Plot(string Name, double Value) { }

		/// <summary>
		/// Generate a timestamp value for the current moment.
		/// </summary>
		/// <returns></returns>
		public ulong FastTime()
		{
			return 0;
		}

		/// <summary>
		/// Retrieve the profiler's identifier for the current thread
		/// </summary>
		/// <returns></returns>
		public uint GetCurrentThreadId()
		{
			return 0;
		}
	}
#endif
}

