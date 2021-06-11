// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;

namespace UnrealBuildTool
{
	/// <summary>
	/// This executor is similar to LocalExecutor, but uses p/invoke on Windows to ensure that child processes are started at a lower priority and are terminated when the parent process terminates.
	/// </summary>
	class ParallelExecutor : ActionExecutor
	{
		[DebuggerDisplay("{Inner}")]
		class BuildAction
		{
			public int SortIndex;
			public LinkedAction Inner;

			public HashSet<BuildAction> Dependencies = new HashSet<BuildAction>();
			public int MissingDependencyCount;

			public HashSet<BuildAction> Dependants = new HashSet<BuildAction>();
			public int TotalDependantCount;

			public List<string> LogLines = new List<string>();
			public int ExitCode = -1;

			public BuildAction(LinkedAction Inner)
			{
				this.Inner = Inner;
			}
		}

		/// <summary>
		/// Maximum processor count for local execution. 
		/// </summary>
		[XmlConfigFile]
		public static int MaxProcessorCount = int.MaxValue;

		/// <summary>
		/// Processor count multiplier for local execution. Can be below 1 to reserve CPU for other tasks.
		/// When using the local executor (not XGE), run a single action on each CPU core. Note that you can set this to a larger value
		/// to get slightly faster build times in many cases, but your computer's responsiveness during compiling may be much worse.
		/// This value is ignored if the CPU does not support hyper-threading.
		/// </summary>
		[XmlConfigFile]
		public static double ProcessorCountMultiplier = 1.0;

		/// <summary>
		/// Free memory per action in bytes, used to limit the number of parallel actions if the machine is memory starved.
		/// Set to 0 to disable free memory checking.
		/// </summary>
		[XmlConfigFile]
		static double MemoryPerActionBytes = 1.5 * 1024 * 1024 * 1024;

		/// <summary>
		/// When enabled, will stop compiling targets after a compile error occurs.
		/// </summary>
		[XmlConfigFile]
		bool bStopCompilationAfterErrors = false;

		/// <summary>
		/// How many processes that will be executed in parallel
		/// </summary>
		public int NumParallelProcesses { get; private set; }

		public static int GetDefaultNumParallelProcesses()
		{
			return Utils.GetMaxActionsToExecuteInParallel(MaxProcessorCount, ProcessorCountMultiplier, Convert.ToInt64(MemoryPerActionBytes));
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="MaxLocalActions">How many actions to execute in parallel</param>
		public ParallelExecutor(int MaxLocalActions)
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
				NumParallelProcesses = GetDefaultNumParallelProcesses();
			}
		}

		/// <summary>
		/// Returns the name of this executor
		/// </summary>
		public override string Name
		{
			get { return "Parallel"; }
		}

		/// <summary>
		/// Checks whether the parallel executor can be used
		/// </summary>
		/// <returns>True if the parallel executor can be used</returns>
		public static bool IsAvailable()
		{
			return true;
		}

		/// <summary>
		/// Executes the specified actions locally.
		/// </summary>
		/// <returns>True if all the tasks successfully executed, or false if any of them failed.</returns>
		public override bool ExecuteActions(List<LinkedAction> InputActions)
		{
			int ActualNumParallelProcesses = Math.Min(InputActions.Count, NumParallelProcesses);
			Log.TraceInformation("Building {0} {1} with {2} {3}...", InputActions.Count, (InputActions.Count == 1) ? "action" : "actions", ActualNumParallelProcesses, (ActualNumParallelProcesses == 1) ? "process" : "processes");

			// Create actions with all our internal metadata
			List<BuildAction> Actions = new List<BuildAction>();
			for (int Idx = 0; Idx < InputActions.Count; Idx++)
			{
				BuildAction Action = new BuildAction(InputActions[Idx]);
				Action.SortIndex = Idx;

				if (!Action.Inner.StatusDescription.EndsWith(".ispc"))
				{
					Action.SortIndex += 10000;
				}

				Actions.Add(Action);
			}

			// Update all the actions with all their dependencies
			Dictionary<LinkedAction, BuildAction> LinkedActionToBuildAction = Actions.ToDictionary(x => x.Inner, x => x);
			foreach (BuildAction Action in Actions)
			{
				foreach (LinkedAction PrerequisiteAction in Action.Inner.PrerequisiteActions)
				{
					BuildAction? Dependency;
					if (LinkedActionToBuildAction.TryGetValue(PrerequisiteAction, out Dependency))
					{
						Action.Dependencies.Add(Dependency);
						Dependency.Dependants.Add(Action);
					}
				}
			}

			// Figure out the recursive dependency count
			HashSet<BuildAction> VisitedActions = new HashSet<BuildAction>();
			foreach (BuildAction Action in Actions)
			{
				Action.MissingDependencyCount = Action.Dependencies.Count;
				RecursiveIncDependents(Action, VisitedActions);
			}

			// Create the list of things to process
			List<BuildAction> QueuedActions = new List<BuildAction>();
			foreach (BuildAction Action in Actions)
			{
				if (Action.MissingDependencyCount == 0)
				{
					QueuedActions.Add(Action);
				}
			}

			// Execute the actions
			using (LogIndentScope Indent = new LogIndentScope("  "))
			{
				// Create a job object for all the child processes
				bool bResult = true;
				Dictionary<BuildAction, Thread> ExecutingActions = new Dictionary<BuildAction, Thread>();
				List<BuildAction> CompletedActions = new List<BuildAction>();

				using (ManagedProcessGroup ProcessGroup = new ManagedProcessGroup())
				{
					using (AutoResetEvent CompletedEvent = new AutoResetEvent(false))
					{
						int NumCompletedActions = 0;
						using (ProgressWriter ProgressWriter = new ProgressWriter("Compiling C++ source code...", false))
						{
							while (QueuedActions.Count > 0 || ExecutingActions.Count > 0)
							{
								// Sort the actions by the number of things dependent on them
								QueuedActions.Sort((A, B) => (A.TotalDependantCount == B.TotalDependantCount) ? (B.SortIndex - A.SortIndex) : (B.TotalDependantCount - A.TotalDependantCount));

								// Create threads up to the maximum number of actions
								while (ExecutingActions.Count < ActualNumParallelProcesses && QueuedActions.Count > 0)
								{
									BuildAction Action = QueuedActions[QueuedActions.Count - 1];
									QueuedActions.RemoveAt(QueuedActions.Count - 1);

									Thread ExecutingThread = new Thread(() => { ExecuteAction(ProcessGroup, Action, CompletedActions, CompletedEvent); });
									string Description = $"{(Action.Inner.CommandDescription != null ? Action.Inner.CommandDescription : Action.Inner.CommandPath.GetFileName())} {Action.Inner.StatusDescription}".Trim();
									ExecutingThread.Name = String.Format("Build:{0}", Description);
									ExecutingThread.Start();

									ExecutingActions.Add(Action, ExecutingThread);
								}

								// Wait for something to finish
								CompletedEvent.WaitOne();

								// Wait for something to finish and flush it to the log
								lock (CompletedActions)
								{
									foreach (BuildAction CompletedAction in CompletedActions)
									{
										// Join the thread
										Thread CompletedThread = ExecutingActions[CompletedAction];
										CompletedThread.Join();
										ExecutingActions.Remove(CompletedAction);

										// Update the progress
										NumCompletedActions++;
										ProgressWriter.Write(NumCompletedActions, InputActions.Count);

										string Description = string.Empty;

										// Write it to the log
										if (CompletedAction.Inner.bShouldOutputStatusDescription || CompletedAction.LogLines.Count == 0)
										{
											Description = $"{(CompletedAction.Inner.CommandDescription != null ? CompletedAction.Inner.CommandDescription : CompletedAction.Inner.CommandPath.GetFileNameWithoutExtension())} {CompletedAction.Inner.StatusDescription}".Trim();
										}
										else if (CompletedAction.LogLines.Count > 0)
										{
											Description = $"{(CompletedAction.Inner.CommandDescription != null ? CompletedAction.Inner.CommandDescription : CompletedAction.Inner.CommandPath.GetFileNameWithoutExtension())} {CompletedAction.LogLines[0]}".Trim();
										}

										Log.TraceInformation("[{0}/{1}] {2}", NumCompletedActions, InputActions.Count, Description);
										foreach (string Line in CompletedAction.LogLines.Skip(CompletedAction.Inner.bShouldOutputStatusDescription ? 0 : 1))
										{
											Log.TraceInformation(Line);
										}

										// Check the exit code
										if (CompletedAction.ExitCode == 0)
										{
											// Mark all the dependents as done
											foreach (BuildAction DependantAction in CompletedAction.Dependants)
											{
												if (--DependantAction.MissingDependencyCount == 0)
												{
													QueuedActions.Add(DependantAction);
												}
											}
										}
										else
										{
											// BEGIN TEMPORARY TO CATCH PVS-STUDIO ISSUES
											if (CompletedAction.LogLines.Count == 0)
											{
												Log.TraceInformation("[{0}/{1}] {2} - Error but no output", NumCompletedActions, InputActions.Count, Description);
												Log.TraceInformation("[{0}/{1}] {2} - {3} {4} {5} {6}", NumCompletedActions, InputActions.Count, Description, CompletedAction.ExitCode,
													CompletedAction.Inner.WorkingDirectory, CompletedAction.Inner.CommandPath, CompletedAction.Inner.CommandArguments);
											}
											// END TEMPORARY
											// Update the exit code if it's not already set
											if (bResult && CompletedAction.ExitCode != 0)
											{
												bResult = false;
											}
										}
									}
									CompletedActions.Clear();
								}

								// If we've already got a non-zero exit code, clear out the list of queued actions so nothing else will run
								if (!bResult && bStopCompilationAfterErrors)
								{
									QueuedActions.Clear();
								}
							}
						}
					}
				}

				return bResult;
			}
		}

		/// <summary>
		/// Execute an individual action
		/// </summary>
		/// <param name="ProcessGroup">The process group</param>
		/// <param name="Action">The action to execute</param>
		/// <param name="CompletedActions">On completion, the list to add the completed action to</param>
		/// <param name="CompletedEvent">Event to set once an event is complete</param>
		static void ExecuteAction(ManagedProcessGroup ProcessGroup, BuildAction Action, List<BuildAction> CompletedActions, AutoResetEvent CompletedEvent)
		{
			try
			{
				using (ManagedProcess Process = new ManagedProcess(ProcessGroup, Action.Inner.CommandPath.FullName, Action.Inner.CommandArguments, Action.Inner.WorkingDirectory.FullName, null, null, ProcessPriorityClass.BelowNormal))
				{
					Action.LogLines.AddRange(Process.ReadAllLines());
					Action.ExitCode = Process.ExitCode;
				}
			}
			catch (Exception Ex)
			{
				Log.WriteException(Ex, null);
				Action.ExitCode = 1;
			}

			lock (CompletedActions)
			{
				CompletedActions.Add(Action);
			}

			CompletedEvent.Set();
		}

		/// <summary>
		/// Increment the number of dependants of an action, recursively
		/// </summary>
		/// <param name="Action">The action to update</param>
		/// <param name="VisitedActions">Set of visited actions</param>
		private static void RecursiveIncDependents(BuildAction Action, HashSet<BuildAction> VisitedActions)
		{
			foreach (BuildAction Dependency in Action.Dependants)
			{
				if (!VisitedActions.Contains(Action))
				{
					VisitedActions.Add(Action);
					Dependency.TotalDependantCount++;
					RecursiveIncDependents(Dependency, VisitedActions);
				}
			}
		}
	}

	/// <summary>
	/// Publicly visible static class that allows external access to the parallel executor config
	/// </summary>
	public static class ParallelExecutorConfiguration
	{
		/// <summary>
		/// Maximum number of processes that should be used for execution
		/// </summary>
		public static int MaxParallelProcesses { get { return ParallelExecutor.GetDefaultNumParallelProcesses(); } }
	}
}
