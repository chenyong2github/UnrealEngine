// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.IO;
using System.Threading;
using System.Diagnostics;
using System.Management;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using Tools.DotNETCommon;

namespace AutomationTool
{
	public enum CtrlTypes
	{
		CTRL_C_EVENT = 0,
		CTRL_BREAK_EVENT,
		CTRL_CLOSE_EVENT,
		CTRL_LOGOFF_EVENT = 5,
		CTRL_SHUTDOWN_EVENT
	}

	public interface IProcess
	{
		void StopProcess(bool KillDescendants = true);
		bool HasExited { get; }
		string GetProcessName();
	}

	/// <summary>
	/// Tracks all active processes.
	/// </summary>
	public sealed class ProcessManager
	{
		public delegate bool CtrlHandlerDelegate(CtrlTypes EventType);

		// @todo: Add mono support
		[DllImport("Kernel32")]
		public static extern bool SetConsoleCtrlHandler(CtrlHandlerDelegate Handler, bool Add);

		/// <summary>
		/// List of active (running) processes.
		/// </summary>
		private static List<IProcess> ActiveProcesses = new List<IProcess>();
		/// <summary>
		/// Synchronization object
		/// </summary>
		private static object SyncObject = new object();


		/// <summary>
		/// Creates a new process and adds it to the tracking list.
		/// </summary>
		/// <returns>New Process objects</returns>
		public static IProcessResult CreateProcess(string AppName, bool bAllowSpew, bool bCaptureSpew, Dictionary<string, string> Env = null, LogEventType SpewVerbosity = LogEventType.Console, ProcessResult.SpewFilterCallbackType SpewFilterCallback = null)
		{
			var NewProcess = HostPlatform.Current.CreateProcess(AppName);
			if (Env != null)
			{
				foreach (var EnvPair in Env)
				{
					if (NewProcess.StartInfo.EnvironmentVariables.ContainsKey(EnvPair.Key))
					{
						NewProcess.StartInfo.EnvironmentVariables.Remove(EnvPair.Key);
					}
					if (!String.IsNullOrEmpty(EnvPair.Value))
					{
						NewProcess.StartInfo.EnvironmentVariables.Add(EnvPair.Key, EnvPair.Value);
					}
				}
			}
			var Result = new ProcessResult(AppName, NewProcess, bAllowSpew, bCaptureSpew, SpewVerbosity: SpewVerbosity, InSpewFilterCallback: SpewFilterCallback);
			AddProcess(Result);
			return Result;
		}

		public static void AddProcess(IProcess Proc)
		{
			lock (SyncObject)
			{
				ActiveProcesses.Add(Proc);
			}
		}

		public static void RemoveProcess(IProcess Proc)
		{
			lock (SyncObject)
			{
				ActiveProcesses.Remove(Proc);
			}
		}

		public static bool CanBeKilled(string ProcessName)
		{
			return !HostPlatform.Current.DontKillProcessList.Contains(ProcessName, StringComparer.InvariantCultureIgnoreCase);
		}

		/// <summary>
		/// Kills all running processes.
		/// </summary>
		public static void KillAll()
		{
			List<IProcess> ProcessesToKill = new List<IProcess>();
			lock (SyncObject)
			{
				foreach (var ProcResult in ActiveProcesses)
				{
					if (!ProcResult.HasExited)
					{
						ProcessesToKill.Add(ProcResult);
					}
				}
				ActiveProcesses.Clear();
			}
			// Remove processes that can't be killed
			for (int ProcessIndex = ProcessesToKill.Count - 1; ProcessIndex >= 0; --ProcessIndex )
			{
				var ProcessName = ProcessesToKill[ProcessIndex].GetProcessName();
				if (!String.IsNullOrEmpty(ProcessName) && !CanBeKilled(ProcessName))
				{
					CommandUtils.LogLog("Ignoring process \"{0}\" because it can't be killed.", ProcessName);
					ProcessesToKill.RemoveAt(ProcessIndex);
				}
			}
			if(ProcessesToKill.Count > 0)
			{
				CommandUtils.LogLog("Trying to kill {0} spawned processes.", ProcessesToKill.Count);
				foreach (var Proc in ProcessesToKill)
				{
					CommandUtils.LogLog("  {0}", Proc.GetProcessName());
				}
				if (CommandUtils.IsBuildMachine)
				{
					for (int Cnt = 0; Cnt < 9; Cnt++)
					{
						bool AllDone = true;
						foreach (var Proc in ProcessesToKill)
						{
							try
							{
								if (!Proc.HasExited)
								{
									AllDone = false;
									CommandUtils.LogLog("Waiting for process: {0}", Proc.GetProcessName());
								}
							}
							catch (Exception)
							{
								CommandUtils.LogWarning("Exception Waiting for process");
								AllDone = false;
							}
						}
						try
						{
							if (ProcessResult.HasAnyDescendants(Process.GetCurrentProcess()))
							{
								AllDone = false;
								CommandUtils.LogInformation("Waiting for descendants of main process...");
							}
						}
						catch (Exception Ex)
						{
							CommandUtils.LogWarning("Exception Waiting for descendants of main process. " + Ex);
							AllDone = false;
						}

						if (AllDone)
						{
							break;
						}
						Thread.Sleep(10000);
					}
				}
				foreach (var Proc in ProcessesToKill)
				{
					var ProcName = Proc.GetProcessName();
					try
					{
						if (!Proc.HasExited)
						{
							CommandUtils.LogLog("Killing process: {0}", ProcName);
							Proc.StopProcess(false);
						}
					}
					catch (Exception Ex)
					{
						CommandUtils.LogWarning("Exception while trying to kill process {0}:", ProcName);
						CommandUtils.LogWarning(LogUtils.FormatException(Ex));
					}
				}
				try
				{
					if (CommandUtils.IsBuildMachine && ProcessResult.HasAnyDescendants(Process.GetCurrentProcess()))
					{
						CommandUtils.LogLog("current process still has descendants, trying to kill them...");
						ProcessResult.KillAllDescendants(Process.GetCurrentProcess());
					}
				}
				catch (Exception)
				{
					CommandUtils.LogWarning("Exception killing descendants of main process");
				}
			}
		}
	}

	#region ProcessResult Helper Class

	public interface IProcessResult : IProcess
	{
		void OnProcessExited();
		void DisposeProcess();
		void StdOut(object sender, DataReceivedEventArgs e);
		void StdErr(object sender, DataReceivedEventArgs e);
		int ExitCode{ get;set; }
		string Output {get;}
		Process ProcessObject {get;}
		string ToString();
		void WaitForExit();
	}
	
	/// <summary>
	/// Class containing the result of process execution.
	/// </summary>
	public class ProcessResult : IProcessResult
	{
		public delegate string SpewFilterCallbackType(string Message);

		private int ProcessExitCode = -1;
		private StringBuilder ProcessOutput;
		private bool AllowSpew = true;
		private LogEventType SpewVerbosity = LogEventType.Console;
		private SpewFilterCallbackType SpewFilterCallback = null;
		private string AppName = String.Empty;
		private Process Proc = null;
		private AutoResetEvent OutputWaitHandle = new AutoResetEvent(false);
		private AutoResetEvent ErrorWaitHandle = new AutoResetEvent(false);
		private object ProcSyncObject;

		public ProcessResult(string InAppName, Process InProc, bool bAllowSpew, bool bCaptureSpew = true, LogEventType SpewVerbosity = LogEventType.Console, SpewFilterCallbackType InSpewFilterCallback = null)
		{
			AppName = InAppName;
			ProcSyncObject = new object();
			Proc = InProc;
			AllowSpew = bAllowSpew;
			if(bCaptureSpew)
			{
				ProcessOutput = new StringBuilder();
			}
			else
			{
				OutputWaitHandle.Set();
				ErrorWaitHandle.Set();
			}
			this.SpewVerbosity = SpewVerbosity;
			SpewFilterCallback = InSpewFilterCallback;
			if (Proc != null)
			{
				Proc.EnableRaisingEvents = false;
			}
		}

        ~ProcessResult()
        {
            if(Proc != null)
            {
                Proc.Dispose();
            }
        }

		/// <summary>
		/// Removes a process from the list of tracked processes.
		/// </summary>
		public void OnProcessExited()
		{
			ProcessManager.RemoveProcess(this);
		}

        /// <summary>
        /// Log output of a remote process at a given severity.
        /// To pretty up the output, we use a custom source so it will say the source of the process instead of this method name.
        /// </summary>
        /// <param name="Verbosity"></param>
        /// <param name="Message"></param>
        [MethodImplAttribute(MethodImplOptions.NoInlining)]
		private void LogOutput(LogEventType Verbosity, string Message)
		{
            Log.WriteLine(1, Verbosity, Message);
		}
       
		/// <summary>
		/// Manually dispose of Proc and set it to null.
		/// </summary>
        public void DisposeProcess()
        {
			if(Proc != null)
			{
				Proc.Dispose();
				Proc = null;
			}
        }

		/// <summary>
		/// Process.OutputDataReceived event handler.
		/// </summary>
		/// <param name="sender">Sender</param>
		/// <param name="e">Event args</param>
		public void StdOut(object sender, DataReceivedEventArgs e)
		{
			if (e.Data != null)
			{
				if (AllowSpew)
				{
					if (SpewFilterCallback != null)
					{
						string FilteredSpew = SpewFilterCallback(e.Data);
						if (FilteredSpew != null)
						{
							LogOutput(SpewVerbosity, FilteredSpew);
						}
					}
					else
					{
						LogOutput(SpewVerbosity, e.Data);
					}
				}

				if(ProcessOutput != null)
				{
					lock (ProcSyncObject)
					{
						ProcessOutput.Append(e.Data);
						ProcessOutput.Append(Environment.NewLine);
					}
				}
			}
			else
			{
				OutputWaitHandle.Set();
			}
		}

		/// <summary>
		/// Process.ErrorDataReceived event handler.
		/// </summary>
		/// <param name="sender">Sender</param>
		/// <param name="e">Event args</param>
		public void StdErr(object sender, DataReceivedEventArgs e)
		{
			if (e.Data != null)
			{
				if (SpewFilterCallback != null)
				{
					string FilteredSpew = SpewFilterCallback(e.Data);
					if (FilteredSpew != null)
					{
						LogOutput(SpewVerbosity, FilteredSpew);
					}
				}
				else
				{
					LogOutput(SpewVerbosity, e.Data);
				}
				if(ProcessOutput != null)
				{
					lock (ProcSyncObject)
					{
						ProcessOutput.Append(e.Data);
						ProcessOutput.Append(Environment.NewLine);
					}
				}
			}
			else
			{
				ErrorWaitHandle.Set();
			}
		}

		/// <summary>
		/// Convenience operator for getting the exit code value.
		/// </summary>
		/// <param name="Result"></param>
		/// <returns>Process exit code.</returns>
		[Obsolete]
		public static implicit operator int(ProcessResult Result)
		{
			return Result.ExitCode;
		}

		/// <summary>
		/// Gets or sets the process exit code.
		/// </summary>
		public int ExitCode
		{
			get { return ProcessExitCode; }
			set { ProcessExitCode = value; }
		}

		/// <summary>
		/// Gets all std output the process generated.
		/// </summary>
		public string Output
		{
			get
			{
				if (ProcessOutput == null)
				{
					return null;
				}
				lock (ProcSyncObject)
				{
					return ProcessOutput.ToString();
				}
			}
		}

		public bool HasExited
		{
			get 
			{
				bool bHasExited = true;
				lock (ProcSyncObject)
				{
					if (Proc != null)
					{
						bHasExited = Proc.HasExited;
						if (bHasExited)
						{
							ExitCode = Proc.ExitCode;
						}
					}
				}
				return bHasExited; 
			}
		}

		public Process ProcessObject
		{
			get { return Proc; }
		}

		/// <summary>
		/// Thread-safe way of getting the process name
		/// </summary>
		/// <returns>Name of the process this object represents</returns>
		public string GetProcessName()
		{
			string Name = null;
			lock (ProcSyncObject)
			{
				try
				{
					if (Proc != null && !Proc.HasExited)
					{
						Name = Proc.ProcessName;
					}
				}
				catch
				{
					// Ignore all exceptions
				}
			}
			if (String.IsNullOrEmpty(Name))
			{
				Name = "[EXITED] " + AppName;
			}
			return Name;
		}

		/// <summary>
		/// Object iterface.
		/// </summary>
		/// <returns>String representation of this object.</returns>
		public override string ToString()
		{
			return ExitCode.ToString();
		}

		public void WaitForExit()
		{
			bool bProcTerminated = false;
			bool bStdOutSignalReceived = false;
			bool bStdErrSignalReceived = false;
			// Make sure the process objeect is valid.
			lock (ProcSyncObject)
			{
				bProcTerminated = (Proc == null);
			}
			// Keep checking if we got all output messages until the process terminates.
			if (!bProcTerminated)
			{
				// Check messages
				int MaxWaitUntilMessagesReceived = 120;
				while (MaxWaitUntilMessagesReceived > 0 && !(bStdOutSignalReceived && bStdErrSignalReceived))
				{
					if (!bStdOutSignalReceived)
					{
						bStdOutSignalReceived = OutputWaitHandle.WaitOne(500);
					}
					if (!bStdErrSignalReceived)
					{
						bStdErrSignalReceived = ErrorWaitHandle.WaitOne(500);
					}
					// Check if the process terminated
					lock (ProcSyncObject)
					{
						bProcTerminated = (Proc == null) || Proc.HasExited;
					}
					if (bProcTerminated)
					{
						// Process terminated but make sure we got all messages, don't wait forever though
						MaxWaitUntilMessagesReceived--;
					}
				}
                if (!(bStdOutSignalReceived && bStdErrSignalReceived))
                {
					CommandUtils.LogLog("Waited for a long time for output of {0}, some output may be missing; we gave up.", AppName);
                }

				// Double-check if the process terminated
				lock (ProcSyncObject)
				{
					bProcTerminated = (Proc == null) || Proc.HasExited;

					if (Proc != null)
					{
						if (!bProcTerminated)
						{
							// The process did not terminate yet but we've read all output messages, wait until the process terminates
							Proc.WaitForExit();
						}

						ExitCode = Proc.ExitCode;
					}
				}
			}
		}

		/// <summary>
		/// Finds child processes of the current process.
		/// </summary>
		/// <param name="ProcessId"></param>
		/// <param name="PossiblyRelatedId"></param>
		/// <param name="VisitedPids"></param>
		/// <returns></returns>
		private static bool IsOurDescendant(Process ProcessToKill, int PossiblyRelatedId, HashSet<int> VisitedPids)
		{
			// check if we're the parent of it or its parent is our descendant
			try
			{
				VisitedPids.Add(PossiblyRelatedId);
				Process Parent = null;
				using (ManagementObject ManObj = new ManagementObject(string.Format("win32_process.handle='{0}'", PossiblyRelatedId)))
				{
					ManObj.Get();
					int ParentId = Convert.ToInt32(ManObj["ParentProcessId"]);
					if (ParentId == 0 || VisitedPids.Contains(ParentId))
					{
						return false;
					}
					Parent = Process.GetProcessById(ParentId);  // will throw an exception if not spawned by us or not running
				}
				if (Parent != null)
				{
					return Parent.Id == ProcessToKill.Id || IsOurDescendant(ProcessToKill, Parent.Id, VisitedPids);  // could use ParentId, but left it to make the above var used
				}
				else
				{
					return false;
				}
			}
			catch (Exception)
			{
				// This can happen if the pid is no longer valid which is ok.
				return false;
			}
		}

		/// <summary>
		/// Kills all child processes of the specified process.
		/// </summary>
		/// <param name="ProcessId">Process id</param>
		public static void KillAllDescendants(Process ProcessToKill)
		{
			bool bKilledAChild;
			do
			{
				bKilledAChild = false;
				// For some reason Process.GetProcesses() sometimes returns the same process twice
				// So keep track of all processes we already tried to kill
				var KilledPids = new HashSet<int>();
				var AllProcs = Process.GetProcesses();
				foreach (Process KillCandidate in AllProcs)
				{
					var VisitedPids = new HashSet<int>();
					if (ProcessManager.CanBeKilled(KillCandidate.ProcessName) && 
						!KilledPids.Contains(KillCandidate.Id) && 
						IsOurDescendant(ProcessToKill, KillCandidate.Id, VisitedPids))
					{
						KilledPids.Add(KillCandidate.Id);
						CommandUtils.LogLog("Trying to kill descendant pid={0}, name={1}", KillCandidate.Id, KillCandidate.ProcessName);
						try
						{
							KillCandidate.Kill();
							bKilledAChild = true;
						}
						catch (Exception Ex)
						{
							if(!KillCandidate.HasExited)
							{
								CommandUtils.LogWarning("Failed to kill descendant:");
								CommandUtils.LogWarning(LogUtils.FormatException(Ex));
							}
						}
						break;  // exit the loop as who knows what else died, so let's get processes anew
					}
				}
			} while (bKilledAChild);
		}

        /// <summary>
        /// returns true if this process has any descendants
        /// </summary>
        /// <param name="ProcessToCheck">Process to check</param>
        public static bool HasAnyDescendants(Process ProcessToCheck)
        {
            Process[] AllProcs = Process.GetProcesses();
            foreach (Process KillCandidate in AllProcs)
            {
				// Silently skip InvalidOperationExceptions here, because it depends on the process still running. It may have terminated.
				string ProcessName;
				try
				{
					ProcessName = KillCandidate.ProcessName;
				}
				catch(InvalidOperationException)
				{
					continue;
				}

				// Check if it's still running
				HashSet<int> VisitedPids = new HashSet<int>();
				if (ProcessManager.CanBeKilled(ProcessName) && IsOurDescendant(ProcessToCheck, KillCandidate.Id, VisitedPids))
				{
					CommandUtils.LogLog("Descendant pid={0}, name={1}", KillCandidate.Id, ProcessName);
					return true;
				}
            }
            return false;
        }

		public void StopProcess(bool KillDescendants = true)
		{
			if (Proc != null)
			{
				Process ProcToKill = null;
				// At this point any call to Proc memebers will result in an exception
				// so null the object.
				var ProcToKillName = GetProcessName();
				lock (ProcSyncObject)
				{
					ProcToKill = Proc;
					Proc = null;
				}
				// Now actually kill the process and all its descendants if requested
				if (KillDescendants)
				{
					KillAllDescendants(ProcToKill);
				}
				try
				{
					ProcToKill.Kill();
					ProcToKill.WaitForExit(60000);
					if (!ProcToKill.HasExited)
					{
						CommandUtils.LogLog("Process {0} failed to exit.", ProcToKillName);
					}
					else
					{
						CommandUtils.LogLog("Process {0} successfully exited.", ProcToKillName);
						OnProcessExited();
					}
					ProcToKill.Close();					
				}
				catch (Exception Ex)
				{
					CommandUtils.LogWarning("Exception while trying to kill process {0}:", ProcToKillName);
					CommandUtils.LogWarning(LogUtils.FormatException(Ex));
				}
			}
		}
	}

	#endregion

	public partial class CommandUtils
	{
		#region Statistics

		private static Dictionary<string, int> ExeToTimeInMs = new Dictionary<string, int>();

		public static void AddRunTime(string Exe, int TimeInMs)
		{
			lock(ExeToTimeInMs)
			{
				string Base = Path.GetFileName(Exe);
				if (!ExeToTimeInMs.ContainsKey(Base))
				{
					ExeToTimeInMs.Add(Base, TimeInMs);
				}
				else
				{
					ExeToTimeInMs[Base] += TimeInMs;
				}
			}
		}

		public static void PrintRunTime()
		{
			lock(ExeToTimeInMs)
			{
				foreach (var Item in ExeToTimeInMs)
				{
					LogVerbose("Total {0}s to run " + Item.Key, Item.Value / 1000);
				}
				ExeToTimeInMs.Clear();
			}
		}

		#endregion

		[Flags]
		public enum ERunOptions
		{
			None = 0,
			AllowSpew = 1 << 0,
			AppMustExist = 1 << 1,
			NoWaitForExit = 1 << 2,
			NoStdOutRedirect = 1 << 3,
            NoLoggingOfRunCommand = 1 << 4,
            UTF8Output = 1 << 5,

			/// When specified with AllowSpew, the output will be TraceEventType.Verbose instead of TraceEventType.Information
			SpewIsVerbose = 1 << 6,
            /// <summary>
            /// If NoLoggingOfRunCommand is set, it normally suppresses the run duration output. This turns it back on.
            /// </summary>
            LoggingOfRunDuration = 1 << 7,
			/// <summary>
			/// If set, a window is allowed to be created
			/// </summary>
			NoHideWindow = 1 << 8,

			/// <summary>
			/// Do not capture stdout in the process result
			/// </summary>
			NoStdOutCapture = 1 << 8,

			Default = AllowSpew | AppMustExist,
		}

		/// <summary>
		/// Runs external program.
		/// </summary>
		/// <param name="App">Program filename.</param>
		/// <param name="CommandLine">Commandline</param>
		/// <param name="Input">Optional Input for the program (will be provided as stdin)</param>
		/// <param name="Options">Defines the options how to run. See ERunOptions.</param>
		/// <param name="Env">Environment to pass to program.</param>
		/// <param name="FilterCallback">Callback to filter log spew before output.</param>
		/// <returns>Object containing the exit code of the program as well as it's stdout output.</returns>
		public static IProcessResult Run(string App, string CommandLine = null, string Input = null, ERunOptions Options = ERunOptions.Default, Dictionary<string, string> Env = null, ProcessResult.SpewFilterCallbackType SpewFilterCallback = null, string Identifier = null)
		{
			App = ConvertSeparators(PathSeparator.Default, App);

			HostPlatform.Current.SetupOptionsForRun(ref App, ref Options, ref CommandLine);
			if (App == "ectool" || App == "zip" || App == "xcodebuild")
			{
				Options &= ~ERunOptions.AppMustExist;
			}

			// Check if the application exists, including the PATH directories.
			if (Options.HasFlag(ERunOptions.AppMustExist) && !FileExists(Options.HasFlag(ERunOptions.NoLoggingOfRunCommand) ? true : false, App))
			{
				bool bExistsInPath = false;
				if(!App.Contains(Path.DirectorySeparatorChar) && !App.Contains(Path.AltDirectorySeparatorChar))
				{
					string[] PathDirectories = Environment.GetEnvironmentVariable("PATH").Split(Path.PathSeparator);
					foreach(string PathDirectory in PathDirectories)
					{
						string TryApp = Path.Combine(PathDirectory, App);
						if(FileExists(Options.HasFlag(ERunOptions.NoLoggingOfRunCommand), TryApp))
						{
							App = TryApp;
							bExistsInPath = true;
							break;
						}
					}
				}
				if(!bExistsInPath)
				{
					throw new AutomationException("BUILD FAILED: Couldn't find the executable to run: {0}", App);
				}
			}
			var StartTime = DateTime.UtcNow;

			LogEventType SpewVerbosity = Options.HasFlag(ERunOptions.SpewIsVerbose) ? LogEventType.Verbose : LogEventType.Console;
            if (!Options.HasFlag(ERunOptions.NoLoggingOfRunCommand))
            {
                LogWithVerbosity(SpewVerbosity,"Running: " + App + " " + (String.IsNullOrEmpty(CommandLine) ? "" : CommandLine));
            }

			string PrevIndent = null;
			if(Options.HasFlag(ERunOptions.AllowSpew))
			{
				PrevIndent = Tools.DotNETCommon.Log.Indent;
				Tools.DotNETCommon.Log.Indent += "  ";
			}

			IProcessResult Result = ProcessManager.CreateProcess(App, Options.HasFlag(ERunOptions.AllowSpew), !Options.HasFlag(ERunOptions.NoStdOutCapture), Env, SpewVerbosity: SpewVerbosity, SpewFilterCallback: SpewFilterCallback);
			try
			{
				Process Proc = Result.ProcessObject;

				bool bRedirectStdOut = (Options & ERunOptions.NoStdOutRedirect) != ERunOptions.NoStdOutRedirect;
				Proc.StartInfo.FileName = App;
				Proc.StartInfo.Arguments = String.IsNullOrEmpty(CommandLine) ? "" : CommandLine;
				Proc.StartInfo.UseShellExecute = false;
				if (bRedirectStdOut)
				{
					Proc.StartInfo.RedirectStandardOutput = true;
					Proc.StartInfo.RedirectStandardError = true;
					Proc.OutputDataReceived += Result.StdOut;
					Proc.ErrorDataReceived += Result.StdErr;
				}
				Proc.StartInfo.RedirectStandardInput = Input != null;
				Proc.StartInfo.CreateNoWindow = (Options & ERunOptions.NoHideWindow) == 0;
				if ((Options & ERunOptions.UTF8Output) == ERunOptions.UTF8Output)
				{
					Proc.StartInfo.StandardOutputEncoding = new System.Text.UTF8Encoding(false, false);
				}
				Proc.Start();

				if (bRedirectStdOut)
				{
					Proc.BeginOutputReadLine();
					Proc.BeginErrorReadLine();
				}

				if (String.IsNullOrEmpty(Input) == false)
				{
					Proc.StandardInput.WriteLine(Input);
					Proc.StandardInput.Close();
				}

				if (!Options.HasFlag(ERunOptions.NoWaitForExit))
				{
					Result.WaitForExit();
				}
				else
				{
					Result.ExitCode = -1;
				}
			}
			finally
			{
				if(PrevIndent != null)
				{
					Tools.DotNETCommon.Log.Indent = PrevIndent;
				}
			}

			if (!Options.HasFlag(ERunOptions.NoWaitForExit))
			{
				var BuildDuration = (DateTime.UtcNow - StartTime).TotalMilliseconds;
				//AddRunTime(App, (int)(BuildDuration));
				Result.ExitCode = Result.ProcessObject.ExitCode;
				if (!Options.HasFlag(ERunOptions.NoLoggingOfRunCommand) || Options.HasFlag(ERunOptions.LoggingOfRunDuration))
				{
					LogWithVerbosity(SpewVerbosity, "Took {0}s to run {1}, ExitCode={2}", BuildDuration / 1000, Path.GetFileName(App), Result.ExitCode);
				}
				Result.OnProcessExited();
				Result.DisposeProcess();
			}

			return Result;
		}

		/// <summary>
		/// Gets a logfile name for a RunAndLog call
		/// </summary>
		/// <param name="Env">Environment to use.</param>
		/// <param name="App">Executable to run</param>
		/// <param name="LogName">Name of the logfile ( if null, executable name is used )</param>
		/// <returns>The log file name.</returns>
		public static string GetRunAndLogOnlyName(CommandEnvironment Env, string App, string LogName = null)
        {
            if (LogName == null)
            {
                LogName = Path.GetFileNameWithoutExtension(App);
            }
            return LogUtils.GetUniqueLogName(CombinePaths(Env.LogFolder, LogName));
        }

		/// <summary>
		/// Runs external program and writes the output to a logfile.
		/// </summary>
		/// <param name="Env">Environment to use.</param>
		/// <param name="App">Executable to run</param>
		/// <param name="CommandLine">Commandline to pass on to the executable</param>
		/// <param name="LogName">Name of the logfile ( if null, executable name is used )</param>
		/// <param name="Input">Optional Input for the program (will be provided as stdin)</param>
		/// <param name="Options">Defines the options how to run. See ERunOptions.</param>
		/// <param name="FilterCallback">Callback to filter log spew before output.</param>
		public static void RunAndLog(CommandEnvironment Env, string App, string CommandLine, string LogName = null, int MaxSuccessCode = 0, string Input = null, ERunOptions Options = ERunOptions.Default, Dictionary<string, string> EnvVars = null, ProcessResult.SpewFilterCallbackType SpewFilterCallback = null)
		{
			RunAndLog(App, CommandLine, GetRunAndLogOnlyName(Env, App, LogName), MaxSuccessCode, Input, Options, EnvVars, SpewFilterCallback);
		}

		/// <summary>
		/// Exception class for child process commands failing
		/// </summary>
		public class CommandFailedException : AutomationException
		{
			public CommandFailedException(string Message) : base(Message)
			{
			}

			public CommandFailedException(ExitCode ExitCode, string Message) : base(ExitCode, Message)
			{
			}
		}

		/// <summary>
		/// Runs external program and writes the output to a logfile.
		/// </summary>
		/// <param name="App">Executable to run</param>
		/// <param name="CommandLine">Commandline to pass on to the executable</param>
		/// <param name="Logfile">Full path to the logfile, where the application output should be written to.</param>
		/// <param name="Input">Optional Input for the program (will be provided as stdin)</param>
		/// <param name="Options">Defines the options how to run. See ERunOptions.</param>
		/// <param name="FilterCallback">Callback to filter log spew before output.</param>
		public static string RunAndLog(string App, string CommandLine, string Logfile = null, int MaxSuccessCode = 0, string Input = null, ERunOptions Options = ERunOptions.Default, Dictionary<string, string> EnvVars = null, ProcessResult.SpewFilterCallbackType SpewFilterCallback = null)
        {
			IProcessResult Result = Run(App, CommandLine, Input, Options, EnvVars, SpewFilterCallback);
            if (!String.IsNullOrEmpty(Result.Output) && Logfile != null)
            {
                WriteToFile(Logfile, Result.Output);
            }
            else if (Logfile == null)
            {
                Logfile = "[No logfile specified]";
            }
            else
            {
                Logfile = "[None!, no output produced]";
            }

            if (Result.ExitCode > MaxSuccessCode || Result.ExitCode < 0)
            {
                throw new CommandFailedException((ExitCode)Result.ExitCode, String.Format("Command failed (Result:{3}): {0} {1}. See logfile for details: '{2}' ",
                                                App, CommandLine, Path.GetFileName(Logfile), Result.ExitCode));
            }
            if (!String.IsNullOrEmpty(Result.Output))
            {
                return Result.Output;
            }
            return "";
        }

		/// <summary>
		/// Runs external program and writes the output to a logfile.
		/// </summary>
		/// <param name="App">Executable to run</param>
		/// <param name="CommandLine">Commandline to pass on to the executable</param>
		/// <param name="Logfile">Full path to the logfile, where the application output should be written to.</param>
		/// <param name="FilterCallback">Callback to filter log spew before output.</param>
		/// <returns>Whether the program executed successfully or not.</returns>
		public static string RunAndLog(string App, string CommandLine, out int SuccessCode, string Logfile = null, Dictionary<string, string> EnvVars = null, ProcessResult.SpewFilterCallbackType SpewFilterCallback = null)
        {
			IProcessResult Result = Run(App, CommandLine, Env: EnvVars, SpewFilterCallback: SpewFilterCallback);
            SuccessCode = Result.ExitCode;
            if (Result.Output.Length > 0 && Logfile != null)
            {
                WriteToFile(Logfile, Result.Output);
            }
            if (!String.IsNullOrEmpty(Result.Output))
            {
                return Result.Output;
            }
            return "";
        }

		/// <summary>
		/// Runs external program and writes the output to a logfile.
		/// </summary>
		/// <param name="Env">Environment to use.</param>
		/// <param name="App">Executable to run</param>
		/// <param name="CommandLine">Commandline to pass on to the executable</param>
		/// <param name="LogName">Name of the logfile ( if null, executable name is used )</param>
		/// <param name="FilterCallback">Callback to filter log spew before output.</param>
		/// <returns>Whether the program executed successfully or not.</returns>
		public static string RunAndLog(CommandEnvironment Env, string App, string CommandLine, out int SuccessCode, string LogName = null, Dictionary<string, string> EnvVars = null, ProcessResult.SpewFilterCallbackType SpewFilterCallback = null)
        {
			return RunAndLog(App, CommandLine, out SuccessCode, GetRunAndLogOnlyName(Env, App, LogName), EnvVars, SpewFilterCallback);
        }

		/// <summary>
		/// Runs UAT recursively
		/// </summary>
		/// <param name="Env">Environment to use.</param>
		/// <param name="CommandLine">Commandline to pass on to the executable</param>
		/// <param name="Identifier">Log prefix for output</param>
		public static void RunUAT(CommandEnvironment Env, string CommandLine, string Identifier)
		{
			// Check if there are already log files which start with this prefix, and try to uniquify it if until there aren't.
			string DirOnlyName = Identifier;
			string LogSubdir = CombinePaths(CmdEnv.LogFolder, DirOnlyName, "");
			for(int Attempt = 1;;Attempt++)
			{
                string[] ExistingFiles = FindFiles(DirOnlyName + "*", false, CmdEnv.LogFolder);
                if (ExistingFiles.Length == 0)
                {
                    break;
                }
				if (Attempt == 1000)
				{
					throw new AutomationException("Couldn't seem to create a log subdir {0}", LogSubdir);
				}
				DirOnlyName = String.Format("{0}_{1}", Identifier, Attempt + 1);
				LogSubdir = CombinePaths(CmdEnv.LogFolder, DirOnlyName, "");
			}

			// Get the stdout log file for this run, and create the subdirectory for all the other log output
			CreateDirectory(LogSubdir);

			// Run UAT with the log folder redirected through the environment
			Dictionary<string, string> EnvironmentVars = new Dictionary<string, string>();
			EnvironmentVars.Add(EnvVarNames.LogFolder, LogSubdir);
			EnvironmentVars.Add(EnvVarNames.FinalLogFolder, CombinePaths(CmdEnv.FinalLogFolder, DirOnlyName));
			EnvironmentVars.Add(EnvVarNames.DisableStartupMutex, "1");
			EnvironmentVars.Add(EnvVarNames.IsChildInstance, "1");
			if (!IsBuildMachine)
			{
				EnvironmentVars.Add(AutomationTool.EnvVarNames.LocalRoot, ""); // if we don't clear this out, it will think it is a build machine; it will rederive everything
			}

			IProcessResult Result = Run(CmdEnv.UATExe, CommandLine, null, ERunOptions.Default, EnvironmentVars, Identifier: Identifier);
			if (Result.ExitCode != 0)
			{
				throw new CommandFailedException(String.Format("Recursive UAT command failed (exit code {0})", Result.ExitCode));
			}
		}

		protected delegate bool ProcessLog(string LogText);
		/// <summary>
		/// Keeps reading a log file as it's being written to by another process until it exits.
		/// </summary>
		/// <param name="LogFilename">Name of the log file.</param>
		/// <param name="LogProcess">Process that writes to the log file.</param>
		/// <param name="OnLogRead">Callback used to process the recently read log contents.</param>
		protected static void LogFileReaderProcess(string LogFilename, IProcessResult LogProcess, ProcessLog OnLogRead = null)
		{
			while (!FileExists(LogFilename) && !LogProcess.HasExited)
			{
				LogInformation("Waiting for logging process to start...");
				Thread.Sleep(2000);
			}
			Thread.Sleep(1000);

			using (FileStream ProcessLog = File.Open(LogFilename, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
			{
				StreamReader LogReader = new StreamReader(ProcessLog);
				bool bKeepReading = true;

				// Read until the process has exited.
				while (!LogProcess.HasExited && bKeepReading)
				{
					while (!LogReader.EndOfStream && bKeepReading)
					{
						string Output = LogReader.ReadToEnd();
						if (Output != null && OnLogRead != null)
						{
							bKeepReading = OnLogRead(Output);
						}
					}

					while (LogReader.EndOfStream && !LogProcess.HasExited && bKeepReading)
					{
						Thread.Sleep(250);
						// Tick the callback so that it can respond to external events
						if (OnLogRead != null)
						{
							bKeepReading = OnLogRead(null);
						}
					}
				}
			}
		}

	}
}
