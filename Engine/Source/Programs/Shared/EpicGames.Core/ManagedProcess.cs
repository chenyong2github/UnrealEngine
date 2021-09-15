// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Win32.SafeHandles;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Flags for the managed process
	/// </summary>
	[Flags]
	public enum ManagedProcessFlags
	{
		/// <summary>
		/// No flags 
		/// </summary>
		None = 0,

		/// <summary>
		/// Merge stdout and stderr
		/// </summary>
		MergeOutputPipes = 1,
	}

	/// <summary>
	/// Tracks a set of processes, and destroys them when the object is disposed.
	/// </summary>
	public class ManagedProcessGroup : IDisposable
	{
		const UInt32 JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000;
		const UInt32 JOB_OBJECT_LIMIT_BREAKAWAY_OK = 0x00000800;

		[StructLayout(LayoutKind.Sequential)]
		struct JOBOBJECT_BASIC_LIMIT_INFORMATION
		{
			public Int64 PerProcessUserTimeLimit;
			public Int64 PerJobUserTimeLimit;
			public UInt32 LimitFlags;
			public UIntPtr MinimumWorkingSetSize;
			public UIntPtr MaximumWorkingSetSize;
			public UInt32 ActiveProcessLimit;
			public Int64 Affinity;
			public UInt32 PriorityClass;
			public UInt32 SchedulingClass;
		}

		[StructLayout(LayoutKind.Sequential)]
		struct IO_COUNTERS
		{
			public UInt64 ReadOperationCount;
			public UInt64 WriteOperationCount;
			public UInt64 OtherOperationCount;
			public UInt64 ReadTransferCount;
			public UInt64 WriteTransferCount;
			public UInt64 OtherTransferCount;
		}

		[StructLayout(LayoutKind.Sequential)]
		struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
		{
			public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
			public IO_COUNTERS IoInfo;
			public UIntPtr ProcessMemoryLimit;
			public UIntPtr JobMemoryLimit;
			public UIntPtr PeakProcessMemoryUsed;
			public UIntPtr PeakJobMemoryUsed;
		}
		
		[StructLayout(LayoutKind.Sequential)]
		struct JOBOBJECT_BASIC_ACCOUNTING_INFORMATION
		{
			public UInt64 TotalUserTime;
			public UInt64 TotalKernelTime;
			public UInt64 ThisPeriodTotalUserTime;
			public UInt64 ThisPeriodTotalKernelTime;
			public UInt32 TotalPageFaultCount;
			public UInt32 TotalProcesses;
			public UInt32 ActiveProcesses;
			public UInt32 TotalTerminatedProcesses;
		}

		[DllImport("kernel32.dll", SetLastError=true)]
		static extern SafeFileHandle CreateJobObject(IntPtr SecurityAttributes, IntPtr Name);

		const int JobObjectBasicAccountingInformation = 1;
		const int JobObjectExtendedLimitInformation = 9;

        [DllImport("kernel32.dll", SetLastError=true)]
        static extern int SetInformationJobObject(SafeFileHandle hJob, int JobObjectInfoClass, IntPtr lpJobObjectInfo, int cbJobObjectInfoLength);
		
		[DllImport("kernel32.dll", SetLastError = true)]
		static extern bool QueryInformationJobObject(SafeFileHandle hJob, int JobObjectInformationClass, ref JOBOBJECT_BASIC_ACCOUNTING_INFORMATION lpJobObjectInformation, int cbJobObjectInformationLength);

		/// <summary>
		/// Handle to the native job object that this process is added to. This handle is closed by the Dispose() method (and will automatically be closed by the OS on process exit),
		/// resulting in the child process being killed.
		/// </summary>
		internal SafeFileHandle? JobHandle
		{
			get;
			private set;
		}

		/// <summary>
		/// Determines support for using job objects
		/// </summary>
		static internal bool SupportsJobObjects
		{
			get { return RuntimePlatform.IsWindows; }
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public ManagedProcessGroup()
		{
			if(SupportsJobObjects)
			{
				// Create the job object that the child process will be added to
				JobHandle = CreateJobObject(IntPtr.Zero, IntPtr.Zero);
				if(JobHandle == null)
				{
					throw new Win32Exception();
				}

				// Configure the job object to terminate the processes added to it when the handle is closed
				JOBOBJECT_EXTENDED_LIMIT_INFORMATION LimitInformation = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
				LimitInformation.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;

				int Length = Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
				IntPtr LimitInformationPtr = Marshal.AllocHGlobal(Length);
				Marshal.StructureToPtr(LimitInformation, LimitInformationPtr, false);

				if(SetInformationJobObject(JobHandle, JobObjectExtendedLimitInformation, LimitInformationPtr, Length) == 0)
				{
					throw new Win32Exception();
				}
			}
		}

		/// <summary>
		/// Returns the total CPU time usage for this job.
		/// </summary>
		public TimeSpan TotalProcessorTime
		{
			get
			{
				if (SupportsJobObjects)
				{
					JOBOBJECT_BASIC_ACCOUNTING_INFORMATION AccountingInformation = new JOBOBJECT_BASIC_ACCOUNTING_INFORMATION();
					if (QueryInformationJobObject(JobHandle!, JobObjectBasicAccountingInformation, ref AccountingInformation, Marshal.SizeOf(typeof(JOBOBJECT_BASIC_ACCOUNTING_INFORMATION))) == false)
					{
						throw new Win32Exception();
					}

					return new TimeSpan((long)AccountingInformation.TotalUserTime + (long)AccountingInformation.TotalKernelTime);
				}
				else
				{
					return new TimeSpan();
				}
			}
		}

		public void Add(IntPtr ProcessHandle)
		{
		}

		/// <summary>
		/// Dispose of the process group
		/// </summary>
		public void Dispose()
		{
			if(JobHandle != null)
			{
				JobHandle.Dispose();
				JobHandle = null;
			}
		}
	}

	/// <summary>
	/// Encapsulates a managed child process, from which we can read the console output.
	/// Uses job objects to ensure that the process will be terminated automatically by the O/S if the current process is terminated, and polls pipe reads to avoid blocking on unmanaged code
	/// if the calling thread is terminated. Currently only implemented for Windows; makes heavy use of P/Invoke.
	/// </summary>
	public sealed class ManagedProcess : IDisposable
	{
		[StructLayout(LayoutKind.Sequential)]
		class SECURITY_ATTRIBUTES
		{
			public int nLength;
			public IntPtr lpSecurityDescriptor;
			public int bInheritHandle;
		}

		[StructLayout(LayoutKind.Sequential)]
		class PROCESS_INFORMATION
		{
			public IntPtr hProcess;
			public IntPtr hThread;
			public uint dwProcessId;
			public uint dwThreadId;
		}

		[StructLayout(LayoutKind.Sequential)]
		class STARTUPINFO
		{
			public int cb;
			public string? lpReserved;
			public string? lpDesktop;
			public string? lpTitle;
			public uint dwX;
			public uint dwY;
			public uint dwXSize;
			public uint dwYSize;
			public uint dwXCountChars;
			public uint dwYCountChars;
			public uint dwFillAttribute;
			public uint dwFlags;
			public short wShowWindow;
			public short cbReserved2;
			public IntPtr lpReserved2;
			public SafeHandle? hStdInput;
			public SafeHandle? hStdOutput;
			public SafeHandle? hStdError;
		}

		[DllImport("kernel32.dll", SetLastError=true)]
		static extern int AssignProcessToJobObject(SafeFileHandle? hJob, IntPtr hProcess);

		[DllImport("kernel32.dll", SetLastError=true)]
		static extern int CloseHandle(IntPtr hObject);

		[DllImport("kernel32.dll", SetLastError = true)]
		static extern int CreatePipe(out SafeFileHandle hReadPipe, out SafeFileHandle hWritePipe, SECURITY_ATTRIBUTES lpPipeAttributes, uint nSize);

		[DllImport("kernel32.dll", SetLastError = true)]
		static extern int SetHandleInformation(SafeFileHandle hObject, int dwMask, int dwFlags);

		const int HANDLE_FLAG_INHERIT = 1;

        const int STARTF_USESTDHANDLES = 0x00000100;

		[Flags]
		enum ProcessCreationFlags : int
		{
			CREATE_NO_WINDOW = 0x08000000,
			CREATE_SUSPENDED = 0x00000004,
			NORMAL_PRIORITY_CLASS = 0x00000020,
			IDLE_PRIORITY_CLASS = 0x00000040,
			HIGH_PRIORITY_CLASS = 0x00000080,
			REALTIME_PRIORITY_CLASS = 0x00000100,
			BELOW_NORMAL_PRIORITY_CLASS = 0x00004000,
			ABOVE_NORMAL_PRIORITY_CLASS = 0x00008000,
		}

        [DllImport("kernel32.dll", SetLastError=true)]
        static extern int CreateProcess(/*[MarshalAs(UnmanagedType.LPTStr)]*/ string? lpApplicationName, StringBuilder lpCommandLine, IntPtr lpProcessAttributes, IntPtr lpThreadAttributes, bool bInheritHandles, ProcessCreationFlags dwCreationFlags, IntPtr lpEnvironment, /*[MarshalAs(UnmanagedType.LPTStr)]*/ string? lpCurrentDirectory, STARTUPINFO lpStartupInfo, PROCESS_INFORMATION lpProcessInformation);

		[DllImport("kernel32.dll", SetLastError=true)]
		static extern int ResumeThread(IntPtr hThread);

		[DllImport("kernel32.dll", SetLastError=true)]
		static extern IntPtr GetCurrentProcess();

        const int DUPLICATE_SAME_ACCESS = 2;

        [DllImport("kernel32.dll", SetLastError=true)]    
        static extern int DuplicateHandle(
            IntPtr hSourceProcessHandle,
            SafeHandle hSourceHandle,
            IntPtr hTargetProcess,
            out SafeFileHandle targetHandle,
            int dwDesiredAccess,
            [MarshalAs(UnmanagedType.Bool)] bool bInheritHandle,
            int dwOptions
        );

		[DllImport("kernel32.dll", SetLastError = true)]
		static extern int GetExitCodeProcess(SafeFileHandle hProcess, out int lpExitCode);

		[DllImport ("kernel32.dll")]
		static extern bool IsProcessInJob(IntPtr hProcess, IntPtr hJob, out bool Result);

		[DllImport("kernel32.dll", SetLastError=true)]
		static extern int TerminateProcess(SafeHandleZeroOrMinusOneIsInvalid hProcess, uint uExitCode);

		const UInt32 INFINITE = 0xFFFFFFFF;

		[DllImport("kernel32.dll", SetLastError=true)]
		static extern UInt32 WaitForSingleObject(SafeHandleZeroOrMinusOneIsInvalid hHandle, UInt32 dwMilliseconds);

		[DllImport("kernel32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool GetProcessTimes(SafeHandleZeroOrMinusOneIsInvalid hProcess,
			out System.Runtime.InteropServices.ComTypes.FILETIME lpCreationTime,
			out System.Runtime.InteropServices.ComTypes.FILETIME lpExitTime,
			out System.Runtime.InteropServices.ComTypes.FILETIME lpKernelTime,
			out System.Runtime.InteropServices.ComTypes.FILETIME lpUserTime);

		[DllImport("kernel32.dll")]
		static extern UInt16 GetActiveProcessorGroupCount();

		[DllImport("kernel32.dll")]
		static extern UInt32 GetActiveProcessorCount(UInt16 GroupNumber);
		
		[StructLayout(LayoutKind.Sequential)]
		class GROUP_AFFINITY
		{
			public UInt64 Mask;
			public UInt16 Group;
			public UInt16 Reserved0;
			public UInt16 Reserved1;
			public UInt16 Reserved2;
		}

		[DllImport("kernel32.dll", SetLastError = true)]
		static extern int SetThreadGroupAffinity(IntPtr hThread, GROUP_AFFINITY GroupAffinity, GROUP_AFFINITY? PreviousGroupAffinity);

		/// <summary>
		/// Converts FILETIME to DateTime.
		/// </summary>
		/// <param name="Time">Input FILETIME structure</param>
		/// <returns>Converted DateTime</returns>
		static DateTime FileTimeToDateTime(System.Runtime.InteropServices.ComTypes.FILETIME Time)
		{
			ulong High = (ulong)Time.dwHighDateTime;
			uint Low = (uint)Time.dwLowDateTime;
			long FileTime = (long)((High << 32) + Low);
			try
			{
				return DateTime.FromFileTimeUtc(FileTime);
			}
			catch (ArgumentOutOfRangeException)
			{
				return DateTime.MinValue;
			}
		}

		const int ERROR_ACCESS_DENIED = 5;

		/// <summary>
		/// The process id
		/// </summary>
		public int Id { get; private set; }

		/// <summary>
		/// Handle for the child process.
		/// </summary>
		SafeFileHandle? ProcessHandle;

		/// <summary>
		/// The write end of the child process' stdin pipe.
		/// </summary>
		SafeFileHandle? StdInWrite;

		/// <summary>
		/// The read end of the child process' stdout pipe.
		/// </summary>
		SafeFileHandle? StdOutRead;

		/// <summary>
		/// The read end of the child process' stderr pipe.
		/// </summary>
		SafeFileHandle? StdErrRead;

		/// <summary>
		/// Input stream for the child process.
		/// </summary>
		public Stream StdIn { get; private set; } = null!;

		/// <summary>
		/// Output stream for the child process.
		/// </summary>
		public Stream StdOut { get; private set; } = null!;

		/// <summary>
		/// Reader for the process' output stream.
		/// </summary>
		public StreamReader StdOutText { get; private set; } = null!;

		/// <summary>
		/// Output stream for the child process.
		/// </summary>
		public Stream StdErr { get; private set; } = null!;

		/// <summary>
		/// Reader for the process' output stream.
		/// </summary>
		public StreamReader StdErrText { get; private set; } = null!;

		/// <summary>
		/// Standard process implementation for non-Windows platforms.
		/// </summary>
		Process? FrameworkProcess;

		/// <summary>
		/// Thread to read from stdout
		/// </summary>
		Thread? FrameworkStdOutThread;

		/// <summary>
		/// Thread to read from stderr
		/// </summary>
		Thread? FrameworkStdErrThread;

		/// <summary>
		/// Merged output & error write stream for the framework child process.
		/// </summary>
		AnonymousPipeServerStream? FrameworkMergedStdWriter;

		/// <summary>
		/// Tracks how many pipes have been copied to the merged writer.
		/// </summary>
		int FrameworkMergedStdWriterThreadCount;

		/// <summary>
		/// Static lock object. This is used to synchronize the creation of child processes - in particular, the inheritance of stdout/stderr write pipes. If processes
		/// inherit pipes meant for other processes, they won't be closed until both terminate.
		/// </summary>
		static object LockObject = new object();

		/// <summary>
		/// Used to perform CPU usage and resource accounting of all children process involved in a single compilation unit.
		/// </summary>
		ManagedProcessGroup? AccountingProcessGroup;

		/// <summary>
		/// Spawns a new managed process.
		/// </summary>
		/// <param name="Group">The managed process group to add to</param>
		/// <param name="FileName">Path to the executable to be run</param>
		/// <param name="CommandLine">Command line arguments for the process</param>
		/// <param name="WorkingDirectory">Working directory for the new process. May be null to use the current working directory.</param>
		/// <param name="Environment">Environment variables for the new process. May be null, in which case the current process' environment is inherited</param>
		/// <param name="Input">Text to be passed via stdin to the new process. May be null.</param>
		/// <param name="Priority">Priority for the child process</param>
		public ManagedProcess(ManagedProcessGroup? Group, string FileName, string CommandLine, string? WorkingDirectory, IReadOnlyDictionary<string, string>? Environment, byte[]? Input, ProcessPriorityClass Priority, ManagedProcessFlags Flags = ManagedProcessFlags.MergeOutputPipes)
			: this(Group, FileName, CommandLine, WorkingDirectory, Environment, Priority, Flags)
		{
			if (Input != null)
			{
				StdIn.Write(Input, 0, Input.Length);
			}
			StdIn.Close();
		}

		/// <summary>
		/// Spawns a new managed process.
		/// </summary>
		/// <param name="Group">The managed process group to add to</param>
		/// <param name="FileName">Path to the executable to be run</param>
		/// <param name="CommandLine">Command line arguments for the process</param>
		/// <param name="WorkingDirectory">Working directory for the new process. May be null to use the current working directory.</param>
		/// <param name="Environment">Environment variables for the new process. May be null, in which case the current process' environment is inherited</param>
		/// <param name="Priority">Priority for the child process</param>
		public ManagedProcess(ManagedProcessGroup? Group, string FileName, string CommandLine, string? WorkingDirectory, IReadOnlyDictionary<string, string>? Environment, ProcessPriorityClass Priority, ManagedProcessFlags Flags = ManagedProcessFlags.MergeOutputPipes)
		{
			// Create the child process
			// NOTE: Child process must be created in a separate method to avoid stomping exception callstacks (https://stackoverflow.com/a/2494150)
			try
			{
				if (ManagedProcessGroup.SupportsJobObjects)
				{
					CreateManagedProcessWin32(Group, FileName, CommandLine, WorkingDirectory, Environment, Priority, Flags);
				}
				else
				{
					CreateManagedProcessPortable(Group, FileName, CommandLine, WorkingDirectory, Environment, Priority, Flags);
				}
			}
			catch (Exception Ex)
			{
				ExceptionUtils.AddContext(Ex, "while launching {0} {1}", FileName, CommandLine);
				throw;
			}
		}

		static readonly int ProcessorGroupCount = GetActiveProcessorGroupCount();

		/// <summary>
		/// A counter used to decide which processor group the next process should be assigned to
		/// </summary>
		static int ProcessorGroupCounter = 0;

		/// <summary>
		/// Spawns a new managed process using Win32 native functions. 
		/// </summary>
		/// <param name="Group">The managed process group to add to</param>
		/// <param name="FileName">Path to the executable to be run</param>
		/// <param name="CommandLine">Command line arguments for the process</param>
		/// <param name="WorkingDirectory">Working directory for the new process. May be null to use the current working directory.</param>
		/// <param name="Environment">Environment variables for the new process. May be null, in which case the current process' environment is inherited</param>
		/// <param name="Priority">Priority for the child process</param>
		/// <param name="ManagedFlags">Flags controlling how the new process is created</param>
		private void CreateManagedProcessWin32(ManagedProcessGroup? Group, string FileName, string CommandLine, string? WorkingDirectory, IReadOnlyDictionary<string, string>? Environment, ProcessPriorityClass Priority, ManagedProcessFlags ManagedFlags)
		{
			IntPtr EnvironmentBlock = IntPtr.Zero;
			try
			{
				// Create the environment block for the child process, if necessary.
				if (Environment != null)
				{
					// The native format for the environment block is a sequence of null terminated strings with a final null terminator.
					List<byte> EnvironmentBytes = new List<byte>();
					foreach (KeyValuePair<string, string> Pair in Environment)
					{
						EnvironmentBytes.AddRange(Console.OutputEncoding.GetBytes(Pair.Key));
						EnvironmentBytes.Add((byte)'=');
						EnvironmentBytes.AddRange(Console.OutputEncoding.GetBytes(Pair.Value));
						EnvironmentBytes.Add((byte)0);
					}
					EnvironmentBytes.Add((byte)0);

					// Allocate an unmanaged block of memory to store it.
					EnvironmentBlock = Marshal.AllocHGlobal(EnvironmentBytes.Count);
					Marshal.Copy(EnvironmentBytes.ToArray(), 0, EnvironmentBlock, EnvironmentBytes.Count);
				}

				PROCESS_INFORMATION ProcessInfo = new PROCESS_INFORMATION();
				try
				{
					// Get the flags to create the new process
					ProcessCreationFlags Flags = ProcessCreationFlags.CREATE_NO_WINDOW | ProcessCreationFlags.CREATE_SUSPENDED;
					switch (Priority)
					{
						case ProcessPriorityClass.Normal:
							Flags |= ProcessCreationFlags.NORMAL_PRIORITY_CLASS;
							break;
						case ProcessPriorityClass.Idle:
							Flags |= ProcessCreationFlags.IDLE_PRIORITY_CLASS;
							break;
						case ProcessPriorityClass.High:
							Flags |= ProcessCreationFlags.HIGH_PRIORITY_CLASS;
							break;
						case ProcessPriorityClass.RealTime:
							Flags |= ProcessCreationFlags.REALTIME_PRIORITY_CLASS;
							break;
						case ProcessPriorityClass.BelowNormal:
							Flags |= ProcessCreationFlags.BELOW_NORMAL_PRIORITY_CLASS;
							break;
						case ProcessPriorityClass.AboveNormal:
							Flags |= ProcessCreationFlags.ABOVE_NORMAL_PRIORITY_CLASS;
							break;
					}

					// Acquire a global lock before creating inheritable handles. If multiple threads create inheritable handles at the same time, and child processes will inherit them all.
					// Since we need to wait for output pipes to be closed (in order to consume all output), this can result in output reads not returning until all processes with the same
					// inherited handles are closed.
					lock (LockObject)
					{
						SafeFileHandle? StdInRead = null;
						SafeFileHandle? StdOutWrite = null;
						SafeFileHandle? StdErrWrite = null;
						try
						{
							// Create stdin and stdout pipes for the child process. We'll close the handles for the child process' ends after it's been created.
							SECURITY_ATTRIBUTES SecurityAttributes = new SECURITY_ATTRIBUTES();
							SecurityAttributes.nLength = Marshal.SizeOf(SecurityAttributes);
							SecurityAttributes.bInheritHandle = 1;

							if (CreatePipe(out StdInRead, out StdInWrite, SecurityAttributes, 4 * 1024) == 0 || SetHandleInformation(StdInWrite, HANDLE_FLAG_INHERIT, 0) == 0)
							{
								throw new Win32ExceptionWithCode("Unable to create stdin pipe");
							}
							if (CreatePipe(out StdOutRead, out StdOutWrite, SecurityAttributes, 1024 * 1024) == 0 || SetHandleInformation(StdOutRead, HANDLE_FLAG_INHERIT, 0) == 0)
							{
								throw new Win32ExceptionWithCode("Unable to create stdout pipe");
							}

							if ((ManagedFlags & ManagedProcessFlags.MergeOutputPipes) != 0)
							{
								if (DuplicateHandle(GetCurrentProcess(), StdOutWrite, GetCurrentProcess(), out StdErrWrite, 0, true, DUPLICATE_SAME_ACCESS) == 0)
								{
									throw new Win32ExceptionWithCode("Unable to duplicate stdout handle");
								}
							}
							else
							{
								if (CreatePipe(out StdErrRead, out StdErrWrite, SecurityAttributes, 1024 * 1024) == 0 || SetHandleInformation(StdErrRead, HANDLE_FLAG_INHERIT, 0) == 0)
								{
									throw new Win32ExceptionWithCode("Unable to create stderr pipe");
								}
							}

							// Create the new process as suspended, so we can modify it before it starts executing (and potentially preempting us)
							STARTUPINFO StartupInfo = new STARTUPINFO();
							StartupInfo.cb = Marshal.SizeOf(StartupInfo);
							StartupInfo.hStdInput = StdInRead;
							StartupInfo.hStdOutput = StdOutWrite;
							StartupInfo.hStdError = StdErrWrite;
							StartupInfo.dwFlags = STARTF_USESTDHANDLES;

							// Under heavy load (ie. spawning large number of processes, typically Clang) we see CreateProcess very occasionally failing with ERROR_ACCESS_DENIED.
							int[] RetryDelay = { 100, 200, 1000, 5000 };
							for(int AttemptIdx = 0; ; AttemptIdx++)
							{
								if (CreateProcess(null, new StringBuilder("\"" + FileName + "\" " + CommandLine), IntPtr.Zero, IntPtr.Zero, true, Flags, EnvironmentBlock, WorkingDirectory, StartupInfo, ProcessInfo) != 0)
								{
									break;
								}

								if (Marshal.GetLastWin32Error() != ERROR_ACCESS_DENIED || AttemptIdx >= RetryDelay.Length)
								{
									throw new Win32ExceptionWithCode("Unable to create process");
								}

								Thread.Sleep(RetryDelay[AttemptIdx]);
							}

							// Save the process id
							Id = (int)ProcessInfo.dwProcessId;
						}
						finally
						{
							// Close the write ends of the handle. We don't want any other process to be able to inherit these.
							if (StdInRead != null)
							{
								StdInRead.Dispose();
								StdInRead = null;
							}
							if (StdOutWrite != null)
							{
								StdOutWrite.Dispose();
								StdOutWrite = null;
							}
							if (StdErrWrite != null)
							{
								StdErrWrite.Dispose();
								StdErrWrite = null;
							}
						}
					}

					// Add it to our job object
					if (Group != null && AssignProcessToJobObject(Group.JobHandle, ProcessInfo.hProcess) == 0)
					{
						// Support for nested job objects was only added in Windows 8; prior to that, assigning processes to job objects would fail. Figure out if we're already in a job, and ignore the error if we are.
						int OriginalError = Marshal.GetLastWin32Error();

						bool bProcessInJob;
						IsProcessInJob(GetCurrentProcess(), IntPtr.Zero, out bProcessInJob);

						if (!bProcessInJob)
						{
							throw new Win32ExceptionWithCode(OriginalError, "Unable to assign process to job object");
						}
					}

					// Create a JobObject for each spawned process to do CPU usage accounting of spawned process and all its children
					AccountingProcessGroup = new ManagedProcessGroup();
					if (AccountingProcessGroup != null)
					{
						if (AssignProcessToJobObject(AccountingProcessGroup.JobHandle, ProcessInfo.hProcess) == 0) 
						{
							throw new Win32Exception();
						}
					}

					// On systems with more than one processor group (more than one CPU socket, or more than 64 cores), it is possible
					// that processes launched from here may be scheduled by the operating system in a way that impedes overall throughput.
					//
					// From https://docs.microsoft.com/en-us/windows/win32/procthread/processor-groups
					// > The operating system initially assigns each process to a single group in a round-robin manner across the groups in the system
					//
					// When UBT launches a process here, it is not uncommon for more than one may be created (for example: clang and conhost)
					// If the number of processes created is the same as the number of processor groups in the system, this can lead
					// to high workload processes (like clang) predominantly assigned to one processor group, and lower workload processes
					// (like conhost) to the other.
					//
					// To reduce the chance of pathological process scheduling, we will explicitly distribute processes to each process
					// group. Doing so has been observed to reduce overall compile times for large builds by as much as 10%.
					if (ProcessorGroupCount > 1)
					{
						ushort ProcessorGroup = (ushort)(Interlocked.Increment(ref ProcessorGroupCounter) % ProcessorGroupCount);

						uint GroupProcessorCount = GetActiveProcessorCount(ProcessorGroup);

						GROUP_AFFINITY GroupAffinity = new GROUP_AFFINITY();
						GroupAffinity.Mask = ~0ul >> (int)(64 - GroupProcessorCount);
						GroupAffinity.Group = ProcessorGroup;
						SetThreadGroupAffinity(ProcessInfo.hThread, GroupAffinity, null);
					}
					

					// Allow the thread to start running
					if (ResumeThread(ProcessInfo.hThread) == -1)
					{
						throw new Win32ExceptionWithCode("Unable to resume thread in child process");
					}

					// If we have any input text, write it to stdin now
					StdIn = new FileStream(StdInWrite, FileAccess.Write, 4096, false);

					// Create the stream objects for reading the process output
					StdOut = new FileStream(StdOutRead, FileAccess.Read, 4096, false);
					StdOutText = new StreamReader(StdOut, Console.OutputEncoding);

					// Do the same for the stderr output
					if (StdErrRead != null)
					{
						StdErr = new FileStream(StdErrRead, FileAccess.Read, 4096, false);
						StdErrText = new StreamReader(StdErr, Console.OutputEncoding);
					}
					else
					{
						StdErr = StdOut;
						StdErrText = StdOutText;
					}

					// Wrap the process handle in a SafeFileHandle
					ProcessHandle = new SafeFileHandle(ProcessInfo.hProcess, true);
				}
				finally
				{
					if (ProcessInfo.hProcess != IntPtr.Zero && ProcessHandle == null)
					{
						CloseHandle(ProcessInfo.hProcess);
					}
					if (ProcessInfo.hThread != IntPtr.Zero)
					{
						CloseHandle(ProcessInfo.hThread);
					}
				}
			}
			finally
			{
				if (EnvironmentBlock != IntPtr.Zero)
				{
					Marshal.FreeHGlobal(EnvironmentBlock);
					EnvironmentBlock = IntPtr.Zero;
				}
			}
		}

		/// <summary>
		/// Spawns a new managed process using Win32 native functions. 
		/// </summary>
		/// <param name="Group">The managed process group to add to</param>
		/// <param name="FileName">Path to the executable to be run</param>
		/// <param name="CommandLine">Command line arguments for the process</param>
		/// <param name="WorkingDirectory">Working directory for the new process. May be null to use the current working directory.</param>
		/// <param name="Environment">Environment variables for the new process. May be null, in which case the current process' environment is inherited</param>
		/// <param name="Priority">Priority for the child process</param>
		/// <param name="Flags">Flags for the new process</param>
		private void CreateManagedProcessPortable(ManagedProcessGroup? Group, string FileName, string CommandLine, string? WorkingDirectory, IReadOnlyDictionary<string, string>? Environment, ProcessPriorityClass Priority, ManagedProcessFlags ManagedFlags)
		{
			// TODO: Process Arguments follow windows conventions in .NET Core
			// Which means single quotes ' are not considered quotes.
			// see https://github.com/dotnet/runtime/issues/29857
			// also see UE-102580
			// for rules see https://docs.microsoft.com/en-us/cpp/cpp/main-function-command-line-args
			CommandLine = CommandLine.Replace('\'', '\"');

			// for non-Windows platforms
			FrameworkProcess = new Process();
			FrameworkProcess.StartInfo.FileName = FileName;
			FrameworkProcess.StartInfo.Arguments = CommandLine;
			FrameworkProcess.StartInfo.WorkingDirectory = WorkingDirectory;
			FrameworkProcess.StartInfo.RedirectStandardInput = true;
			FrameworkProcess.StartInfo.RedirectStandardOutput = true;
			FrameworkProcess.StartInfo.RedirectStandardError = true;
			FrameworkProcess.StartInfo.UseShellExecute = false;
			FrameworkProcess.StartInfo.CreateNoWindow = true;

			if (Environment != null)
			{
				foreach (KeyValuePair<string, string> Pair in Environment)
				{
					FrameworkProcess.StartInfo.EnvironmentVariables[Pair.Key] = Pair.Value;
				}
			}

			// Merge StdOut with StdErr
			if ((ManagedFlags & ManagedProcessFlags.MergeOutputPipes) != 0)
			{
				// AnonymousPipes block reading even if the stream has been fully read, until the writer pipe handle is closed.
				FrameworkMergedStdWriter = new AnonymousPipeServerStream(PipeDirection.Out);
				StdOut = new AnonymousPipeClientStream(PipeDirection.In, FrameworkMergedStdWriter.ClientSafePipeHandle);
				StdOutText = new StreamReader(StdOut, Console.OutputEncoding);

				StdErr = StdOut;
				StdErrText = StdOutText;
				FrameworkMergedStdWriterThreadCount = 2;

				FrameworkStdOutThread = new Thread(() => {
					CopyPipe(FrameworkProcess.StandardOutput.BaseStream, FrameworkMergedStdWriter!);
					if (Interlocked.Decrement(ref FrameworkMergedStdWriterThreadCount) == 0)
					{
						// Dispose AnonymousPipe to unblock readers.
						FrameworkMergedStdWriter?.Dispose();
					}
				});
				FrameworkStdOutThread.Name = $"ManagedProcess Merge StdOut";
				FrameworkStdOutThread.IsBackground = true;

				FrameworkStdErrThread = new Thread(() => {
					CopyPipe(FrameworkProcess.StandardError.BaseStream, FrameworkMergedStdWriter!);
					if (Interlocked.Decrement(ref FrameworkMergedStdWriterThreadCount) == 0)
					{
						// Dispose AnonymousPipe to unblock readers.
						FrameworkMergedStdWriter?.Dispose();
					}
				});
				FrameworkStdErrThread.Name = $"ManagedProcess Merge StdErr";
				FrameworkStdErrThread.IsBackground = true;
			}

			FrameworkStartTime = DateTime.Now;
			FrameworkProcess.Start();
			FrameworkStdOutThread?.Start();
			FrameworkStdErrThread?.Start();

			try
			{
				FrameworkStartTime = FrameworkProcess.StartTime;
			}
			catch
			{
			}

			try
			{
				FrameworkProcess.PriorityClass = Priority;
			}
			catch
			{
			}

			Id = FrameworkProcess.Id;
			StdIn = FrameworkProcess.StandardInput.BaseStream;
			if ((ManagedFlags & ManagedProcessFlags.MergeOutputPipes) == 0)
			{
				StdOut = FrameworkProcess.StandardOutput.BaseStream;
				StdOutText = FrameworkProcess.StandardOutput;
				StdErr = FrameworkProcess.StandardError.BaseStream;
				StdErrText = FrameworkProcess.StandardError;
			}
		}

		/// <summary>
		/// Copy data from one pipe to another.
		/// </summary>
		/// <param name="Source"></param>
		/// <param name="Target"></param>
		void CopyPipe(Stream Source, Stream Target)
		{
			try
			{
				Source.CopyTo(Target);
				Target.Flush();
			}
			catch
			{
			}
		}

		/// <summary>
		/// Copy data from one pipe to another.
		/// </summary>
		/// <param name="Source"></param>
		/// <param name="Target"></param>
		/// <param name="CancellationToken"></param>
		async Task CopyPipeAsync(Stream Source, Stream Target, CancellationToken CancellationToken)
		{
			try
			{
				await Source.CopyToAsync(Target, CancellationToken);
				await Target.FlushAsync(CancellationToken);
			}
			catch
			{
			}
		}

		/// <summary>
		/// Free the managed resources for this process
		/// </summary>
		public void Dispose()
		{
			if(ProcessHandle != null)
			{
				TerminateProcess(ProcessHandle, 0);
				WaitForSingleObject(ProcessHandle, INFINITE);

				ProcessHandle.Dispose();
				ProcessHandle = null;
			}
			if(StdInWrite != null)
			{
				StdInWrite.Dispose();
				StdInWrite = null;
			}
			if(StdOutRead != null)
			{
				StdOutRead.Dispose();
				StdOutRead = null;
			}

			StdIn?.Dispose();
			StdOut?.Dispose();
			StdOutText?.Dispose();
			StdErr?.Dispose();
			StdErrText?.Dispose();

			if (AccountingProcessGroup != null) 
			{
				AccountingProcessGroup.Dispose();
			}
			if(FrameworkProcess != null)
			{
				if (!FrameworkProcess.HasExited)
				{
					try
					{
						FrameworkProcess.Kill();
						FrameworkProcess.WaitForExit();
					}
					catch
					{
					}
				}
				
				FrameworkMergedStdWriter?.Dispose();
				FrameworkMergedStdWriter = null;

				FrameworkStdOutThread?.Join();
				FrameworkStdOutThread = null;
				FrameworkStdErrThread?.Join();
				FrameworkStdErrThread = null;

				FrameworkProcess.Dispose();
				FrameworkProcess = null;
			}
		}

		/// <summary>
		/// Reads data from the process output
		/// </summary>
		/// <param name="Buffer">The buffer to receive the data</param>
		/// <param name="Offset">Offset within the buffer to write to</param>
		/// <param name="Count">Maximum number of bytes to read</param>
		/// <returns>Number of bytes read</returns>
		public int Read(byte[] Buffer, int Offset, int Count)
		{
			// Fill the buffer, reentering managed code every 20ms to allow thread abort exceptions to be thrown
			Task<int> ReadTask = StdOut!.ReadAsync(Buffer, Offset, Count);
			while(!ReadTask.Wait(20))
			{
				// Spin through managed code to allow things like ThreadAbortExceptions to be thrown.
			}
			return ReadTask.Result;
		}

		/// <summary>
		/// Reads data from the process output
		/// </summary>
		/// <param name="Buffer">The buffer to receive the data</param>
		/// <param name="Offset">Offset within the buffer to write to</param>
		/// <param name="Count">Maximum number of bytes to read</param>
		/// <returns>Number of bytes read</returns>
		public async Task<int> ReadAsync(byte[] Buffer, int Offset, int Count, CancellationToken CancellationToken)
		{
			return await StdOut!.ReadAsync(Buffer, Offset, Count, CancellationToken);
		}

		/// <summary>
		/// Copy the process output to the given stream
		/// </summary>
		/// <param name="OutputStream">The output stream</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns></returns>
		public Task CopyToAsync(Stream OutputStream, CancellationToken CancellationToken)
		{
			return StdOut!.CopyToAsync(OutputStream, CancellationToken);
		}

		/// <summary>
		/// Copy the process output to the given stream
		/// </summary>
		/// <param name="WriteOutput">The output stream</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns></returns>
		public Task CopyToAsync(Action<byte[], int, int> WriteOutput, int BufferSize, CancellationToken CancellationToken)
		{
			Func<byte[], int, int, CancellationToken, Task> WriteOutputAsync = (Buffer, Offset, Length, CancellationToken) => { WriteOutput(Buffer, Offset, Length); return Task.CompletedTask; };
			return CopyToAsync(WriteOutputAsync, BufferSize, CancellationToken);
		}

		/// <summary>
		/// Copy the process output to the given stream
		/// </summary>
		/// <param name="WriteOutputAsync">The output stream</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns></returns>
		public async Task CopyToAsync(Func<byte[], int, int, CancellationToken, Task> WriteOutputAsync, int BufferSize, CancellationToken CancellationToken)
		{
			TaskCompletionSource<bool> TaskCompletionSource = new TaskCompletionSource<bool>();
			using (CancellationTokenRegistration Registration = CancellationToken.Register(() => TaskCompletionSource.SetResult(false)))
			{
				byte[] Buffer = new byte[BufferSize];
				for (; ; )
				{
					Task<int> ReadTask = StdOut.ReadAsync(Buffer, 0, BufferSize, CancellationToken);

					Task CompletedTask = await Task.WhenAny(ReadTask, TaskCompletionSource.Task);
					CancellationToken.ThrowIfCancellationRequested();

					int Bytes = await ReadTask;
					if (Bytes == 0)
					{
						break;
					}
					await WriteOutputAsync(Buffer, 0, Bytes, CancellationToken);
				}
			}
		}

		/// <summary>
		/// Read all the output from the process. Does not return until the process terminates.
		/// </summary>
		/// <returns>List of output lines</returns>
		public List<string> ReadAllLines()
		{
			// Manually read all the output lines from the stream. Using ReadToEndAsync() or ReadAsync() on the StreamReader is abysmally slow, especially
			// for high-volume processes. Manually picking out the lines via a buffered ReadAsync() call was found to be 6x faster on 'p4 -ztag have' calls.
			List<string> OutputLines = new List<string>();
			byte[] Buffer = new byte[32 * 1024];
			byte LastCharacter = 0;
			int NumBytesInBuffer = 0;
			for(;;)
			{
				// If we're got a single line larger than 32kb (!), enlarge the buffer to ensure we can handle it
				if(NumBytesInBuffer == Buffer.Length)
				{
					Array.Resize(ref Buffer, Buffer.Length + 32 * 1024);
				}

				// Fill the buffer, reentering managed code every 20ms to allow thread abort exceptions to be thrown
				int NumBytesRead = Read(Buffer, NumBytesInBuffer, Buffer.Length - NumBytesInBuffer);
				if(NumBytesRead == 0)
				{
					if(NumBytesInBuffer > 0)
					{
						OutputLines.Add(Console.OutputEncoding.GetString(Buffer, 0, NumBytesInBuffer));
					}
					break;
				}

				// Otherwise append to the existing buffer
				NumBytesInBuffer += NumBytesRead;

				// Pull out all the complete output lines
				int LastStartIdx = 0;
				for(int Idx = 0; Idx < NumBytesInBuffer; Idx++)
				{
					if(Buffer[Idx] == '\r' || Buffer[Idx] == '\n')
					{
						if(Buffer[Idx] != '\n' || LastCharacter != '\r')
						{
							OutputLines.Add(Console.OutputEncoding.GetString(Buffer, LastStartIdx, Idx - LastStartIdx));
						}
						LastStartIdx = Idx + 1;
					}
					LastCharacter = Buffer[Idx];
				}

				// Shuffle everything back to the start of the buffer
				Array.Copy(Buffer, LastStartIdx, Buffer, 0, Buffer.Length - LastStartIdx);
				NumBytesInBuffer -= LastStartIdx;
			}
			WaitForExit();
			return OutputLines;
		}

		/// <summary>
		/// Reads a single line asynchronously
		/// </summary>
		/// <returns>New line</returns>
		public async Task<string?> ReadLineAsync()
		{
			return await StdOutText!.ReadLineAsync();
		}

		public TimeSpan TotalProcessorTime
		{
			get
			{
				if (AccountingProcessGroup != null)
				{
					return AccountingProcessGroup.TotalProcessorTime;
				}
				else
				{
					return new TimeSpan();
				}
			}
		}

		/// <summary>
		/// Block until the process outputs a line of text, or terminates.
		/// </summary>
		/// <param name="Line">Variable to receive the output line</param>
		/// <param name="Token">Cancellation token which can be used to abort the read operation.</param>
		/// <returns>True if a line was read, false if the process terminated without writing another line, or the cancellation token was signaled.</returns>
		public bool TryReadLine([NotNullWhen(true)] out string? Line, CancellationToken? Token = null)
		{
			try
			{
				// Busy wait for the ReadLine call to finish, so we can get interrupted by thread abort exceptions
				Task<string?> ReadLineTask = StdOutText!.ReadLineAsync();
				for (;;)
				{
					const int MillisecondsTimeout = 20;
					if (Token.HasValue 
							? ReadLineTask.Wait(MillisecondsTimeout, Token.Value) 
							: ReadLineTask.Wait(MillisecondsTimeout))
					{
						Line = ReadLineTask.IsCompleted ? ReadLineTask.Result : null;
						return Line != null;
					}
				}
			}
			catch (OperationCanceledException)
			{
				// If the cancel token is signalled, just return false.
				Line = null;
				return false;
			}
		}

		/// <summary>
		/// Waits for the process to exit
		/// </summary>
		/// <returns>Exit code of the process</returns>
		public void WaitForExit()
		{
			if (FrameworkProcess == null)
			{
				WaitForSingleObject(ProcessHandle!, INFINITE);
			}
			else
			{
				FrameworkProcess.WaitForExit();
			}
		}

		/// <summary>
		/// The exit code of the process. Throws an exception if the process has not terminated.
		/// </summary>
		public int ExitCode
		{
			get
			{
				if(FrameworkProcess == null)
				{
					int Value;
					if(GetExitCodeProcess(ProcessHandle!, out Value) == 0)
					{
						throw new Win32Exception();
					}
					return Value;
				}
				else
				{
					return FrameworkProcess.ExitCode;
				}
			}
		}

		private DateTime FrameworkStartTime = DateTime.MinValue;

		/// <summary>
		/// The creation time of the process.
		/// </summary>
		public DateTime StartTime
		{
			get
			{
				if (FrameworkProcess == null)
				{
					System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
					if (!GetProcessTimes(ProcessHandle!, out CreationTime, out _, out _, out _))
					{
						throw new Win32Exception();
					}
					return FileTimeToDateTime(CreationTime);
				}
				else
				{
					return FrameworkStartTime;
				}
			}
		}

		/// <summary>
		/// The exit time of the process. Throws an exception if the process has not terminated.
		/// </summary>
		public DateTime ExitTime
		{
			get
			{
				if (FrameworkProcess == null)
				{
					System.Runtime.InteropServices.ComTypes.FILETIME ExitTime;
					if (!GetProcessTimes(ProcessHandle!, out _, out ExitTime, out _, out _))
					{
						throw new Win32Exception();
					}
					return FileTimeToDateTime(ExitTime);
				}
				else
				{
					return FrameworkProcess.ExitTime;
				}
			}
		}
	}
}
