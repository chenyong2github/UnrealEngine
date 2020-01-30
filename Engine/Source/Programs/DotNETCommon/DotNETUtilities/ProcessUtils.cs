// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Win32.SafeHandles;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Tools.DotNETCommon
{
	/// <summary>
	/// Includes utilities for managing processes
	/// </summary>
	public static class ProcessUtils
	{
		[StructLayout(LayoutKind.Sequential)]
		struct PROCESSENTRY32
		{
			public uint dwSize;
			public uint cntUsage;
			public uint th32ProcessID;
			public IntPtr th32DefaultHeapID;
			public uint th32ModuleID;
			public uint cntThreads;
			public uint th32ParentProcessID;
			public int pcPriClassBase;
			public uint dwFlags;
			[MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
			public string szExeFile;
		};

		static uint TH32CS_SNAPPROCESS = 2;

		[DllImport("kernel32.dll", SetLastError = true)]
		static extern SafeFileHandle CreateToolhelp32Snapshot(uint dwFlags, uint th32ProcessID);

		[DllImport("kernel32.dll")]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool Process32First(SafeFileHandle hSnapshot, ref PROCESSENTRY32 lppe);

		[DllImport("kernel32.dll")]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool Process32Next(SafeFileHandle hSnapshot, ref PROCESSENTRY32 lppe);

		const uint PROCESS_TERMINATE = 0x0001;

		[DllImport("kernel32.dll", SetLastError = true)]
		static extern SafeProcessHandle OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint processId);

		[DllImport("kernel32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool TerminateProcess(SafeProcessHandle hProcess, uint uExitCode);

		/// <summary>
		/// Terminates all child processes of the current process
		/// </summary>
		public static void TerminateChildProcesses()
		{
			using (Process CurrentProcess = Process.GetCurrentProcess())
			{
				if (Environment.OSVersion.Platform == PlatformID.Win32NT)
				{
					TerminateChildProcessesWin32(CurrentProcess.Id);
				}
				else
				{
					TerminateChildProcessesPosix(CurrentProcess.Id);
				}
			}
		}

		/// <summary>
		/// Win32-specific implementation of TerminateChildProcesses
		/// </summary>
		/// <param name="RootProcessId">The root process id of the tree to be terminated</param>
		static void TerminateChildProcessesWin32(int RootProcessId)
		{
			Stopwatch Timer = Stopwatch.StartNew();
			HashSet<uint> UnableToOpenProcessIds = new HashSet<uint>();
			for (; ; )
			{
				// Find a map of process id to parent process id
				Dictionary<uint, uint> ProcessIdToParentProcessId = new Dictionary<uint, uint>();

				using (SafeFileHandle SnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0))
				{
					if (!SnapshotHandle.IsInvalid)
					{
						PROCESSENTRY32 ProcessEntry = new PROCESSENTRY32();
						ProcessEntry.dwSize = (uint)Marshal.SizeOf(typeof(PROCESSENTRY32));

						for (bool bResult = Process32First(SnapshotHandle, ref ProcessEntry); bResult; bResult = Process32Next(SnapshotHandle, ref ProcessEntry))
						{
							if (ProcessEntry.th32ParentProcessID != 0)
							{
								ProcessIdToParentProcessId[ProcessEntry.th32ProcessID] = ProcessEntry.th32ParentProcessID;
							}
						}
					}
				}

				// Find any process ids which are a descendant from the root process it
				HashSet<uint> ChildProcessIds = new HashSet<uint>();
				for (bool bContinueLoop = true; bContinueLoop; )
				{
					bContinueLoop = false;
					foreach (KeyValuePair<uint, uint> Pair in ProcessIdToParentProcessId)
					{
						if (Pair.Value == RootProcessId || ChildProcessIds.Contains(Pair.Value))
						{
							bContinueLoop |= ChildProcessIds.Add(Pair.Key);
						}
					}
				}

				// If there's nothing running, we can quit
				if (!ChildProcessIds.Any())
				{
					break;
				}

				// Check if we need to print a message about what's going on
				bool bPrintStatus = false;
				if (Timer == null)
				{
					Timer = Stopwatch.StartNew();
					bPrintStatus = true;
				}
				else if (Timer.Elapsed > TimeSpan.FromSeconds(10.0))
				{
					Timer.Restart();
					bPrintStatus = true;
				}

				// Try to kill all the child processes
				bool bWaitingForProcess = false;
				foreach (uint ChildProcessId in ChildProcessIds)
				{
					using (SafeProcessHandle ProcessHandle = OpenProcess(PROCESS_TERMINATE, false, ChildProcessId))
					{
						if (ProcessHandle.IsInvalid)
						{
							if (UnableToOpenProcessIds.Add(ChildProcessId))
							{
								bWaitingForProcess = true;
							}
							else
							{
								Log.TraceWarning("Unable to terminate process {0}.", ChildProcessId);
							}
						}
						else
						{
							if (bPrintStatus)
							{
								Log.TraceInformation("Waiting for process {0} to terminate.", ChildProcessId);
							}

							TerminateProcess(ProcessHandle, 3);
							bWaitingForProcess = true;
						}
					}
				}

				// If there wasn't anything we could do, bail out
				if(!bWaitingForProcess)
				{
					break;
				}

				// Allow a short amount of time for processes to exit, then check again
				Thread.Sleep(500);
			}
		}

		/// <summary>
		/// Posix implementation of TerminateChildProcesses
		/// </summary>
		/// <param name="RootProcessId">The root process id of the tree to be terminated</param>
		static void TerminateChildProcessesPosix(int RootProcessId)
		{
			using (Process Process = Process.Start("pkill", String.Format("-9 -p {0}", RootProcessId)))
			{
				Process.WaitForExit();
			}
		}
	}
}
