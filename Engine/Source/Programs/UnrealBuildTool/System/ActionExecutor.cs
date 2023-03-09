// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Reflection;

namespace UnrealBuildTool
{
	abstract class ActionExecutor : IDisposable
	{
		public abstract string Name
		{
			get;
		}

		static protected double MemoryPerActionBytesOverride
		{
			get;
			private set;
		} = 0.0;

		readonly LogEventParser Parser;

		public ActionExecutor(ILogger Logger)
		{
			Parser = new LogEventParser(Logger);
			Parser.AddMatchersFromAssembly(Assembly.GetExecutingAssembly());
		}

		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		protected virtual void Dispose(bool disposing)
		{
			if (disposing)
			{
				Parser.Dispose();
			}
		}

		/// <summary>
		/// Allow targets to override the expected amount of memory required for compiles, used to control the number
		/// of parallel action processes.
		/// </summary>
		/// <param name="MemoryPerActionOverrideGB"></param>
		public static void SetMemoryPerActionOverride(double MemoryPerActionOverrideGB)
		{
			MemoryPerActionBytesOverride = Math.Max(MemoryPerActionBytesOverride, MemoryPerActionOverrideGB * 1024 * 1024 * 1024);
		}

		public abstract bool ExecuteActions(IEnumerable<LinkedAction> ActionsToExecute, ILogger Logger);

		protected void WriteToolOutput(string Line)
		{
			lock (Parser)
			{
				Parser.WriteLine(Line);
			}
		}
	}

}
