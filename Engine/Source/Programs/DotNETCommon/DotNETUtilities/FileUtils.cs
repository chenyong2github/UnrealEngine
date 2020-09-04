// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace Tools.DotNETCommon
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
		public static IEqualityComparer<string> PlatformPathComparer = Environment.OSVersion.Platform == PlatformID.Unix ? StringComparer.Ordinal : StringComparer.OrdinalIgnoreCase;

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
				CreateDirectoryTree(Directory.ParentDirectory);
				DirectoryReference.CreateDirectory(Directory);
			}
		}

		/// <summary>
		/// Deletes a file, whether it's read-only or not
		/// </summary>
		/// <param name="FileName">Name of the file to delete</param>
		public static void ForceDeleteFile(string FileName)
		{
			ForceDeleteFile(new FileInfo(FileName));
		}

		/// <summary>
		/// Deletes a file, whether it's read-only or not
		/// </summary>
		/// <param name="File">The file to delete</param>
		public static void ForceDeleteFile(FileInfo File)
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

		/// <summary>
		/// Deletes a file, whether it's read-only or not
		/// </summary>
		/// <param name="Location">The file to delete</param>
		public static void ForceDeleteFile(FileReference Location)
		{
			ForceDeleteFile(new FileInfo(Location.FullName));
		}

		/// <summary>
		/// Deletes a directory and all its contents. Attempts to handle directories with long filenames (> 260 chars) on Windows.
		/// </summary>
		/// <param name="DirectoryName">Directory to delete</param>
		public static void ForceDeleteDirectory(string DirectoryName)
		{
			if (Environment.OSVersion.Platform == PlatformID.Win32NT)
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
				if (Environment.OSVersion.Platform == PlatformID.Win32NT)
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
			if (Environment.OSVersion.Platform == PlatformID.Win32NT)
			{
				ForceDeleteLongDirectoryContentsWin32(Directory.FullName);
			}
			else
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
				throw new WrappedFileOrDirectoryException(Ex, String.Format("{0}: Unable to move {1} to {2}", Ex.GetType(), SourceLocation, TargetLocation));
			}
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
							if (!DeleteFileW(FullName))
							{
								throw new WrappedFileOrDirectoryException(new Win32Exception(), "Unable to delete " + FullName);
							}
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
				throw new WrappedFileOrDirectoryException(new Win32Exception(), "Unable to delete " + DirName);
			}
		}

		#endregion
	}
}
