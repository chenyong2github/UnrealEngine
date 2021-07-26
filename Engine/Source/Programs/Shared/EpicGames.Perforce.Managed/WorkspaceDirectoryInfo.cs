// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Stores the state of a directory in the workspace
	/// </summary>
	class WorkspaceDirectoryInfo
	{
		/// <summary>
		/// The parent directory
		/// </summary>
		public WorkspaceDirectoryInfo? ParentDirectory { get; }

		/// <summary>
		/// Name of this directory
		/// </summary>
		public Utf8String Name { get; }

		/// <summary>
		/// Digest of the matching stream directory info with the base path. This should be set to zero if the workspace is modified.
		/// </summary>
		public IoHash StreamDirectoryDigest { get; set; }

		/// <summary>
		/// Map of name to file
		/// </summary>
		public Dictionary<Utf8String, WorkspaceFileInfo> NameToFile { get; set; }

		/// <summary>
		/// Map of name to subdirectory
		/// </summary>
		public Dictionary<Utf8String, WorkspaceDirectoryInfo> NameToSubDirectory { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="RootDir"></param>
		public WorkspaceDirectoryInfo(DirectoryReference RootDir)
			: this(null, RootDir.FullName, null)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ParentDirectory">The parent directory</param>
		/// <param name="Name">Name of this directory</param>
		/// <param name="Ref">The corresponding stream digest</param>
		public WorkspaceDirectoryInfo(WorkspaceDirectoryInfo? ParentDirectory, Utf8String Name, StreamTreeRef? Ref)
		{
			this.ParentDirectory = ParentDirectory;
			this.Name = Name;
			this.StreamDirectoryDigest = (Ref == null) ? IoHash.Zero : Ref.GetHash();
			this.NameToFile = new Dictionary<Utf8String, WorkspaceFileInfo>(Utf8StringComparer.Ordinal);
			this.NameToSubDirectory = new Dictionary<Utf8String, WorkspaceDirectoryInfo>(FileUtils.PlatformPathComparerUtf8);
		}

		/// <summary>
		/// Adds a file to the workspace
		/// </summary>
		/// <param name="Path">Relative path to the file, using forward slashes, and without a leading slash</param>
		/// <param name="Length">Length of the file on disk</param>
		/// <param name="LastModifiedTicks">Last modified time of the file</param>
		/// <param name="bReadOnly">Whether the file is read only</param>
		/// <param name="ContentId">Unique identifier for the server content</param>
		public void AddFile(Utf8String Path, long Length, long LastModifiedTicks, bool bReadOnly, FileContentId ContentId)
		{
			StreamDirectoryDigest = IoHash.Zero;

			int Idx = Path.Span.IndexOf((byte)'/');
			if (Idx == -1)
			{
				NameToFile[Path] = new WorkspaceFileInfo(this, Path, Length, LastModifiedTicks, bReadOnly, ContentId);
			}
			else
			{
				Utf8String Name = Path.Slice(0, Idx);

				WorkspaceDirectoryInfo? SubDirectory;
				if (!NameToSubDirectory.TryGetValue(Name, out SubDirectory))
				{
					SubDirectory = new WorkspaceDirectoryInfo(this, Name, null);
					NameToSubDirectory[Name] = SubDirectory;
				}

				SubDirectory.AddFile(Path.Slice(Idx + 1), Length, LastModifiedTicks, bReadOnly, ContentId);
			}
		}

		/// <summary>
		/// Create a flat list of files in this workspace
		/// </summary>
		/// <returns>List of files</returns>
		public List<WorkspaceFileInfo> GetFiles()
		{
			List<WorkspaceFileInfo> Files = new List<WorkspaceFileInfo>();
			GetFilesInternal(Files);
			return Files;
		}

		/// <summary>
		/// Internal helper method for recursing through the tree to build a file list
		/// </summary>
		/// <param name="Files"></param>
		private void GetFilesInternal(List<WorkspaceFileInfo> Files)
		{
			Files.AddRange(NameToFile.Values);

			foreach (KeyValuePair<Utf8String, WorkspaceDirectoryInfo> Pair in NameToSubDirectory)
			{
				Pair.Value.GetFilesInternal(Files);
			}
		}

		/// <summary>
		/// Refresh the state of the workspace on disk
		/// </summary>
		/// <param name="bRemoveUntracked">Whether to remove files that are not part of the stream</param>
		/// <param name="FilesToDelete">Receives an array of files to delete</param>
		/// <param name="DirectoriesToDelete">Recevies an array of directories to delete</param>
		public void Refresh(bool bRemoveUntracked, out FileInfo[] FilesToDelete, out DirectoryInfo[] DirectoriesToDelete)
		{
			ConcurrentQueue<FileInfo> ConcurrentFilesToDelete = new ConcurrentQueue<FileInfo>();
			ConcurrentQueue<DirectoryInfo> ConcurrentDirectoriesToDelete = new ConcurrentQueue<DirectoryInfo>();
			using (ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
			{
				Queue.Enqueue(() => Refresh(new DirectoryInfo(GetFullName()), bRemoveUntracked, ConcurrentFilesToDelete, ConcurrentDirectoriesToDelete, Queue));
			}
			DirectoriesToDelete = ConcurrentDirectoriesToDelete.ToArray();
			FilesToDelete = ConcurrentFilesToDelete.ToArray();
		}

		/// <summary>
		/// Recursive method for querying the workspace state
		/// </summary>
		/// <param name="Info"></param>
		/// <param name="bRemoveUntracked"></param>
		/// <param name="FilesToDelete"></param>
		/// <param name="DirectoriesToDelete"></param>
		/// <param name="Queue"></param>
		void Refresh(DirectoryInfo Info, bool bRemoveUntracked, ConcurrentQueue<FileInfo> FilesToDelete, ConcurrentQueue<DirectoryInfo> DirectoriesToDelete, ThreadPoolWorkQueue Queue)
		{
			// Recurse through subdirectories
			Dictionary<Utf8String, WorkspaceDirectoryInfo> NewNameToSubDirectory = new Dictionary<Utf8String, WorkspaceDirectoryInfo>(NameToSubDirectory.Count, NameToSubDirectory.Comparer);
			foreach (DirectoryInfo SubDirectoryInfo in Info.EnumerateDirectories())
			{
				WorkspaceDirectoryInfo? SubDirectory;
				if (NameToSubDirectory.TryGetValue(SubDirectoryInfo.Name, out SubDirectory))
				{
					NewNameToSubDirectory.Add(SubDirectory.Name, SubDirectory);
					Queue.Enqueue(() => SubDirectory.Refresh(SubDirectoryInfo, bRemoveUntracked, FilesToDelete, DirectoriesToDelete, Queue));
				}
				else if (bRemoveUntracked)
				{
					DirectoriesToDelete.Enqueue(SubDirectoryInfo);
				}
			}
			NameToSubDirectory = NewNameToSubDirectory;

			// Figure out which files have changed.
			Dictionary<Utf8String, WorkspaceFileInfo> NewNameToFile = new Dictionary<Utf8String, WorkspaceFileInfo>(NameToFile.Count, NameToFile.Comparer);
			foreach (FileInfo File in Info.EnumerateFiles())
			{
				WorkspaceFileInfo? StagedFile;
				if (NameToFile.TryGetValue(File.Name, out StagedFile))
				{
					if (StagedFile.MatchesAttributes(File))
					{
						NewNameToFile.Add(StagedFile.Name, StagedFile);
					}
					else
					{
						FilesToDelete.Enqueue(File);
					}
				}
				else
				{
					if (bRemoveUntracked)
					{
						FilesToDelete.Enqueue(File);
					}
				}
			}

			// If the file state has changed, clear the directory hashes
			if (NameToFile.Count != NewNameToFile.Count)
			{
				for (WorkspaceDirectoryInfo? Directory = this; Directory != null && Directory.StreamDirectoryDigest != IoHash.Zero; Directory = Directory.ParentDirectory)
				{
					Directory.StreamDirectoryDigest = IoHash.Zero;
				}
			}

			// Update the new file list
			NameToFile = NewNameToFile;
		}

		/// <summary>
		/// Builds a list of differences from the working directory
		/// </summary>
		/// <returns></returns>
		public string[] FindDifferences()
		{
			ConcurrentQueue<string> Paths = new ConcurrentQueue<string>();
			using (ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
			{
				Queue.Enqueue(() => FindDifferences(new DirectoryInfo(GetFullName()), "/", Paths, Queue));
			}
			return Paths.OrderBy(x => x).ToArray();
		}

		/// <summary>
		/// Helper method for finding differences from the working directory
		/// </summary>
		/// <param name="Directory"></param>
		/// <param name="Path"></param>
		/// <param name="Paths"></param>
		/// <param name="Queue"></param>
		void FindDifferences(DirectoryInfo Directory, string Path, ConcurrentQueue<string> Paths, ThreadPoolWorkQueue Queue)
		{
			// Recurse through subdirectories
			HashSet<Utf8String> RemainingSubDirectoryNames = new HashSet<Utf8String>(NameToSubDirectory.Keys);
			foreach (DirectoryInfo SubDirectory in Directory.EnumerateDirectories())
			{
				WorkspaceDirectoryInfo? StagedSubDirectory;
				if (NameToSubDirectory.TryGetValue(SubDirectory.Name, out StagedSubDirectory))
				{
					RemainingSubDirectoryNames.Remove(SubDirectory.Name);
					Queue.Enqueue(() => StagedSubDirectory.FindDifferences(SubDirectory, String.Format("{0}{1}/", Path, SubDirectory.Name), Paths, Queue));
					continue;
				}
				Paths.Enqueue(String.Format("+{0}{1}/...", Path, SubDirectory.Name));
			}
			foreach (Utf8String RemainingSubDirectoryName in RemainingSubDirectoryNames)
			{
				Paths.Enqueue(String.Format("-{0}{1}/...", Path, RemainingSubDirectoryName));
			}

			// Search through files
			HashSet<Utf8String> RemainingFileNames = new HashSet<Utf8String>(NameToFile.Keys);
			foreach (FileInfo File in Directory.EnumerateFiles())
			{
				WorkspaceFileInfo? StagedFile;
				if (!NameToFile.TryGetValue(File.Name, out StagedFile))
				{
					Paths.Enqueue(String.Format("+{0}{1}", Path, File.Name));
				}
				else if (!StagedFile.MatchesAttributes(File))
				{
					Paths.Enqueue(String.Format("!{0}{1}", Path, File.Name));
					RemainingFileNames.Remove(File.Name);
				}
				else
				{
					RemainingFileNames.Remove(File.Name);
				}
			}
			foreach (Utf8String RemainingFileName in RemainingFileNames)
			{
				Paths.Enqueue(String.Format("-{0}{1}", Path, RemainingFileName));
			}
		}

		/// <summary>
		/// Get the full path to this directory
		/// </summary>
		/// <returns></returns>
		public string GetFullName()
		{
			StringBuilder Builder = new StringBuilder();
			AppendFullPath(Builder);
			return Builder.ToString();
		}

		/// <summary>
		/// Get the path to this directory
		/// </summary>
		/// <returns></returns>
		public DirectoryReference GetLocation()
		{
			return new DirectoryReference(GetFullName());
		}

		/// <summary>
		/// Append the client path, using native directory separators, to the given string builder
		/// </summary>
		/// <param name="Builder"></param>
		public void AppendClientPath(StringBuilder Builder)
		{
			if (ParentDirectory != null)
			{
				ParentDirectory.AppendClientPath(Builder);
				Builder.Append(Name);
				Builder.Append(Path.DirectorySeparatorChar);
			}
		}

		/// <summary>
		/// Append the path for this directory to the given string builder
		/// </summary>
		/// <param name="Builder"></param>
		public void AppendFullPath(StringBuilder Builder)
		{
			if (ParentDirectory != null)
			{
				ParentDirectory.AppendFullPath(Builder);
				Builder.Append(Path.DirectorySeparatorChar);
			}
			Builder.Append(Name);
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			return GetFullName();
		}
	}

	/// <summary>
	/// Extension methods for WorkspaceDirectoryInfo
	/// </summary>
	static class WorkspaceDirectoryInfoExtensions
	{
		public static void ReadWorkspaceDirectoryInfo(this MemoryReader Reader, WorkspaceDirectoryInfo DirectoryInfo, ManagedWorkspaceVersion Version)
		{
			if (Version < ManagedWorkspaceVersion.AddDigest)
			{
				DirectoryInfo.StreamDirectoryDigest = IoHash.Zero;
			}
			else if (Version < ManagedWorkspaceVersion.AddDigestIoHash)
			{
				Reader.ReadFixedLengthBytes(Sha1.Length);
				DirectoryInfo.StreamDirectoryDigest = IoHash.Zero;
			}
			else
			{
				DirectoryInfo.StreamDirectoryDigest = Reader.ReadIoHash();
			}

			int NumFiles = Reader.ReadInt32();
			for (int Idx = 0; Idx < NumFiles; Idx++)
			{
				WorkspaceFileInfo FileInfo = Reader.ReadWorkspaceFileInfo(DirectoryInfo);
				DirectoryInfo.NameToFile.Add(FileInfo.Name, FileInfo);
			}

			int NumSubDirectories = Reader.ReadInt32();
			for (int Idx = 0; Idx < NumSubDirectories; Idx++)
			{
				Utf8String Name = Reader.ReadString();

				WorkspaceDirectoryInfo SubDirectory = new WorkspaceDirectoryInfo(DirectoryInfo, Name, null);
				Reader.ReadWorkspaceDirectoryInfo(SubDirectory, Version);
				DirectoryInfo.NameToSubDirectory[SubDirectory.Name] = SubDirectory;
			}
		}

		public static void WriteWorkspaceDirectoryInfo(this MemoryWriter Writer, WorkspaceDirectoryInfo DirectoryInfo)
		{
			Writer.WriteIoHash(DirectoryInfo.StreamDirectoryDigest);

			Writer.WriteInt32(DirectoryInfo.NameToFile.Count);
			foreach (WorkspaceFileInfo File in DirectoryInfo.NameToFile.Values)
			{
				Writer.WriteWorkspaceFileInfo(File);
			}

			Writer.WriteInt32(DirectoryInfo.NameToSubDirectory.Count);
			foreach (WorkspaceDirectoryInfo SubDirectory in DirectoryInfo.NameToSubDirectory.Values)
			{
				Writer.WriteString(SubDirectory.Name);
				Writer.WriteWorkspaceDirectoryInfo(SubDirectory);
			}
		}

		public static int GetSerializedSize(this WorkspaceDirectoryInfo DirectoryInfo)
		{
			return Digest<Sha1>.Length + sizeof(int) + DirectoryInfo.NameToFile.Values.Sum(x => x.GetSerializedSize()) + sizeof(int) + DirectoryInfo.NameToSubDirectory.Values.Sum(x => x.Name.GetSerializedSize() + x.GetSerializedSize());
		}
	}
}
