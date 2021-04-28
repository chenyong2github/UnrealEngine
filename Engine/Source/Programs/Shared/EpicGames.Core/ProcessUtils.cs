// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using Microsoft.Win32.SafeHandles;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Includes utilities for managing processes
	/// </summary>
	public static class ProcessUtils
	{
		const uint PROCESS_TERMINATE = 0x0001;
		const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;

		[DllImport("kernel32.dll", SetLastError = true)]
		static extern SafeProcessHandle OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint processId);

		[DllImport("kernel32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool TerminateProcess(SafeProcessHandle hProcess, uint uExitCode);

		[DllImport("kernel32.dll", SetLastError = true)]
		static extern int QueryFullProcessImageName([In]SafeProcessHandle hProcess, [In]int dwFlags, [Out]StringBuilder lpExeName, ref int lpdwSize);

		[DllImport("Psapi.dll", SetLastError = true)]
		static extern bool EnumProcesses([MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.U4)] [In][Out] uint[] processIds, int arraySizeBytes, [MarshalAs(UnmanagedType.U4)] out int bytesCopied);

		[DllImport("kernel32.dll")]
		static extern uint WaitForMultipleObjects(int nCount, IntPtr[] lpHandles, bool bWaitAll, uint dwMilliseconds);

		/// <summary>
		/// Attempts to terminate all processes matching a predicate
		/// </summary>
		/// <param name="Predicate">The predicate for whether to terminate a process</param>
		/// <param name="Logger">Logging device</param>
		/// <param name="CancellationToken">Cancellation token to abort the search</param>
		/// <returns>True if the process succeeded</returns>
		public static bool TerminateProcesses(Predicate<FileReference> Predicate, ILogger Logger, CancellationToken CancellationToken)
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				return TerminateProcessesWin32(Predicate, Logger, CancellationToken);
			}
			else
			{
				return TerminateProcessesGenericPlatform(Predicate, Logger, CancellationToken);
			}
		}

		/// <summary>
		/// Attempts to terminate all processes matching a predicate
		/// </summary>
		/// <param name="Predicate">The predicate for whether to terminate a process</param>
		/// <param name="Logger">Logging device</param>
		/// <param name="CancellationToken">Cancellation token to abort the search</param>
		/// <returns>True if the process succeeded</returns>
		static bool TerminateProcessesWin32(Predicate<FileReference> Predicate, ILogger Logger, CancellationToken CancellationToken)
		{
			Dictionary<int, int> ProcessToCount = new Dictionary<int, int>();
			for(; ;)
			{
				CancellationToken.ThrowIfCancellationRequested();

				// Enumerate the processes
				int NumProcessIds;
				uint[] ProcessIds = new uint[512];
				for (; ; )
				{
					int MaxBytes = ProcessIds.Length * sizeof(uint);
					int NumBytes = 0;
					if (!EnumProcesses(ProcessIds, MaxBytes, out NumBytes))
					{
						throw new Win32ExceptionWithCode("Unable to enumerate processes");
					}
					if (NumBytes < MaxBytes)
					{
						NumProcessIds = NumBytes / sizeof(uint);
						break;
					}
					ProcessIds = new uint[ProcessIds.Length + 256];
				}

				// Find the processes to terminate
				List<SafeProcessHandle> WaitHandles = new List<SafeProcessHandle>();
				try
				{
					// Open each process in turn
					foreach (uint ProcessId in ProcessIds)
					{
						SafeProcessHandle Handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, false, ProcessId);
						try
						{
							if (!Handle.IsInvalid)
							{
								int Characters = 260;
								StringBuilder Buffer = new StringBuilder(Characters);
								if (QueryFullProcessImageName(Handle, 0, Buffer, ref Characters) > 0)
								{
									FileReference ImageFile = new FileReference(Buffer.ToString(0, Characters));
									if (Predicate(ImageFile))
									{
										Logger.LogInformation("Terminating {ImageName} ({ProcessId})", ImageFile, ProcessId);
										if (TerminateProcess(Handle, 9))
										{
											WaitHandles.Add(Handle);
										}
										else
										{
											Logger.LogInformation("Failed call to TerminateProcess ({Code})", Marshal.GetLastWin32Error());
										}
										Handle.SetHandleAsInvalid();
									}
								}
							}
						}
						finally
						{
							Handle.Dispose();
						}
					}

					// If there's nothing to do, exit immediately
					if (WaitHandles.Count == 0)
					{
						return true;
					}

					// Wait for them all to complete
					WaitForMultipleObjects(WaitHandles.Count, WaitHandles.Select(x => x.DangerousGetHandle()).ToArray(), true, 10 * 1000);
				}
				finally
				{
					foreach (SafeProcessHandle WaitHandle in WaitHandles)
					{
						WaitHandle.Close();
					}
				}
			}
		}

		/// <summary>
		/// Attempts to terminate all processes matching a predicate
		/// </summary>
		/// <param name="Predicate">The predicate for whether to terminate a process</param>
		/// <param name="Logger">Logging device</param>
		/// <param name="CancellationToken">Cancellation token to abort the search</param>
		/// <returns>True if the process succeeded</returns>
		static bool TerminateProcessesGenericPlatform(Predicate<FileReference> Predicate, ILogger Logger, CancellationToken CancellationToken)
		{
			bool Result = true;
			Dictionary<(int, DateTime), int> ProcessToCount = new Dictionary<(int, DateTime), int>();
			for (; ; )
			{
				CancellationToken.ThrowIfCancellationRequested();

				bool bNoMatches = true;

				// Enumerate all the processes
				Process[] Processes = Process.GetProcesses();
				foreach (Process Process in Processes)
				{
					// Attempt to get the image file. Ignore exceptions trying to fetch metadata for processes we don't have access to.
					FileReference? ImageFile;
					try
					{
						ImageFile = new FileReference(Process.MainModule.FileName);
					}
					catch
					{
						ImageFile = null;
					}

					// Test whether to terminate this process
					if (ImageFile != null && Predicate(ImageFile))
					{
						// Get a unique id for the process, given that process ids are recycled
						(int, DateTime) UniqueId;
						try
						{
							UniqueId = (Process.Id, Process.StartTime);
						}
						catch
						{
							UniqueId = (Process.Id, DateTime.MinValue);
						}

						// Figure out whether to try and terminate this process
						const int MaxCount = 5;
						if (!ProcessToCount.TryGetValue(UniqueId, out int Count) || Count < MaxCount)
						{
							bNoMatches = false;
							try
							{
								Logger.LogInformation("Terminating {ImageName} ({ProcessId})", ImageFile, Process.Id);
								Process.Kill(true);
								if (!Process.WaitForExit(5 * 1000))
								{
									Logger.LogInformation("Termination still pending; will retry...");
								}
							}
							catch (Exception Ex)
							{
								Count++;
								if (Count > 1)
								{
									Logger.LogInformation(Ex, "Exception while querying basic process info for pid {ProcessId}; will retry.", Process.Id);
								}
								else if (Count == MaxCount)
								{
									Logger.LogWarning(Ex, "Unable to terminate process {ImageFile} ({ProcessId}): {Message}", ImageFile, Process.Id, Ex.Message);
									Result = false;
								}
								ProcessToCount[UniqueId] = Count;
							}
						}
					}
				}

				// Return once we reach this point and haven't found anything else to terminate
				if (bNoMatches)
				{
					return Result;
				}
			}
		}
	}
}
