// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Win32.SafeHandles;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Exception used to represent caught file/directory exceptions.
	/// </summary>
	class WrappedFileOrDirectoryException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">Inner exception</param>
		/// <param name="Message">Message to display</param>
		public WrappedFileOrDirectoryException(Exception Inner, string Message) : base(Message)
		{
		}

		/// <summary>
		/// Returns the message to display for this exception
		/// </summary>
		/// <returns>Message to display</returns>
		public override string ToString()
		{
			return Message;
		}
	}

	/// <summary>
	/// Utility functions for manipulating files. Where these methods have similar functionality to those in the NET Framework, they generally go the extra mile to produce concise, specific error messages where possible.
	/// </summary>
	public static class FileUtils
	{
		/// <summary>
		/// Comparer that should be used for native path comparisons
		/// </summary>
		public static IEqualityComparer<string> PlatformPathComparer = RuntimePlatform.IsLinux ? StringComparer.Ordinal : StringComparer.OrdinalIgnoreCase;

		/// <summary>
		/// Utf8 string comparer that should be used for native path comparisons
		/// </summary>
		public static IEqualityComparer<Utf8String> PlatformPathComparerUtf8 = RuntimePlatform.IsLinux ? Utf8StringComparer.Ordinal : Utf8StringComparer.OrdinalIgnoreCase;

		/// <summary>
		/// Read all text for a file
		/// </summary>
		/// <param name="File"></param>
		/// <returns></returns>
		public static string ReadAllText(FileReference File)
		{
			try
			{
				return FileReference.ReadAllText(File);
			}
			catch (DirectoryNotFoundException Ex)
			{
				throw new WrappedFileOrDirectoryException(Ex, $"Unable to read file '{File}'. The directory does not exist.");
			}
			catch (FileNotFoundException Ex)
			{
				throw new WrappedFileOrDirectoryException(Ex, $"Unable to read file '{File}'. The file does not exist.");
			}
			catch (Exception Ex)
			{
				throw new WrappedFileOrDirectoryException(Ex, $"Unable to read file '{File}'");
			}
		}

		/// <summary>
		/// Finds the on-disk case of a a file
		/// </summary>
		/// <param name="Info">FileInfo instance describing the file</param>
		/// <returns>New FileInfo instance that represents the file with the correct case</returns>
		public static FileInfo FindCorrectCase(FileInfo Info)
		{
			DirectoryInfo ParentInfo = DirectoryUtils.FindCorrectCase(Info.Directory);
			foreach (FileInfo ChildInfo in ParentInfo.EnumerateFiles())
			{
				if (String.Equals(ChildInfo.Name, Info.Name, FileReference.Comparison))
				{
					return ChildInfo;
				}
			}
			return new FileInfo(Path.Combine(ParentInfo.FullName, Info.Name));
		}

		/// <summary>
		/// Creates a directory tree, with all intermediate branches
		/// </summary>
		/// <param name="Directory">The directory to create</param>
		public static void CreateDirectoryTree(DirectoryReference Directory)
		{
			if (!DirectoryReference.Exists(Directory))
			{
				DirectoryReference? ParentDirectory = Directory.ParentDirectory;
				if (ParentDirectory != null)
				{
					CreateDirectoryTree(ParentDirectory);
				}
				DirectoryReference.CreateDirectory(Directory);
			}
		}

		/// <summary>
		/// Deletes a file, whether it's read-only or not
		/// </summary>
		/// <param name="FileName">Name of the file to delete</param>
		public static void ForceDeleteFile(string FileName)
		{
			if (RuntimePlatform.IsWindows)
			{
				ForceDeleteFileWin32(FileName);
			}
			else
			{
				ForceDeleteFile(new FileInfo(FileName));
			}
		}

		/// <summary>
		/// Deletes a file, whether it's read-only or not
		/// </summary>
		/// <param name="File">The file to delete</param>
		public static void ForceDeleteFile(FileInfo File)
		{
			if (RuntimePlatform.IsWindows)
			{
				ForceDeleteFileWin32(File.FullName);
				File.Refresh();
			}
			else
			{
				try
				{
					if (File.Exists)
					{
						File.Attributes = FileAttributes.Normal;
						File.Delete();
					}
				}
				catch (Exception Ex)
				{
					throw new WrappedFileOrDirectoryException(Ex, String.Format("Unable to delete '{0}': {1}", File.FullName, Ex.Message.TrimEnd()));
				}
			}
		}

		/// <summary>
		/// Deletes a file, whether it's read-only or not
		/// </summary>
		/// <param name="Location">The file to delete</param>
		public static void ForceDeleteFile(FileReference Location)
		{
			if (RuntimePlatform.IsWindows)
			{
				ForceDeleteFileWin32(Location.FullName);
			}
			else
			{
				ForceDeleteFile(new FileInfo(Location.FullName));
			}
		}

		/// <summary>
		/// Deletes a directory and all its contents. Attempts to handle directories with long filenames (> 260 chars) on Windows.
		/// </summary>
		/// <param name="DirectoryName">Directory to delete</param>
		public static void ForceDeleteDirectory(string DirectoryName)
		{
			if (RuntimePlatform.IsWindows)
			{
				ForceDeleteLongDirectoryWin32("\\\\?\\" + DirectoryName);
			}
			else
			{
				ForceDeleteDirectory(new DirectoryInfo(DirectoryName));
			}
		}

		/// <summary>
		/// Deletes a directory and all its contents. Attempts to handle directories with long filenames (> 260 chars) on Windows.
		/// </summary>
		/// <param name="Directory">Directory to delete</param>
		public static void ForceDeleteDirectory(DirectoryInfo Directory)
		{
			if (Directory.Exists)
			{
				if (RuntimePlatform.IsWindows)
				{
					ForceDeleteLongDirectoryWin32("\\\\?\\" + Directory.FullName);
				}
				else
				{
					ForceDeleteDirectoryContents(Directory);
					ForceDeleteDirectoryInternal(Directory);
				}
			}
		}

		/// <summary>
		/// Deletes a directory and all its contents. Attempts to handle directories with long filenames (> 260 chars) on Windows.
		/// </summary>
		/// <param name="Location">Directory to delete</param>
		public static void ForceDeleteDirectory(DirectoryReference Location)
		{
			ForceDeleteDirectory(Location.FullName);
		}

		/// <summary>
		/// Helper method to delete a directory and throw a WrappedFileOrDirectoryException on failure.
		/// </summary>
		/// <param name="Directory">The directory to delete</param>
		static void ForceDeleteDirectoryInternal(DirectoryInfo Directory)
		{
			try
			{
				Directory.Delete(true);
			}
			catch (Exception Ex)
			{
				throw new WrappedFileOrDirectoryException(Ex, String.Format("Unable to delete '{0}': {1}", Directory.FullName, Ex.Message.TrimEnd()));
			}
		}

		/// <summary>
		/// Deletes the contents of a directory, without deleting the directory itself. Attempts to handle directories with long filenames (> 260 chars) on Windows.
		/// </summary>
		/// <param name="Directory">Directory to delete</param>
		public static void ForceDeleteDirectoryContents(string Directory)
		{
			ForceDeleteDirectoryContents(new DirectoryInfo(Directory));
		}

		/// <summary>
		/// Deletes the contents of a directory, without deleting the directory itself. Attempts to handle directories with long filenames (> 260 chars) on Windows.
		/// </summary>
		/// <param name="Directory">Directory to delete</param>
		public static void ForceDeleteDirectoryContents(DirectoryInfo Directory)
		{
			if (RuntimePlatform.IsWindows)
			{
				ForceDeleteLongDirectoryContentsWin32(Directory.FullName);
			}
			else
			{
				if (Directory.Exists)
				{
					foreach (FileInfo File in Directory.EnumerateFiles())
					{
						ForceDeleteFile(File);
					}
					foreach (DirectoryInfo SubDirectory in Directory.EnumerateDirectories())
					{
						if (SubDirectory.Attributes.HasFlag(FileAttributes.ReparsePoint))
						{
							ForceDeleteDirectoryInternal(SubDirectory);
						}
						else
						{
							ForceDeleteDirectory(SubDirectory);
						}
					}
				}
			}
		}

		/// <summary>
		/// Deletes the contents of a directory, without deleting the directory itself. Attempts to handle directories with long filenames (> 260 chars) on Windows.
		/// </summary>
		/// <param name="Directory">Directory to delete</param>
		public static void ForceDeleteDirectoryContents(DirectoryReference Directory)
		{
			ForceDeleteDirectoryContents(new DirectoryInfo(Directory.FullName));
		}

		/// <summary>
		/// Moves a file from one location to another. Creates the destination directory, and removes read-only files in the target location if necessary.
		/// </summary>
		/// <param name="SourceFileName">Path to the source file</param>
		/// <param name="TargetFileName">Path to the target file</param>
		public static void ForceMoveFile(string SourceFileName, string TargetFileName)
		{
			ForceMoveFile(new FileReference(SourceFileName), new FileReference(TargetFileName));
		}

		/// <summary>
		/// Moves a file from one location to another. Creates the destination directory, and removes read-only files in the target location if necessary.
		/// </summary>
		/// <param name="SourceFileName">Path to the source file</param>
		/// <param name="TargetFileName">Path to the target file</param>
		public static void ForceMoveFile(FileReference SourceLocation, FileReference TargetLocation)
		{
			// Try to move the file into place
			try
			{
				FileReference.Move(SourceLocation, TargetLocation);
				return;
			}
			catch (Exception Ex)
			{
				// Try to create the target directory
				try
				{
					if (!DirectoryReference.Exists(TargetLocation.Directory))
					{
						CreateDirectoryTree(TargetLocation.Directory);
					}
				}
				catch
				{
				}

				// Try to delete an existing file at the target location
				try
				{
					if (FileReference.Exists(TargetLocation))
					{
						FileReference.SetAttributes(TargetLocation, FileAttributes.Normal);
						FileReference.Delete(TargetLocation);
						FileReference.Move(SourceLocation, TargetLocation);
						return;
					}
				}
				catch (Exception DeleteEx)
				{
					throw new WrappedFileOrDirectoryException(new AggregateException(Ex, DeleteEx), String.Format("Unable to move {0} to {1} (also tried delete/move)", SourceLocation, TargetLocation));
				}

				// Throw the original exception
				throw new WrappedFileOrDirectoryException(Ex, String.Format("Unable to move {0} to {1}", SourceLocation, TargetLocation));
			}
		}

		/// <summary>
		/// Gets the file mode on Mac
		/// </summary>
		/// <param name="FileName"></param>
		/// <returns></returns>
		public static int GetFileMode_Mac(string FileName)
		{
			stat64_t stat = new stat64_t();
			int Result = stat64(FileName, stat);
			return (Result >= 0)? stat.st_mode : -1;
		}

		/// <summary>
		/// Sets the file mode on Mac
		/// </summary>
		/// <param name="FileName"></param>
		/// <param name="Mode"></param>
		public static void SetFileMode_Mac(string FileName, ushort Mode)
		{
			chmod(FileName, Mode);
		}

		/// <summary>
		/// Gets the file mode on Linux
		/// </summary>
		/// <param name="FileName"></param>
		/// <returns></returns>
		public static int GetFileMode_Linux(string FileName)
		{
			stat64_linux_t stat = new stat64_linux_t();
			int Result = stat64_linux(1, FileName, stat);
			return (Result >= 0)? (int)stat.st_mode : -1;
		}

		/// <summary>
		/// Sets the file mode on Linux
		/// </summary>
		/// <param name="FileName"></param>
		/// <param name="Mode"></param>
		public static void SetFileMode_Linux(string FileName, ushort Mode)
		{
			chmod_linux(FileName, Mode);
		}

		#region Win32 Native File Methods

		[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
		struct WIN32_FIND_DATA
		{
			public uint dwFileAttributes;
			public System.Runtime.InteropServices.ComTypes.FILETIME ftCreationTime;
			public System.Runtime.InteropServices.ComTypes.FILETIME ftLastAccessTime;
			public System.Runtime.InteropServices.ComTypes.FILETIME ftLastWriteTime;
			public uint nFileSizeHigh;
			public uint nFileSizeLow;
			public uint dwReserved0;
			public uint dwReserved1;
			[MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
			public string cFileName;
			[MarshalAs(UnmanagedType.ByValTStr, SizeConst = 14)]
			public string cAlternateFileName;
		}

		const uint FILE_ATTRIBUTE_READONLY = 0x01;
		const uint FILE_ATTRIBUTE_DIRECTORY = 0x10;
		const uint FILE_ATTRIBUTE_NORMAL = 0x80;

		[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
		static extern IntPtr FindFirstFileW(string FileName, ref WIN32_FIND_DATA FindData);

		[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool FindNextFileW(IntPtr FindHandle, ref WIN32_FIND_DATA FindData);

		[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool FindClose(IntPtr FindHandle);

		[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool DeleteFileW(string lpFileName);

		[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool RemoveDirectory(string lpPathName);

		[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool SetFileAttributesW(string lpFileName, uint dwFileAttributes);

		static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);

		const int ERROR_PATH_NOT_FOUND = 3;

		private static void ForceDeleteLongDirectoryContentsWin32(string DirName)
		{
			WIN32_FIND_DATA FindData = new WIN32_FIND_DATA();

			IntPtr hFind = FindFirstFileW(DirName + "\\*", ref FindData);
			if (hFind != INVALID_HANDLE_VALUE)
			{
				try
				{
					for (; ; )
					{
						string FullName = DirName + "\\" + FindData.cFileName;
						if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
						{
							if (FindData.cFileName != "." && FindData.cFileName != "..")
							{
								ForceDeleteLongDirectoryWin32(FullName);
							}
						}
						else
						{
							if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
							{
								SetFileAttributesW(FullName, FILE_ATTRIBUTE_NORMAL);
							}
							ForceDeleteFileWin32(FullName);
						}

						if (!FindNextFileW(hFind, ref FindData))
						{
							break;
						}
					}
				}
				finally
				{
					FindClose(hFind);
				}
			}
		}

		private static void ForceDeleteLongDirectoryWin32(string DirName)
		{
			ForceDeleteLongDirectoryContentsWin32(DirName);

			if (!RemoveDirectory(DirName))
			{
				int ErrorCode = Marshal.GetLastWin32Error();
				if (ErrorCode != ERROR_PATH_NOT_FOUND)
				{
					throw new WrappedFileOrDirectoryException(new Win32Exception(ErrorCode), "Unable to delete " + DirName);
				}
			}
		}

		const int ERROR_FILE_NOT_FOUND = 2;
		const int ERROR_ACCESS_DENIED = 5;

		[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
		internal static extern int GetFileAttributesW(string lpFileName);

		public static void ForceDeleteFileWin32(string FileName)
		{
			// Try to delete the file normally
			if (DeleteFileW(FileName))
			{
				return;
			}

			// Capture the exception for failing to delete the file
			Win32Exception Ex = new Win32Exception();

			// Check the file exists and is not readonly
			int Attributes = GetFileAttributesW(FileName);
			if (Attributes == -1)
			{
				int ErrorCode = Marshal.GetLastWin32Error();
				if (ErrorCode == ERROR_PATH_NOT_FOUND || ErrorCode == ERROR_FILE_NOT_FOUND)
				{
					return;
				}
			}
			else
			{
				if ((Attributes & (int)FileAttributes.ReadOnly) != 0)
				{
					if (SetFileAttributesW(FileName, (int)FileAttributes.Normal) && DeleteFileW(FileName))
					{
						return;
					}
				}
			}

			// Get a useful error message about why the delete failed
			StringBuilder Message = new StringBuilder($"Unable to delete {FileName} - {Ex.Message}");
			if (Ex.NativeErrorCode == ERROR_ACCESS_DENIED)
			{
				List<FileLockInfo_Win32>? LockInfoList;
				try
				{
					LockInfoList = GetFileLockInfo_Win32(FileName);
				}
				catch
				{
					LockInfoList = null;
				}

				if (LockInfoList != null && LockInfoList.Count > 0)
				{
					Message.Append("\nProcesses with open handles to file:");
					foreach (FileLockInfo_Win32 LockInfo in LockInfoList)
					{
						Message.Append($"\n  {LockInfo}");
					}
				}
			}
			throw new WrappedFileOrDirectoryException(Ex, Message.ToString());
		}

		#endregion

		#region Win32 Restart Manager API

		[StructLayout(LayoutKind.Sequential)]
		struct RM_UNIQUE_PROCESS
		{
			public int dwProcessId;
			public FILETIME ProcessStartTime;
		}

		const int RmRebootReasonNone = 0;
		static readonly int RM_SESSION_KEY_LEN = Marshal.SizeOf<Guid>();
		static readonly int CCH_RM_SESSION_KEY = RM_SESSION_KEY_LEN * 2;
		const int CCH_RM_MAX_APP_NAME = 255;
		const int CCH_RM_MAX_SVC_NAME = 63;

		enum RM_APP_TYPE
		{
			RmUnknownApp = 0,
			RmMainWindow = 1,
			RmOtherWindow = 2,
			RmService = 3,
			RmExplorer = 4,
			RmConsole = 5,
			RmCritical = 1000
		}

		[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
		struct RM_PROCESS_INFO
		{
			public RM_UNIQUE_PROCESS Process;

			[MarshalAs(UnmanagedType.ByValTStr, SizeConst = CCH_RM_MAX_APP_NAME + 1)]
			public string strAppName;

			[MarshalAs(UnmanagedType.ByValTStr, SizeConst = CCH_RM_MAX_SVC_NAME + 1)]
			public string strServiceShortName;

			public RM_APP_TYPE ApplicationType;
			public uint AppStatus;
			public uint TSSessionId;

			[MarshalAs(UnmanagedType.Bool)]
			public bool bRestartable;
		}

		[DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
		static extern int RmRegisterResources(uint pSessionHandle, uint nFiles, string[] rgsFilenames, uint nApplications, [In] RM_UNIQUE_PROCESS[]? rgApplications, uint nServices, string[]? rgsServiceNames);

		[DllImport("rstrtmgr.dll", CharSet = CharSet.Auto)]
		static extern int RmStartSession(out uint pSessionHandle, int dwSessionFlags, StringBuilder strSessionKey);

		[DllImport("rstrtmgr.dll")]
		static extern int RmEndSession(uint pSessionHandle);

		[DllImport("rstrtmgr.dll")]
		static extern int RmGetList(uint dwSessionHandle,
									out uint pnProcInfoNeeded,
									ref uint pnProcInfo,
									[In, Out] RM_PROCESS_INFO[] rgAffectedApps,
									ref uint lpdwRebootReasons);

		const int PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;

		[DllImport("kernel32.dll", SetLastError = true)]
		static extern SafeProcessHandle OpenProcess(
			 int processAccess,
			 bool bInheritHandle,
			 int processId
		);

		[DllImport("kernel32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool GetProcessTimes(SafeProcessHandle hProcess, out FILETIME
		   lpCreationTime, out FILETIME lpExitTime, out FILETIME lpKernelTime,
		   out FILETIME lpUserTime);

		[DllImport("kernel32.dll", SetLastError = true)]
		static extern bool QueryFullProcessImageName([In]SafeProcessHandle hProcess, [In]int dwFlags, [Out]StringBuilder lpExeName, ref int lpdwSize);

		/// <summary>
		/// Information about a locked file
		/// </summary>
		public class FileLockInfo_Win32
		{
			/// <summary>
			/// Process id
			/// </summary>
			public int ProcessId;

			/// <summary>
			/// Path to the process holding the lock
			/// </summary>
			public string? FileName;

			/// <summary>
			/// Name of the application
			/// </summary>
			public string AppName;

			/// <summary>
			/// Time at which the process started
			/// </summary>
			public DateTime StartTime;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="ProcessId">Process id</param>
			/// <param name="FileName">Path to the process holding the lock</param>
			/// <param name="AppName">Name of the application</param>
			/// <param name="StartTime">Time at which the process started</param>
			public FileLockInfo_Win32(int ProcessId, string? FileName, string AppName, DateTime StartTime)
			{
				this.ProcessId = ProcessId;
				this.FileName = FileName;
				this.AppName = AppName;
				this.StartTime = StartTime;
			}

			/// <inheritdoc/>
			public override string ToString()
			{
				return $"{ProcessId}: {FileName ?? AppName} (started {StartTime})";
			}
		}

		/// <summary>
		/// Gets a list of processes that have a handle to the given file open
		/// </summary>
		/// <param name="FileName">File to check</param>
		/// <returns>List of processes with a lock open</returns>
		public static List<FileLockInfo_Win32> GetFileLockInfo_Win32(string FileName)
		{
			uint SessionHandle = 0;
			try
			{
				StringBuilder SessionKey = new StringBuilder(CCH_RM_SESSION_KEY + 1);

				int Result = RmStartSession(out SessionHandle, 0, SessionKey);
				if (Result != 0)
				{
					throw new Win32Exception(Result, "Unable to open restart manager session");
				}

				Result = RmRegisterResources(SessionHandle, 1, new string[] { FileName }, 0, null, 0, null);
				if (Result != 0)
				{
					throw new Win32Exception(Result, "Unable to register resource with restart manager");
				}

				uint nProcInfoNeeded = 0;
				uint nProcInfo = 10;
				uint Reason = 0;
				RM_PROCESS_INFO[] ProcessInfoArray = new RM_PROCESS_INFO[nProcInfo];
				Result = RmGetList(SessionHandle, out nProcInfoNeeded, ref nProcInfo, ProcessInfoArray, ref Reason);
				if (Result != 0)
				{
					throw new Win32Exception(Result, "Unable to query processes with file handle open");
				}

				List<FileLockInfo_Win32> FileLocks = new List<FileLockInfo_Win32>();
				for (int Idx = 0; Idx < nProcInfo; Idx++)
				{
					RM_PROCESS_INFO ProcessInfo = ProcessInfoArray[Idx];
					long StartTimeTicks = FileTimeToTicks(ProcessInfo.Process.ProcessStartTime);

					string? ImageName = null;
					using (SafeProcessHandle hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, ProcessInfo.Process.dwProcessId))
					{
						if (hProcess != null)
						{
							FILETIME CreateTime, ExitTime, KernelTime, UserTime;
							if (GetProcessTimes(hProcess, out CreateTime, out ExitTime, out KernelTime, out UserTime) && FileTimeToTicks(CreateTime) == StartTimeTicks)
							{
								int Capacity = 260;
								StringBuilder ImageNameBuilder = new StringBuilder(Capacity);
								if (QueryFullProcessImageName(hProcess, 0, ImageNameBuilder, ref Capacity))
								{
									ImageName = ImageNameBuilder.ToString(0, Capacity);
								}
							}
						}
					}

					FileLocks.Add(new FileLockInfo_Win32(ProcessInfo.Process.dwProcessId, ImageName, ProcessInfo.strAppName, DateTime.FromFileTime(StartTimeTicks)));
				}
				return FileLocks;
			}
			finally
			{
				if (SessionHandle != 0)
				{
					RmEndSession(SessionHandle);
				}
			}
		}

		private static long FileTimeToTicks(FILETIME FileTime)
		{
			return (long)(uint)FileTime.dwLowDateTime | ((long)(uint)FileTime.dwHighDateTime << 32);
		}

		#endregion

		#region Mac Native File Methods
#pragma warning disable CS0649
		struct timespec_t
		{
			public ulong tv_sec;
			public ulong tv_nsec;
		}

		[StructLayout(LayoutKind.Sequential)]
		class stat64_t
		{
			public uint st_dev;
			public ushort st_mode;
			public ushort st_nlink;
			public ulong st_ino;
			public uint st_uid;
			public uint st_gid;
			public uint st_rdev;
			public timespec_t st_atimespec;
			public timespec_t st_mtimespec;
			public timespec_t st_ctimespec;
			public timespec_t st_birthtimespec;
			public ulong st_size;
			public ulong st_blocks;
			public uint st_blksize;
			public uint st_flags;
			public uint st_gen;
			public uint st_lspare;
			public ulong st_qspare1;
			public ulong st_qspare2;
		}

		[DllImport("libSystem.dylib")]
		static extern int stat64(string pathname, stat64_t stat);

		[DllImport("libSystem.dylib")]
		static extern int chmod(string path, ushort mode);

#pragma warning restore CS0649
		#endregion

		#region Linux Native File Methods
#pragma warning disable CS0649

		[StructLayout(LayoutKind.Sequential)]
		class stat64_linux_t
		{
			public ulong st_dev;
			public ulong st_ino;
			public ulong st_nlink;
			public uint st_mode;
			public uint st_uid;
			public uint st_gid;
			public int pad0;
			public ulong st_rdev;
			public long st_size;
			public long st_blksize;
			public long st_blocks;
			public timespec_t st_atime;
			public timespec_t st_mtime;
			public timespec_t st_ctime;
			public long glibc_reserved0;
			public long glibc_reserved1;
			public long glibc_reserved2;
		};

		/* stat tends to get compiled to another symbol and libc doesnt directly have that entry point */
		[DllImport("libc", EntryPoint="__xstat64")]
		static extern int stat64_linux(int ver, string pathname, stat64_linux_t stat);

		[DllImport("libc", EntryPoint="chmod")]
		static extern int chmod_linux(string path, ushort mode);

#pragma warning restore CS0649
		#endregion
	}
}
