// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent.Workspace.Common
{
	/// <summary>
	/// Stores information about a file that has been staged into a workspace
	/// </summary>
	class WorkspaceFileInfo
	{
		public readonly WorkspaceDirectoryInfo Directory;
		public readonly string Name;
		public long Length;
		public long LastModifiedTicks;
		public bool bReadOnly;
		public readonly FileContentId ContentId;

		public WorkspaceFileInfo(WorkspaceDirectoryInfo Directory, string Name, FileContentId ContentId)
		{
			this.Directory = Directory;
			this.Name = Name;
			this.ContentId = ContentId;
		}

		public WorkspaceFileInfo(WorkspaceDirectoryInfo Directory, string Name, FileInfo Info, FileContentId ContentId)
			: this(Directory, Name, Info.Length, Info.LastWriteTimeUtc.Ticks, Info.Attributes.HasFlag(FileAttributes.ReadOnly), ContentId)
		{
		}

		public WorkspaceFileInfo(WorkspaceDirectoryInfo Directory, string Name, long Length, long LastModifiedTicks, bool bReadOnly, FileContentId ContentId)
		{
			this.Directory = Directory;
			this.Name = Name;
			this.Length = Length;
			this.LastModifiedTicks = LastModifiedTicks;
			this.bReadOnly = bReadOnly;
			this.ContentId = ContentId;
		}

		public WorkspaceFileInfo(WorkspaceDirectoryInfo Directory, BinaryReader Reader)
		{
			this.Directory = Directory;
			this.Name = Reader.ReadString();
			this.Length = Reader.ReadInt64();
			this.LastModifiedTicks = Reader.ReadInt64();
			this.bReadOnly = Reader.ReadBoolean();
			this.ContentId = Reader.ReadObject<FileContentId>();
		}

		public void SetMetadata(long Length, long LastModifiedTicks, bool bReadOnly)
		{
			this.Length = Length;
			this.LastModifiedTicks = LastModifiedTicks;
			this.bReadOnly = bReadOnly;
		}

		public void UpdateMetadata()
		{
			FileInfo Info = new FileInfo(GetFullName());
			if(Info.Exists)
			{
				Length = Info.Length;
				LastModifiedTicks = Info.LastWriteTimeUtc.Ticks;
				bReadOnly = Info.Attributes.HasFlag(FileAttributes.ReadOnly);
			}
		}

		public bool MatchesAttributes(FileInfo Info)
		{
			return Length == Info.Length && LastModifiedTicks == Info.LastWriteTimeUtc.Ticks && (Info.Attributes.HasFlag(FileAttributes.ReadOnly) == bReadOnly);
		}

		public void Write(BinaryWriter Writer)
		{
			Writer.Write(Name);
			Writer.Write(Length);
			Writer.Write(LastModifiedTicks);
			Writer.Write(bReadOnly);
			Writer.Write(ContentId);
		}

		public string GetClientPath()
		{
			StringBuilder Builder = new StringBuilder();
			Directory.AppendClientPath(Builder);
			Builder.Append(Name);
			return Builder.ToString();
		}

		public string GetFullName()
		{
			StringBuilder Builder = new StringBuilder();
			Directory.AppendFullPath(Builder);
			Builder.Append(Path.DirectorySeparatorChar);
			Builder.Append(Name);
			return Builder.ToString();
		}

		public FileReference GetLocation()
		{
			return new FileReference(GetFullName());
		}

		public override string ToString()
		{
			return GetFullName();
		}
	}

	/// <summary>
	/// 
	/// </summary>
	class WorkspaceDirectoryInfo
	{
		public readonly WorkspaceDirectoryInfo ParentDirectory;
		public readonly string Name;
		public Dictionary<string, WorkspaceFileInfo> NameToFile;
		public Dictionary<string, WorkspaceDirectoryInfo> NameToSubDirectory;

		public WorkspaceDirectoryInfo(DirectoryReference RootDir)
			: this(null, RootDir.FullName)
		{
		}

		public WorkspaceDirectoryInfo(WorkspaceDirectoryInfo ParentDirectory, string Name)
		{
			this.ParentDirectory = ParentDirectory;
			this.Name = Name;
			this.NameToFile = new Dictionary<string, WorkspaceFileInfo>(StringComparer.Ordinal);
			this.NameToSubDirectory = new Dictionary<string, WorkspaceDirectoryInfo>(FileUtils.PlatformPathComparer);
		}

		public void Read(BinaryReader Reader)
		{
			WorkspaceFileInfo[] Files = new WorkspaceFileInfo[Reader.ReadInt32()];
			for(int Idx = 0; Idx < Files.Length; Idx++)
			{
				Files[Idx] = new WorkspaceFileInfo(this, Reader);
			}
			NameToFile = Files.ToDictionary(x => x.Name, x => x, StringComparer.Ordinal);

			int NumSubDirectories = Reader.ReadInt32();
			for(int Idx = 0; Idx < NumSubDirectories; Idx++)
			{
				WorkspaceDirectoryInfo SubDirectory = new WorkspaceDirectoryInfo(this, Reader.ReadString());
				SubDirectory.Read(Reader);
				NameToSubDirectory[SubDirectory.Name] = SubDirectory;
			}
		}

		public void Write(BinaryWriter Writer)
		{
			Writer.Write(NameToFile.Count);
			foreach(WorkspaceFileInfo File in NameToFile.Values)
			{
				File.Write(Writer);
			}

			Writer.Write(NameToSubDirectory.Count);
			foreach(WorkspaceDirectoryInfo SubDirectory in NameToSubDirectory.Values)
			{
				Writer.Write(SubDirectory.Name);
				SubDirectory.Write(Writer);
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

			foreach(KeyValuePair<string, WorkspaceDirectoryInfo> Pair in NameToSubDirectory)
			{
				Pair.Value.GetFilesInternal(Files);
			}
		}

		public void Refresh(out FileInfo[] FilesToDelete, out DirectoryInfo[] DirectoriesToDelete)
		{
			ConcurrentBag<FileInfo> ConcurrentFilesToDelete = new ConcurrentBag<FileInfo>();
			ConcurrentBag<DirectoryInfo> ConcurrentDirectoriesToDelete = new ConcurrentBag<DirectoryInfo>();
			using(ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
			{
				Queue.Enqueue(() => Refresh(new DirectoryInfo(GetFullName()), ConcurrentFilesToDelete, ConcurrentDirectoriesToDelete, Queue));
			}
			DirectoriesToDelete = ConcurrentDirectoriesToDelete.ToArray();
			FilesToDelete = ConcurrentFilesToDelete.ToArray();
		}

		private void Refresh(DirectoryInfo Info, ConcurrentBag<FileInfo> FilesToDelete, ConcurrentBag<DirectoryInfo> DirectoriesToDelete, ThreadPoolWorkQueue Queue)
		{
			// Recurse through subdirectories
			Dictionary<string, WorkspaceDirectoryInfo> NewNameToSubDirectory = new Dictionary<string, WorkspaceDirectoryInfo>(NameToSubDirectory.Count, NameToSubDirectory.Comparer);
			foreach(DirectoryInfo SubDirectoryInfo in Info.EnumerateDirectories())
			{
				WorkspaceDirectoryInfo SubDirectory;
				if(NameToSubDirectory.TryGetValue(SubDirectoryInfo.Name, out SubDirectory))
				{
					NewNameToSubDirectory.Add(SubDirectory.Name, SubDirectory);
					Queue.Enqueue(() => SubDirectory.Refresh(SubDirectoryInfo, FilesToDelete, DirectoriesToDelete, Queue));
				}
				else
				{
					DirectoriesToDelete.Add(SubDirectoryInfo);
				}
			}
			NameToSubDirectory = NewNameToSubDirectory;
			
			// Figure out which files have changed.
			Dictionary<string, WorkspaceFileInfo> NewNameToFile = new Dictionary<string, WorkspaceFileInfo>(NameToFile.Count, NameToFile.Comparer);
			foreach(FileInfo File in Info.EnumerateFiles())
			{
				WorkspaceFileInfo StagedFile;
				if(NameToFile.TryGetValue(File.Name, out StagedFile) && StagedFile.MatchesAttributes(File))
				{
					NewNameToFile.Add(StagedFile.Name, StagedFile);
				}
				else
				{
					FilesToDelete.Add(File);
				}
			}
			NameToFile = NewNameToFile;
		}

		public string[] FindDifferences()
		{
			ConcurrentBag<string> Paths = new ConcurrentBag<string>();
			using(ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
			{
				Queue.Enqueue(() => FindDifferences(new DirectoryInfo(GetFullName()), "/", Paths, Queue));
			}
			return Paths.OrderBy(x => x).ToArray();
		}

		private void FindDifferences(DirectoryInfo Directory, string Path, ConcurrentBag<string> Paths, ThreadPoolWorkQueue Queue)
		{
			// Recurse through subdirectories
			HashSet<string> RemainingSubDirectoryNames = new HashSet<string>(NameToSubDirectory.Keys);
			foreach(DirectoryInfo SubDirectory in Directory.EnumerateDirectories())
			{
				WorkspaceDirectoryInfo StagedSubDirectory;
				if(NameToSubDirectory.TryGetValue(SubDirectory.Name, out StagedSubDirectory))
				{
					RemainingSubDirectoryNames.Remove(SubDirectory.Name);
					Queue.Enqueue(() => StagedSubDirectory.FindDifferences(SubDirectory, String.Format("{0}{1}/", Path, SubDirectory.Name), Paths, Queue));
					continue;
				}
				Paths.Add(String.Format("+{0}{1}/...", Path, SubDirectory.Name));
			}
			foreach(string RemainingSubDirectoryName in RemainingSubDirectoryNames)
			{
				Paths.Add(String.Format("-{0}{1}/...", Path, RemainingSubDirectoryName));
			}

			// Search through files
			HashSet<string> RemainingFileNames = new HashSet<string>(NameToFile.Keys);
			foreach(FileInfo File in Directory.EnumerateFiles())
			{
				WorkspaceFileInfo StagedFile;
				if(!NameToFile.TryGetValue(File.Name, out StagedFile))
				{
					Paths.Add(String.Format("+{0}{1}", Path, File.Name));
				}
				else if(!StagedFile.MatchesAttributes(File))
				{
					Paths.Add(String.Format("!{0}{1}", Path, File.Name));
					RemainingFileNames.Remove(File.Name);
				}
				else
				{
					RemainingFileNames.Remove(File.Name);
				}
			}
			foreach(string RemainingFileName in RemainingFileNames)
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
			if(ParentDirectory != null)
			{
				ParentDirectory.AppendClientPath(Builder);
				Builder.Append(Name);
				Builder.Append(Path.DirectorySeparatorChar);
			}
		}

		public void AppendFullPath(StringBuilder Builder)
		{
			if(ParentDirectory != null)
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
}
