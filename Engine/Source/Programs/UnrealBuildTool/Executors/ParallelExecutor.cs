// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealBuildTool
{
	/// <summary>
	/// This executor uses async Tasks to process the action graph
	/// </summary>
	class ParallelExecutor : ActionExecutor
	{
		/// <summary>
		/// Maximum processor count for local execution. 
		/// </summary>
		[XmlConfigFile]
		private static int MaxProcessorCount = int.MaxValue;

		/// <summary>
		/// Processor count multiplier for local execution. Can be below 1 to reserve CPU for other tasks.
		/// When using the local executor (not XGE), run a single action on each CPU core. Note that you can set this to a larger value
		/// to get slightly faster build times in many cases, but your computer's responsiveness during compiling may be much worse.
		/// This value is ignored if the CPU does not support hyper-threading.
		/// </summary>
		[XmlConfigFile]
		private static double ProcessorCountMultiplier = 1.0;

		/// <summary>
		/// Free memory per action in bytes, used to limit the number of parallel actions if the machine is memory starved.
		/// Set to 0 to disable free memory checking.
		/// </summary>
		[XmlConfigFile]
		private static double MemoryPerActionBytes = 1.5 * 1024 * 1024 * 1024;

		/// <summary>
		/// The priority to set for spawned processes.
		/// Valid Settings: Idle, BelowNormal, Normal, AboveNormal, High
		/// Default: BelowNormal
		/// </summary>
		[XmlConfigFile]
		private static ProcessPriorityClass ProcessPriority = ProcessPriorityClass.BelowNormal;

		/// <summary>
		/// When enabled, will stop compiling targets after a compile error occurs.
		/// </summary>
		[XmlConfigFile]
		private static bool bStopCompilationAfterErrors = false;

		/// <summary>
		/// Whether to show compilation times along with worst offenders or not.
		/// </summary>
		[XmlConfigFile]
		private static bool bShowCompilationTimes = false;

		/// <summary>
		/// Whether to show compilation times for each executed action
		/// </summary>
		[XmlConfigFile]
		private static bool bShowPerActionCompilationTimes = false;

		/// <summary>
		/// Whether to log command lines for actions being executed
		/// </summary>
		[XmlConfigFile]
		private static bool bLogActionCommandLines = false;

		/// <summary>
		/// Add target names for each action executed
		/// </summary>
		[XmlConfigFile]
		private static bool bPrintActionTargetNames = false;

		/// <summary>
		/// Collapse non-error output lines
		/// </summary>
		private bool bCompactOutput = false;

		/// <summary>
		/// How many processes that will be executed in parallel
		/// </summary>
		public int NumParallelProcesses { get; private set; }

		private static readonly char[] LineEndingSplit = new char[] { '\n', '\r' };

		public static int GetDefaultNumParallelProcesses()
		{
			double MemoryPerActionBytesComputed = Math.Max(MemoryPerActionBytes, MemoryPerActionBytesOverride);
			if (MemoryPerActionBytesComputed > MemoryPerActionBytes)
			{
				Log.TraceInformation($"Overriding MemoryPerAction with target-defined value of {MemoryPerActionBytesComputed / 1024 / 1024 / 1024} bytes");
			}

			return Utils.GetMaxActionsToExecuteInParallel(MaxProcessorCount, ProcessorCountMultiplier, Convert.ToInt64(MemoryPerActionBytesComputed));
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="MaxLocalActions">How many actions to execute in parallel</param>
		/// <param name="bCompactOutput">Should output be written in a compact fashion</param>
		public ParallelExecutor(int MaxLocalActions, bool bCompactOutput)
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

			this.bCompactOutput = bCompactOutput;
		}

		/// <summary>
		/// Returns the name of this executor
		/// </summary>
		public override string Name
		{
			get { return "Parallel"; }
		}

		/// <summary>
		/// Checks whether the task executor can be used
		/// </summary>
		/// <returns>True if the task executor can be used</returns>
		public static bool IsAvailable()
		{
			return true;
		}

		protected class ExecuteResults
		{
			public List<string> LogLines { get; private set; }
			public int ExitCode { get; private set; }
			public TimeSpan ExecutionTime { get; private set; }
			public TimeSpan ProcessorTime { get; private set; }
			public string? AdditionalDescription { get; protected set; } = null;

			public ExecuteResults(List<string> LogLines, int ExitCode, TimeSpan ExecutionTime, TimeSpan ProcessorTime)
			{
				this.LogLines = LogLines;
				this.ExitCode = ExitCode;
				this.ProcessorTime = ProcessorTime;
				this.ExecutionTime = ExecutionTime;
			}
			public ExecuteResults(List<string> LogLines, int ExitCode)
			{
				this.LogLines = LogLines;
				this.ExitCode = ExitCode;
			}
		}

		/// <summary>
		/// Executes the specified actions locally.
		/// </summary>
		/// <returns>True if all the tasks successfully executed, or false if any of them failed.</returns>
		public override bool ExecuteActions(List<LinkedAction> InputActions)
		{
			int NumCompletedActions = 0;
			int TotalActions = InputActions.Count;
			int ActualNumParallelProcesses = Math.Min(TotalActions, NumParallelProcesses);

			using ManagedProcessGroup ProcessGroup = new ManagedProcessGroup();
			using SemaphoreSlim MaxProcessSemaphore = new SemaphoreSlim(ActualNumParallelProcesses, ActualNumParallelProcesses);
			using ProgressWriter ProgressWriter = new ProgressWriter("Compiling C++ source code...", false);

			Log.TraceInformation($"Building {TotalActions} {(TotalActions == 1 ? "action" : "actions")} with {ActualNumParallelProcesses} {(ActualNumParallelProcesses == 1 ? "process" : "processes")}...");

			Dictionary<LinkedAction, Task<ExecuteResults>> ExecuteTasks = new Dictionary<LinkedAction, Task<ExecuteResults>>();
			List<Task> LogTasks = new List<Task>();

			using LogIndentScope Indent = new LogIndentScope("  ");

			CancellationTokenSource CancellationTokenSource = new CancellationTokenSource();
			CancellationToken CancellationToken = CancellationTokenSource.Token;

			// Create a task for every action
			foreach (LinkedAction Action in InputActions)
			{
				if (ExecuteTasks.ContainsKey(Action))
				{
					continue;
				}

				Task<ExecuteResults> ExecuteTask = CreateExecuteTask(Action, InputActions, ExecuteTasks, ProcessGroup, MaxProcessSemaphore, CancellationToken);
				Task LogTask = ExecuteTask.ContinueWith(antecedent => LogCompletedAction(Action, antecedent, CancellationTokenSource, ProgressWriter, TotalActions, ref NumCompletedActions), CancellationToken);

				ExecuteTasks.Add(Action, ExecuteTask);
				LogTasks.Add(LogTask);
			}

			Task SummaryTask = Task.Factory.ContinueWhenAll(LogTasks.ToArray(), (AntecedentTasks) => TraceSummary(ExecuteTasks, ProcessGroup), CancellationToken);
			SummaryTask.Wait();

			// Return if all tasks succeeded
			return ExecuteTasks.Values.All(x => x.Result.ExitCode == 0);
		}

		protected static Task<ExecuteResults> CreateExecuteTask(LinkedAction Action, List<LinkedAction> InputActions, Dictionary<LinkedAction, Task<ExecuteResults>> ExecuteTasks, ManagedProcessGroup ProcessGroup, SemaphoreSlim MaxProcessSemaphore, CancellationToken CancellationToken)
		{
			List<LinkedAction> PrerequisiteActions = Action.PrerequisiteActions.Where(x => InputActions.Contains(x)).ToList();
			if (PrerequisiteActions.Count == 0)
			{
				return Task.Factory.StartNew(
					() => ExecuteAction(new Task<ExecuteResults>[0], Action, ProcessGroup, MaxProcessSemaphore, CancellationToken),
					CancellationToken,
					TaskCreationOptions.LongRunning,
					TaskScheduler.Current
				).Unwrap();
			}

			// Create tasks for any preresquite actions if they don't exist already
			List<Task<ExecuteResults>> PrerequisiteTasks = new List<Task<ExecuteResults>>();
			foreach (var PrerequisiteAction in PrerequisiteActions)
			{
				if (!ExecuteTasks.ContainsKey(PrerequisiteAction))
				{
					ExecuteTasks.Add(PrerequisiteAction, CreateExecuteTask(PrerequisiteAction, InputActions, ExecuteTasks, ProcessGroup, MaxProcessSemaphore, CancellationToken));
				}
				PrerequisiteTasks.Add(ExecuteTasks[PrerequisiteAction]);
			}

			return Task.Factory.ContinueWhenAll(
				PrerequisiteTasks.ToArray(),
				(AntecedentTasks) => ExecuteAction(AntecedentTasks, Action, ProcessGroup, MaxProcessSemaphore, CancellationToken),
				CancellationToken,
				TaskContinuationOptions.LongRunning,
				TaskScheduler.Current
			).Unwrap();
		}

		protected static async Task WaitPrerequsiteActions(LinkedAction Action, Dictionary<LinkedAction, Task<ExecuteResults>> Tasks)
		{
			// Wait for all PrerequisiteActions to complete
			ExecuteResults[] Results = await Task.WhenAll(Action.PrerequisiteActions.Select(x => Tasks[x]));

			// Cancel tasks if any PrerequisiteActions fail, unless a PostBuildStep
			if (Action.ActionType != ActionType.PostBuildStep && Results.Any(x => x.ExitCode != 0))
			{
				throw new OperationCanceledException();
			}
		}

		protected static async Task<ExecuteResults> ExecuteAction(Task<ExecuteResults>[] AntecedentTasks, LinkedAction Action, ManagedProcessGroup ProcessGroup, SemaphoreSlim MaxProcessSemaphore, CancellationToken CancellationToken)
		{
			Task? SemaphoreTask = null;
			try
			{
				// Cancel tasks if any PrerequisiteActions fail, unless a PostBuildStep
				if (Action.ActionType != ActionType.PostBuildStep && AntecedentTasks.Any(x => x.Result.ExitCode != 0))
				{
					throw new OperationCanceledException();
				}

				// Limit the number of concurrent processes that will run in parallel
				SemaphoreTask = MaxProcessSemaphore.WaitAsync(CancellationToken);
				await SemaphoreTask;
				return await RunAction(Action, ProcessGroup, CancellationToken);
			}
			catch (OperationCanceledException)
			{
				return new ExecuteResults(new List<string>(), int.MaxValue);
			}
			catch (Exception Ex)
			{
				Log.WriteException(Ex, null);
				return new ExecuteResults(new List<string>(), int.MaxValue);
			}
			finally
			{
				if (SemaphoreTask?.Status == TaskStatus.RanToCompletion)
				{
					MaxProcessSemaphore.Release();
				}
			}
		}

		protected static async Task<ExecuteResults> RunAction(LinkedAction Action, ManagedProcessGroup ProcessGroup, CancellationToken CancellationToken)
		{
			CancellationToken.ThrowIfCancellationRequested();

			using ManagedProcess Process = new ManagedProcess(ProcessGroup, Action.CommandPath.FullName, Action.CommandArguments, Action.WorkingDirectory.FullName, null, null, ProcessPriority);

			using MemoryStream StdOutStream = new MemoryStream();
			await Process.CopyToAsync(StdOutStream, CancellationToken);

			CancellationToken.ThrowIfCancellationRequested();

			Process.WaitForExit();

			List<string> LogLines = Console.OutputEncoding.GetString(StdOutStream.GetBuffer(), 0, Convert.ToInt32(StdOutStream.Length)).Split(LineEndingSplit, StringSplitOptions.RemoveEmptyEntries).ToList();
			int ExitCode = Process.ExitCode;
			TimeSpan ProcessorTime = Process.TotalProcessorTime;
			TimeSpan ExecutionTime = Process.ExitTime - Process.StartTime;
			return new ExecuteResults(LogLines, ExitCode, ExecutionTime, ProcessorTime);
		}

		private static int s_previousLineLength = -1;

		protected void LogCompletedAction(LinkedAction Action, Task<ExecuteResults> ExecuteTask, CancellationTokenSource CancellationTokenSource, ProgressWriter ProgressWriter, int TotalActions, ref int NumCompletedActions)
		{
			List<string> LogLines = new List<string>();
			int ExitCode = int.MaxValue;
			TimeSpan ExecutionTime = TimeSpan.Zero;
			TimeSpan ProcessorTime = TimeSpan.Zero;
			string? AdditionalDescription = null;
			if (ExecuteTask.Status == TaskStatus.RanToCompletion)
			{
				ExecuteResults ExecuteTaskResult = ExecuteTask.Result;
				LogLines = ExecuteTaskResult.LogLines;
				ExitCode = ExecuteTaskResult.ExitCode;
				ExecutionTime = ExecuteTaskResult.ExecutionTime;
				ProcessorTime = ExecuteTaskResult.ProcessorTime;
				AdditionalDescription = ExecuteTaskResult.AdditionalDescription;
			}

			// Write it to the log
			string Description = string.Empty;
			if (Action.bShouldOutputStatusDescription || LogLines.Count == 0)
			{
				Description = $"{(Action.CommandDescription ?? Action.CommandPath.GetFileNameWithoutExtension())} {Action.StatusDescription}".Trim();
			}
			else if (LogLines.Count > 0)
			{
				Description = $"{(Action.CommandDescription ?? Action.CommandPath.GetFileNameWithoutExtension())} {LogLines[0]}".Trim();
			}
			if (!string.IsNullOrEmpty(AdditionalDescription))
            {
				Description = $"{AdditionalDescription} {Description}";
			}

			lock (ProgressWriter)
			{
				int CompletedActions;
				CompletedActions = Interlocked.Increment(ref NumCompletedActions);
				ProgressWriter.Write(CompletedActions, TotalActions);

				// Cancelled
				if (ExitCode == int.MaxValue)
				{
					Log.TraceInformation($"[{CompletedActions}/{TotalActions}] {Description} cancelled");
					return;
				}

				string TargetDetails = "";
				TargetDescriptor? Target = Action.Target;
				if (bPrintActionTargetNames && Target != null)
				{
					TargetDetails = $"[{Target.Name} {Target.Platform} {Target.Configuration}]";
				}

				if (bLogActionCommandLines)
				{
					Log.TraceLog($"[{CompletedActions}/{TotalActions}]{TargetDetails} Command: {Action.CommandPath} {Action.CommandArguments}");
				}

				string CompilationTimes = "";

				if (bShowPerActionCompilationTimes)
				{
					if (ProcessorTime.Ticks > 0)
					{
						CompilationTimes = $" (Wall: {ExecutionTime.TotalSeconds:0.00}s CPU: {ProcessorTime.TotalSeconds:0.00}s)";
					}
					else
					{
						CompilationTimes = $" (Wall: {ExecutionTime.TotalSeconds:0.00}s)";
					}
				}

				string message = ($"[{CompletedActions}/{TotalActions}]{TargetDetails}{CompilationTimes} {Description}");
				
				if (bCompactOutput)
				{
					if (s_previousLineLength > 0)
					{
						// move the cursor to the far left position, one line back
						Console.CursorLeft = 0;
						Console.CursorTop -= 1;
						// clear the line
						Console.Write("".PadRight(s_previousLineLength));
						// move the cursor back to the left, so output is written to the desired location
						Console.CursorLeft = 0;
					}
				}

				s_previousLineLength = message.Length;

				Log.TraceInformation(message);
				foreach (string Line in LogLines.Skip(Action.bShouldOutputStatusDescription ? 0 : 1))
				{
					// suppress library creation messages when writing compact output
					if (bCompactOutput && Line.StartsWith("   Creating library ") && Line.EndsWith(".exp"))
					{
						continue;
					}

					Log.TraceInformation(Line);

					// Prevent overwriting of logged lines
					s_previousLineLength = -1;
				}

				if (ExitCode != 0)
				{
					// BEGIN TEMPORARY TO CATCH PVS-STUDIO ISSUES
					if (LogLines.Count == 0)
					{
						Log.TraceError($"{TargetDetails} {Description}: Exited with error code {ExitCode}");
						Log.TraceInformation($"{TargetDetails} {Description}: WorkingDirectory {Action.WorkingDirectory}");
						Log.TraceInformation($"{TargetDetails} {Description}: {Action.CommandPath} {Action.CommandArguments}");
					}
					// END TEMPORARY

					// prevent overrwriting of error text
					s_previousLineLength = -1;

					// Cancel all other pending tasks
					if (bStopCompilationAfterErrors)
					{
						CancellationTokenSource.Cancel();
					}
				}
			}
		}

		protected static void TraceSummary(Dictionary<LinkedAction, Task<ExecuteResults>> Tasks, ManagedProcessGroup ProcessGroup)
		{
			if (!bShowCompilationTimes)
			{
				return;
			}

			Log.TraceInformation("");
			if (ProcessGroup.TotalProcessorTime.Ticks > 0)
			{
				Log.TraceInformation($"Total CPU Time: {ProcessGroup.TotalProcessorTime.TotalSeconds} s");
				Log.TraceInformation("");
			}

			var CompletedTasks = Tasks.Where(x => x.Value.Status == TaskStatus.RanToCompletion).OrderByDescending(x => x.Value.Result.ExecutionTime).Take(20);

			if (CompletedTasks.Any())
			{
				Log.TraceInformation($"Compilation Time Top {CompletedTasks.Count()}");
				Log.TraceInformation("");
				foreach (var Pair in CompletedTasks)
				{
					string Description = $"{(Pair.Key.Inner.CommandDescription ?? Pair.Key.Inner.CommandPath.GetFileNameWithoutExtension())} {Pair.Key.Inner.StatusDescription}".Trim();
					if (Pair.Value.Result.ProcessorTime.Ticks > 0)
					{
						Log.TraceInformation($"{Description} [ Wall Time {Pair.Value.Result.ExecutionTime.TotalSeconds:0.00} s / CPU Time {Pair.Value.Result.ProcessorTime.TotalSeconds:0.00} s ]");
					}
					else
					{
						Log.TraceInformation($"{Description} [ Time {Pair.Value.Result.ExecutionTime.TotalSeconds:0.00} s ]");
					}

				}
				Log.TraceInformation("");
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
