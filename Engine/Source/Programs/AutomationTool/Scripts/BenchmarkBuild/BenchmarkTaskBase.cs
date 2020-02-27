// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace AutomationTool.Benchmark
{
	/// <summary>
	/// Base class for running tasks
	/// </summary>
	abstract class BenchmarkTaskBase
	{
		/// <summary>
		/// True or false based on whether the task failed
		/// </summary>
		public bool Failed { get; protected set; }

		/// <summary>
		/// Failure message
		/// </summary>
		public string FailureString { get; protected set; }

		/// <summary>
		/// Don't report this test
		/// </summary>
		public bool SkipReport { get; protected set; }

		/// <summary>
		/// Time the task took (does not include prequisites)
		/// </summary>
		public TimeSpan TaskTime { get; protected set; }

		/// <summary>
		/// Time the task started
		/// </summary>
		public DateTime StartTime { get; protected set; }

		/// <summary>
		/// Perform any prerequisites the task requires
		/// </summary>
		virtual protected bool PerformPrequisites() { return true;  }

		/// <summary>
		/// Perform the actual task that is measured
		/// </summary>
		protected abstract bool PerformTask();

		/// <summary>
		/// Return a name for this task for reporting
		/// </summary>
		/// <returns></returns>
		public abstract string GetTaskName();

		/// <summary>
		/// Run the task. Performs any prerequisites, then the actual task itself
		/// </summary>
		public void Run()
		{
			try
			{
				if (PerformPrequisites())
				{
					StartTime = DateTime.Now;

					if (PerformTask())
					{
						TaskTime = DateTime.Now - StartTime;
					}
					else
					{
						FailureString = "Task Failed";
						Failed = true;
					}
				}
				else
				{
					FailureString = "Prequisites Failed";
					Failed = true;
				}
				
			}
			catch (Exception Ex)
			{
				FailureString = string.Format("Exception: {0}", Ex.ToString());
				Failed = true;
			}		
			
			if (Failed)
			{
				Log.TraceError("{0} failed. {1}", GetTaskName(), FailureString);
			}
		}

		/// <summary>
		/// Report how long the task took
		/// </summary>
		public void Report()
		{
			if (!Failed)
			{
				Log.TraceInformation("Task {0}:\t\t\t\t{1}", GetTaskName(), TaskTime.ToString(@"hh\:mm\:ss"));
			}
			else
			{
				Log.TraceInformation("Task {0}::\t\t\t\tFailed. {1}", GetTaskName(), FailureString);
			}
		}
	}
}
