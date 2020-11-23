// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Linq;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	class ActionThread
	{
		/// <summary>
		/// Cache the exit code from the command so that the executor can report errors
		/// </summary>
		public int ExitCode = 0;

		/// <summary>
		/// Set to true only when the local or RPC action is complete
		/// </summary>
		public bool bComplete = false;

		/// <summary>
		/// Cache the action that this thread is managing
		/// </summary>
		QueuedAction Action;

		/// <summary>
		/// For reporting status to the user
		/// </summary>
		int JobNumber;
		int TotalJobs;

		/// <summary>
		/// Constructor, takes the action to process
		/// </summary>
		public ActionThread(QueuedAction InAction, int InJobNumber, int InTotalJobs)
		{
			Action = InAction;
			JobNumber = InJobNumber;
			TotalJobs = InTotalJobs;
		}

		/// <summary>
		/// Used when debuging Actions outputs all action return values to debug out
		/// </summary>
		/// <param name="sender"> Sending object</param>
		/// <param name="e">  Event arguments (In this case, the line of string output)</param>
		protected void ActionDebugOutput(object sender, DataReceivedEventArgs e)
		{
			string Output = e.Data;
			if (Output == null)
			{
				return;
			}

			Log.TraceInformation(Output);
		}


		/// <summary>
		/// The actual function to run in a thread. This is potentially long and blocking
		/// </summary>
		private void ThreadFunc()
		{
			string Args = Action.CommandArguments;
#if NET_CORE
			// Process Arguments follow windows conventions in .NET Core
			// Which means single quotes ' are not considered quotes.
			// see https://github.com/dotnet/runtime/issues/29857
			Args = Args.Replace('\'', '\"');
#endif
			// Create the action's process.
			ProcessStartInfo ActionStartInfo = new ProcessStartInfo();
			ActionStartInfo.WorkingDirectory = Action.WorkingDirectory.FullName;
			ActionStartInfo.FileName = Action.CommandPath.FullName;
			ActionStartInfo.Arguments = Args;
			ActionStartInfo.UseShellExecute = false;
			ActionStartInfo.RedirectStandardInput = false;
			ActionStartInfo.RedirectStandardOutput = false;
			ActionStartInfo.RedirectStandardError = false;

			// Log command-line used to execute task if debug info printing is enabled.
			Log.TraceVerbose("Executing: {0} {1}", ActionStartInfo.FileName, ActionStartInfo.Arguments);

			// Log summary if wanted.
			if (Action.bShouldOutputStatusDescription)
			{
				string CommandDescription = Action.CommandDescription != null ? Action.CommandDescription : Path.GetFileName(ActionStartInfo.FileName);
				if (string.IsNullOrEmpty(CommandDescription))
				{
					Log.TraceInformation(Action.StatusDescription);
				}
				else
				{
					Log.TraceInformation("[{0}/{1}] {2} {3}", JobNumber, TotalJobs, CommandDescription, Action.StatusDescription);
				}
			}

			// Try to launch the action's process, and produce a friendly error message if it fails.
			Process ActionProcess = null;
			try
			{
				try
				{
					ActionProcess = new Process();
					ActionProcess.StartInfo = ActionStartInfo;
					ActionStartInfo.RedirectStandardOutput = true;
					ActionStartInfo.RedirectStandardError = true;
					ActionProcess.OutputDataReceived += new DataReceivedEventHandler(ActionDebugOutput);
					ActionProcess.ErrorDataReceived += new DataReceivedEventHandler(ActionDebugOutput);
					ActionProcess.Start();

					ActionProcess.BeginOutputReadLine();
					ActionProcess.BeginErrorReadLine();
				}
				catch (Exception ex)
				{
					Log.TraceError("Failed to start local process for action: {0} {1}", Action.CommandPath, Action.CommandArguments);
					Log.WriteException(ex, null);
					ExitCode = 1;
					bComplete = true;
					return;
				}

				// wait for process to start
				// NOTE: this may or may not be necessary; seems to depend on whether the system UBT is running on start the process in a timely manner.
				int checkIterations = 0;
				bool haveConfiguredProcess = false;
				do
				{
					if (ActionProcess.HasExited)
					{
						if (haveConfiguredProcess == false)
							Debug.WriteLine("Process for action exited before able to configure!");
						break;
					}

					if (!haveConfiguredProcess)
					{
						try
						{
							ActionProcess.PriorityClass = ProcessPriorityClass.BelowNormal;
							haveConfiguredProcess = true;
						}
						catch (Exception)
						{
						}
						break;
					}

					Thread.Sleep(10);

					checkIterations++;
				} while (checkIterations < 100);
				if (checkIterations == 100)
				{
					throw new BuildException("Failed to configure local process for action: {0} {1}", Action.CommandPath, Action.CommandArguments);
				}

				// block until it's complete
				// @todo iosmerge: UBT had started looking at:	if (Utils.IsValidProcess(Process))
				//    do we need to check that in the thread model?
				ActionProcess.WaitForExit();

				// capture exit code
				ExitCode = ActionProcess.ExitCode;
			}
			finally
			{
				// As the process has finished now, free its resources. On non-Windows platforms, processes depend 
				// on POSIX/BSD threading and these are limited per application. Disposing the Process releases 
				// these thread resources.
				if (ActionProcess != null)
					ActionProcess.Close();
			}

			// we are done!!
			bComplete = true;
		}

		/// <summary>
		/// Starts a thread and runs the action in that thread
		/// </summary>
		public void Run()
		{
			Thread T = new Thread(ThreadFunc);
			T.Start();
		}
	};

	class LocalExecutor : ActionExecutor
	{
		/// <summary>
		/// Processor count multiplier for local execution. Can be below 1 to reserve CPU for other tasks.
		/// When using the local executor (not XGE), run a single action on each CPU core. Note that you can set this to a larger value
		/// to get slightly faster build times in many cases, but your computer's responsiveness during compiling may be much worse.
		/// </summary>
		[XmlConfigFile]
		double ProcessorCountMultiplier = 1.0;

		/// <summary>
		/// Maximum processor count for local execution. 
		/// </summary>
		[XmlConfigFile]
		int MaxProcessorCount = int.MaxValue;

		/// <summary>
		/// How many processes that will be executed in parallel
		/// </summary>
		int NumParallelProcesses;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="MaxLocalActions">How many actions to execute in parallel. When 0 a default will be chosen based on system resources</param>
		public LocalExecutor(int MaxLocalActions=0)
		{
			XmlConfig.ApplyTo(this);

			// if specified this caps how many processors we can use
			if (MaxLocalActions > 0)
			{
				NumParallelProcesses = MaxLocalActions;
			}
			else
			{
				// Figure out how many processors to use
				NumParallelProcesses = GetMaxActionsToExecuteInParallel();
			}
		}

		public override string Name
		{
			get { return "Local"; }
		}

		/// <summary>
		/// Determines the maximum number of actions to execute in parallel, taking into account the resources available on this machine.
		/// </summary>
		/// <returns>Max number of actions to execute in parallel</returns>
		public virtual int GetMaxActionsToExecuteInParallel()
		{
			// Get the number of logical processors
			int NumLogicalCores = Utils.GetLogicalProcessorCount();

			// Use WMI to figure out physical cores, excluding hyper threading.
			int NumPhysicalCores = Utils.GetPhysicalProcessorCount();
			if (NumPhysicalCores == -1)
			{
				NumPhysicalCores = NumLogicalCores;
			}

			// The number of actions to execute in parallel is trying to keep the CPU busy enough in presence of I/O stalls.
			int MaxActionsToExecuteInParallel = 0;
			if (NumPhysicalCores < NumLogicalCores && ProcessorCountMultiplier != 1.0)
			{
				// The CPU has more logical cores than physical ones, aka uses hyper-threading. 
				// Use multiplier if provided
				MaxActionsToExecuteInParallel = (int)(NumPhysicalCores * ProcessorCountMultiplier);
			}
			else if (NumPhysicalCores < NumLogicalCores && NumPhysicalCores > 4)
			{
				// The CPU has more logical cores than physical ones, aka uses hyper-threading. 
				// Use average of logical and physical if we have "lots of cores"
				MaxActionsToExecuteInParallel = Math.Max((int)(NumPhysicalCores + NumLogicalCores) / 2, NumLogicalCores - 4);
			}
			// No hyper-threading. Only kicking off a task per CPU to keep machine responsive.
			else
			{
				MaxActionsToExecuteInParallel = NumPhysicalCores;
			}

#if !NET_CORE
			if (Utils.IsRunningOnMono)
			{
				long PhysicalRAMAvailableMB = (new PerformanceCounter("Mono Memory", "Total Physical Memory").RawValue) / (1024 * 1024);
				// heuristic: give each action at least 1.5GB of RAM (some clang instances will need more) if the total RAM is low, or 1GB on 16+GB machines
				long MinMemoryPerActionMB = (PhysicalRAMAvailableMB < 16384) ? 3 * 1024 / 2 : 1024;
				int MaxActionsAffordedByMemory = (int)(Math.Max(1, (PhysicalRAMAvailableMB) / MinMemoryPerActionMB));

				MaxActionsToExecuteInParallel = Math.Min(MaxActionsToExecuteInParallel, MaxActionsAffordedByMemory);
			}
#endif

			MaxActionsToExecuteInParallel = Math.Max(1, Math.Min(MaxActionsToExecuteInParallel, MaxProcessorCount));
			return MaxActionsToExecuteInParallel;
		}

		/// <summary>
		/// Executes the specified actions locally.
		/// </summary>
		/// <returns>True if all the tasks successfully executed, or false if any of them failed.</returns>
		public override bool ExecuteActions(List<QueuedAction> Actions)
		{
			// Time to sleep after each iteration of the loop in order to not busy wait.
			const float LoopSleepTime = 0.1f;

			// The number of actions to execute in parallel is trying to keep the CPU busy enough in presence of I/O stalls.
			Log.TraceInformation("Performing {0} actions ({1} in parallel)", Actions.Count, NumParallelProcesses);

			Dictionary<QueuedAction, ActionThread> ActionThreadDictionary = new Dictionary<QueuedAction, ActionThread>();
			int JobNumber = 1;
			using (ProgressWriter ProgressWriter = new ProgressWriter("Compiling C++ source code...", false))
			{
				int ProgressValue = 0;
				while (true)
				{
					// Count the number of pending and still executing actions.
					int NumUnexecutedActions = 0;
					int NumExecutingActions = 0;
					foreach (QueuedAction Action in Actions)
					{
						ActionThread ActionThread = null;
						bool bFoundActionProcess = ActionThreadDictionary.TryGetValue(Action, out ActionThread);
						if (bFoundActionProcess == false)
						{
							NumUnexecutedActions++;
						}
						else if (ActionThread != null)
						{
							if (ActionThread.bComplete == false)
							{
								NumUnexecutedActions++;
								NumExecutingActions++;
							}
						}
					}

					// Update the current progress
					int NewProgressValue = Actions.Count + 1 - NumUnexecutedActions;
					if (ProgressValue != NewProgressValue)
					{
						ProgressWriter.Write(ProgressValue, Actions.Count + 1);
						ProgressValue = NewProgressValue;
					}

					// If there aren't any pending actions left, we're done executing.
					if (NumUnexecutedActions == 0)
					{
						break;
					}

					// If there are fewer actions executing than the maximum, look for pending actions that don't have any outdated
					// prerequisites.
					foreach (QueuedAction Action in Actions)
					{
						ActionThread ActionProcess = null;
						bool bFoundActionProcess = ActionThreadDictionary.TryGetValue(Action, out ActionProcess);
						if (bFoundActionProcess == false)
						{
							if (NumExecutingActions < Math.Max(1, NumParallelProcesses))
							{
								// Determine whether there are any prerequisites of the action that are outdated.
								bool bHasOutdatedPrerequisites = false;
								bool bHasFailedPrerequisites = false;
								foreach (QueuedAction PrerequisiteAction in Action.PrerequisiteActions)
								{
									if (Actions.Contains(PrerequisiteAction))
									{
										ActionThread PrerequisiteProcess = null;
										bool bFoundPrerequisiteProcess = ActionThreadDictionary.TryGetValue(PrerequisiteAction, out PrerequisiteProcess);
										if (bFoundPrerequisiteProcess == true)
										{
											if (PrerequisiteProcess == null)
											{
												bHasFailedPrerequisites = true;
											}
											else if (PrerequisiteProcess.bComplete == false)
											{
												bHasOutdatedPrerequisites = true;
											}
											else if (PrerequisiteProcess.ExitCode != 0)
											{
												bHasFailedPrerequisites = true;
											}
										}
										else
										{
											bHasOutdatedPrerequisites = true;
										}
									}
								}

								// If there are any failed prerequisites of this action, don't execute it.
								if (bHasFailedPrerequisites)
								{
									// Add a null entry in the dictionary for this action.
									ActionThreadDictionary.Add(Action, null);
								}
								// If there aren't any outdated prerequisites of this action, execute it.
								else if (!bHasOutdatedPrerequisites)
								{
									ActionThread ActionThread = new ActionThread(Action, JobNumber, Actions.Count);
									JobNumber++;

									try
									{
										ActionThread.Run();
									}
									catch (Exception ex)
									{
										throw new BuildException(ex, "Failed to start thread for action: {0} {1}\r\n{2}", Action.CommandPath, Action.CommandArguments, ex.ToString());
									}

									ActionThreadDictionary.Add(Action, ActionThread);

									NumExecutingActions++;
								}
							}
						}
					}

					System.Threading.Thread.Sleep(TimeSpan.FromSeconds(LoopSleepTime));
				}
			}

			// Check whether any of the tasks failed and log action stats if wanted.
			bool bSuccess = true;
			foreach (KeyValuePair<QueuedAction, ActionThread> ActionProcess in ActionThreadDictionary)
			{
				QueuedAction Action = ActionProcess.Key;
				ActionThread ActionThread = ActionProcess.Value;

				// Check for pending actions, preemptive failure
				if (ActionThread == null)
				{
					bSuccess = false;
					continue;
				}
				// Check for executed action but general failure
				if (ActionThread.ExitCode != 0)
				{
					bSuccess = false;
				}
			}

			return bSuccess;
		}
	};
}
