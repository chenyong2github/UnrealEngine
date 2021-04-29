// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// 
	/// </summary>
	class WorkspaceDirectoryInfo
	{
		public readonly WorkspaceDirectoryInfo? ParentDirectory;
		public readonly ReadOnlyUtf8String Name;
		public Dictionary<ReadOnlyUtf8String, WorkspaceFileInfo> NameToFile;
		public Dictionary<ReadOnlyUtf8String, WorkspaceDirectoryInfo> NameToSubDirectory;

		public WorkspaceDirectoryInfo(DirectoryReference RootDir)
			: this(null, RootDir.FullName)
		{
		}

		public WorkspaceDirectoryInfo(WorkspaceDirectoryInfo? ParentDirectory, ReadOnlyUtf8String Name)
		{
			this.ParentDirectory = ParentDirectory;
			this.Name = Name;
			this.NameToFile = new Dictionary<ReadOnlyUtf8String, WorkspaceFileInfo>(ReadOnlyUtf8StringComparer.Ordinal);
			this.NameToSubDirectory = new Dictionary<ReadOnlyUtf8String, WorkspaceDirectoryInfo>(FileUtils.PlatformPathComparerUtf8);
		}

		public void AddFile(ReadOnlyUtf8String Path, long Length, long LastModifiedTicks, bool bReadOnly, FileContentId ContentId)
		{
			int Idx = Path.Span.IndexOf((byte)'/');
			if (Idx == -1)
			{
				NameToFile[Path] = new WorkspaceFileInfo(this, Path, Length, LastModifiedTicks, bReadOnly, ContentId);
			}
			else
			{
				ReadOnlyUtf8String Name = Path.Slice(0, Idx);

				WorkspaceDirectoryInfo? SubDirectory;
				if (!NameToSubDirectory.TryGetValue(Name, out SubDirectory))
				{
					SubDirectory = new WorkspaceDirectoryInfo(this, Name);
					NameToSubDirectory[Name] = SubDirectory;
				}

				SubDirectory.AddFile(Path.Slice(Idx + 1), Length, LastModifiedTicks, bReadOnly, ContentId);
			}
		}

		public List<WorkspaceFileInfo> GetFiles()
		{
			List<WorkspaceFileInfo> Files = new List<WorkspaceFileInfo>();
			GetFilesInternal(Files);
			return Files;
		}

		private void GetFilesInternal(List<WorkspaceFileInfo> Files)
		{
			Files.AddRange(NameToFile.Values);

			foreach (KeyValuePair<ReadOnlyUtf8String, WorkspaceDirectoryInfo> Pair in NameToSubDirectory)
			{
				Pair.Value.GetFilesInternal(Files);
			}
		}

		public void Refresh(bool bRemoveUntracked, out FileInfo[] FilesToDelete, out DirectoryInfo[] DirectoriesToDelete)
		{
			ConcurrentBag<FileInfo> ConcurrentFilesToDelete = new ConcurrentBag<FileInfo>();
			ConcurrentBag<DirectoryInfo> ConcurrentDirectoriesToDelete = new ConcurrentBag<DirectoryInfo>();
			using (ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
			{
				Queue.Enqueue(() => Refresh(new DirectoryInfo(GetFullName()), bRemoveUntracked, ConcurrentFilesToDelete, ConcurrentDirectoriesToDelete, Queue));
			}
			DirectoriesToDelete = ConcurrentDirectoriesToDelete.ToArray();
			FilesToDelete = ConcurrentFilesToDelete.ToArray();
		}

		private void Refresh(DirectoryInfo Info, bool bRemoveUntracked, ConcurrentBag<FileInfo> FilesToDelete, ConcurrentBag<DirectoryInfo> DirectoriesToDelete, ThreadPoolWorkQueue Queue)
		{
			// Recurse through subdirectories
			Dictionary<ReadOnlyUtf8String, WorkspaceDirectoryInfo> NewNameToSubDirectory = new Dictionary<ReadOnlyUtf8String, WorkspaceDirectoryInfo>(NameToSubDirectory.Count, NameToSubDirectory.Comparer);
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
					DirectoriesToDelete.Add(SubDirectoryInfo);
				}
			}
			NameToSubDirectory = NewNameToSubDirectory;

			// Figure out which files have changed.
			Dictionary<ReadOnlyUtf8String, WorkspaceFileInfo> NewNameToFile = new Dictionary<ReadOnlyUtf8String, WorkspaceFileInfo>(NameToFile.Count, NameToFile.Comparer);
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
						FilesToDelete.Add(File);
					}
				}
				else
				{
					if (bRemoveUntracked)
					{
						FilesToDelete.Add(File);
					}
				}
			}
			NameToFile = NewNameToFile;
		}

		public string[] FindDifferences()
		{
			ConcurrentBag<string> Paths = new ConcurrentBag<string>();
			using (ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
			{
				Queue.Enqueue(() => FindDifferences(new DirectoryInfo(GetFullName()), "/", Paths, Queue));
			}
			return Paths.OrderBy(x => x).ToArray();
		}

		private void FindDifferences(DirectoryInfo Directory, string Path, ConcurrentBag<string> Paths, ThreadPoolWorkQueue Queue)
		{
			// Recurse through subdirectories
			HashSet<ReadOnlyUtf8String> RemainingSubDirectoryNames = new HashSet<ReadOnlyUtf8String>(NameToSubDirectory.Keys);
			foreach (DirectoryInfo SubDirectory in Directory.EnumerateDirectories())
			{
				WorkspaceDirectoryInfo? StagedSubDirectory;
				if (NameToSubDirectory.TryGetValue(SubDirectory.Name, out StagedSubDirectory))
				{
					RemainingSubDirectoryNames.Remove(SubDirectory.Name);
					Queue.Enqueue(() => StagedSubDirectory.FindDifferences(SubDirectory, String.Format("{0}{1}/", Path, SubDirectory.Name), Paths, Queue));
					continue;
				}
				Paths.Add(String.Format("+{0}{1}/...", Path, SubDirectory.Name));
			}
			foreach (ReadOnlyUtf8String RemainingSubDirectoryName in RemainingSubDirectoryNames)
			{
				Paths.Add(String.Format("-{0}{1}/...", Path, RemainingSubDirectoryName));
			}

			// Search through files
			HashSet<ReadOnlyUtf8String> RemainingFileNames = new HashSet<ReadOnlyUtf8String>(NameToFile.Keys);
			foreach (FileInfo File in Directory.EnumerateFiles())
			{
				WorkspaceFileInfo? StagedFile;
				if (!NameToFile.TryGetValue(File.Name, out StagedFile))
				{
					Paths.Add(String.Format("+{0}{1}", Path, File.Name));
				}
				else if (!StagedFile.MatchesAttributes(File))
				{
					Paths.Add(String.Format("!{0}{1}", Path, File.Name));
					RemainingFileNames.Remove(File.Name);
				}
				else
				{
					RemainingFileNames.Remove(File.Name);
				}
			}
			foreach (ReadOnlyUtf8String RemainingFileName in RemainingFileNames)
			{
				Paths.Add(String.Format("-{0}{1}", Path, RemainingFileName));
			}
		}

		public string GetFullName()
		{
			StringBuilder Builder = new StringBuilder();
			AppendFullPath(Builder);
			return Builder.ToString();
		}

		public DirectoryReference GetLocation()
		{
			return new DirectoryReference(GetFullName());
		}

		public void AppendClientPath(StringBuilder Builder)
		{
			if (ParentDirectory != null)
			{
				ParentDirectory.AppendClientPath(Builder);
				Builder.Append(Name);
				Builder.Append(Path.DirectorySeparatorChar);
			}
		}

		public void AppendFullPath(StringBuilder Builder)
		{
			if (ParentDirectory != null)
			{
				ParentDirectory.AppendFullPath(Builder);
				Builder.Append(Path.DirectorySeparatorChar);
			}
			Builder.Append(Name);
		}

		public override string ToString()
		{
			return GetFullName();
		}
	}

	static class WorkspaceDirectoryInfoExtensions
	{
		public static void ReadWorkspaceDirectoryInfo(this MemoryReader Reader, WorkspaceDirectoryInfo DirectoryInfo)
		{
			int NumFiles = Reader.ReadInt32();
			for (int Idx = 0; Idx < NumFiles; Idx++)
			{
				WorkspaceFileInfo FileInfo = Reader.ReadWorkspaceFileInfo(DirectoryInfo);
				DirectoryInfo.NameToFile.Add(FileInfo.Name, FileInfo);
			}

			int NumSubDirectories = Reader.ReadInt32();
			for (int Idx = 0; Idx < NumSubDirectories; Idx++)
			{
				WorkspaceDirectoryInfo SubDirectory = new WorkspaceDirectoryInfo(DirectoryInfo, Reader.ReadString());
				Reader.ReadWorkspaceDirectoryInfo(SubDirectory);
				DirectoryInfo.NameToSubDirectory[SubDirectory.Name] = SubDirectory;
			}
		}

		public static void WriteWorkspaceDirectoryInfo(this MemoryWriter Writer, WorkspaceDirectoryInfo DirectoryInfo)
		{
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
			return sizeof(int) + DirectoryInfo.NameToFile.Values.Sum(x => x.GetSerializedSize()) + sizeof(int) + DirectoryInfo.NameToSubDirectory.Values.Sum(x => x.Name.GetSerializedSize() + x.GetSerializedSize());
		}
	}
}
