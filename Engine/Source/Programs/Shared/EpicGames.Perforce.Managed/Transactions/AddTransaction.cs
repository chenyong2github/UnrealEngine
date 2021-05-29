// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Perforce.Managed
{
	class WorkspaceFileToMove
	{
		public StreamFileInfo StreamFile;
		public CachedFileInfo TrackedFile;
		public WorkspaceFileInfo WorkspaceFile;

		public WorkspaceFileToMove(StreamFileInfo StreamFile, CachedFileInfo TrackedFile, WorkspaceFileInfo WorkspaceFile)
		{
			this.StreamFile = StreamFile;
			this.TrackedFile = TrackedFile;
			this.WorkspaceFile = WorkspaceFile;
		}
	}

	class WorkspaceFileToCopy
	{
		public StreamFileInfo StreamFile;
		public WorkspaceFileInfo SourceWorkspaceFile;
		public WorkspaceFileInfo TargetWorkspaceFile;

		public WorkspaceFileToCopy(StreamFileInfo StreamFile, WorkspaceFileInfo SourceWorkspaceFile, WorkspaceFileInfo TargetWorkspaceFile)
		{
			this.StreamFile = StreamFile;
			this.SourceWorkspaceFile = SourceWorkspaceFile;
			this.TargetWorkspaceFile = TargetWorkspaceFile;
		}
	}

	class WorkspaceFileToSync
	{
		public StreamFileInfo StreamFile;
		public WorkspaceFileInfo WorkspaceFile;

		public WorkspaceFileToSync(StreamFileInfo StreamFile, WorkspaceFileInfo WorkspaceFile)
		{
			this.StreamFile = StreamFile;
			this.WorkspaceFile = WorkspaceFile;
		}
	}

	class AddTransaction
	{
		public WorkspaceDirectoryInfo NewWorkspaceRootDir;
		public StreamSnapshot StreamSnapshot;
		public ConcurrentDictionary<CachedFileInfo, WorkspaceFileToMove> FilesToMove = new ConcurrentDictionary<CachedFileInfo, WorkspaceFileToMove>();
		public ConcurrentBag<WorkspaceFileToCopy> FilesToCopy = new ConcurrentBag<WorkspaceFileToCopy>();
		public ConcurrentBag<WorkspaceFileToSync> FilesToSync = new ConcurrentBag<WorkspaceFileToSync>();

		Dictionary<FileContentId, CachedFileInfo> ContentIdToTrackedFile;
		Dictionary<FileContentId, WorkspaceFileInfo> ContentIdToWorkspaceFile = new Dictionary<FileContentId, WorkspaceFileInfo>();

		public AddTransaction(WorkspaceDirectoryInfo WorkspaceRootDir, StreamSnapshot StreamSnapshot, Dictionary<FileContentId, CachedFileInfo> ContentIdToTrackedFile)
		{
			this.StreamSnapshot = StreamSnapshot;
			this.NewWorkspaceRootDir = new WorkspaceDirectoryInfo(WorkspaceRootDir.GetLocation());

			this.ContentIdToTrackedFile = ContentIdToTrackedFile;

			List<WorkspaceFileInfo> WorkspaceFiles = WorkspaceRootDir.GetFiles();
			foreach(WorkspaceFileInfo WorkspaceFile in WorkspaceFiles)
			{
				ContentIdToWorkspaceFile[WorkspaceFile.ContentId] = WorkspaceFile;
			}

			using(ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
			{
				Queue.Enqueue(() => MergeDirectory(WorkspaceRootDir, NewWorkspaceRootDir, StreamSnapshot.Root, Queue));
			}
		}

		void MergeDirectory(WorkspaceDirectoryInfo WorkspaceDir, WorkspaceDirectoryInfo NewWorkspaceDir, Digest<Sha1> StreamDirHash, ThreadPoolWorkQueue Queue)
		{
			// Make sure the directory exists
			Directory.CreateDirectory(WorkspaceDir.GetFullName());

			// Update all the subdirectories
			StreamDirectoryInfo StreamDir = StreamSnapshot.Lookup(StreamDirHash);
			foreach((ReadOnlyUtf8String SubDirName, Digest<Sha1> SubDirHash) in StreamDir.NameToSubDirectory)
			{
				WorkspaceDirectoryInfo? WorkspaceSubDir;
				if(WorkspaceDir.NameToSubDirectory.TryGetValue(SubDirName, out WorkspaceSubDir))
				{
					MergeSubDirectory(SubDirName, WorkspaceSubDir, SubDirHash, NewWorkspaceDir, Queue);
				}
				else
				{
					AddSubDirectory(SubDirName, NewWorkspaceDir, SubDirHash, Queue);
				}
			}

			// Move files into this folder
			foreach(StreamFileInfo StreamFile in StreamDir.NameToFile.Values)
			{
				WorkspaceFileInfo? WorkspaceFile;
				if(WorkspaceDir.NameToFile.TryGetValue(StreamFile.Name.ToString(), out WorkspaceFile))
				{
					NewWorkspaceDir.NameToFile.Add(WorkspaceFile.Name, WorkspaceFile);
				}
				else
				{
					AddFile(NewWorkspaceDir, StreamFile);
				}
			}
		}

		void AddDirectory(WorkspaceDirectoryInfo NewWorkspaceDir, Digest<Sha1> StreamDirHash, ThreadPoolWorkQueue Queue)
		{
			StreamDirectoryInfo StreamDir = StreamSnapshot.Lookup(StreamDirHash);

			// Make sure the directory exists
			Directory.CreateDirectory(NewWorkspaceDir.GetFullName());

			// Add all the sub directories and files
			foreach((ReadOnlyUtf8String SubDirName, Digest<Sha1> SubDirHash) in StreamDir.NameToSubDirectory)
			{
				AddSubDirectory(SubDirName, NewWorkspaceDir, SubDirHash, Queue);
			}
			foreach(StreamFileInfo StreamFile in StreamDir.NameToFile.Values)
			{
				AddFile(NewWorkspaceDir, StreamFile);
			}
		}

		void MergeSubDirectory(ReadOnlyUtf8String Name, WorkspaceDirectoryInfo WorkspaceSubDir, Digest<Sha1> StreamSubDirHash, WorkspaceDirectoryInfo NewWorkspaceDir, ThreadPoolWorkQueue Queue)
		{
			WorkspaceDirectoryInfo NewWorkspaceSubDir = new WorkspaceDirectoryInfo(NewWorkspaceDir, Name, StreamSubDirHash);
			NewWorkspaceDir.NameToSubDirectory.Add(Name, NewWorkspaceSubDir);
			Queue.Enqueue(() => MergeDirectory(WorkspaceSubDir, NewWorkspaceSubDir, StreamSubDirHash, Queue));
		}

		void AddSubDirectory(ReadOnlyUtf8String Name, WorkspaceDirectoryInfo NewWorkspaceDir, Digest<Sha1> StreamSubDirHash, ThreadPoolWorkQueue Queue)
		{
			WorkspaceDirectoryInfo NewWorkspaceSubDir = new WorkspaceDirectoryInfo(NewWorkspaceDir, Name, StreamSubDirHash);
			NewWorkspaceDir.NameToSubDirectory.Add(Name, NewWorkspaceSubDir);
			Queue.Enqueue(() => AddDirectory(NewWorkspaceSubDir, StreamSubDirHash, Queue));
		}

		void AddFile(WorkspaceDirectoryInfo NewWorkspaceDir, StreamFileInfo StreamFile)
		{
			// Create a new file for this workspace
			WorkspaceFileInfo WorkspaceFile = new WorkspaceFileInfo(NewWorkspaceDir, StreamFile.Name.ToString(), StreamFile.ContentId);
			NewWorkspaceDir.NameToFile.Add(StreamFile.Name.ToString(), WorkspaceFile);

			// Try to add it to the cache
			CachedFileInfo? TrackedFile;
			if(ContentIdToTrackedFile.TryGetValue(StreamFile.ContentId, out TrackedFile))
			{
				if(FilesToMove.TryAdd(TrackedFile, new WorkspaceFileToMove(StreamFile, TrackedFile, WorkspaceFile)))
				{
					WorkspaceFile.SetMetadata(TrackedFile.Length, TrackedFile.LastModifiedTicks, TrackedFile.bReadOnly);
				}
				else
				{
					FilesToCopy.Add(new WorkspaceFileToCopy(StreamFile, FilesToMove[TrackedFile].WorkspaceFile, WorkspaceFile));
				}
			}
			else
			{
				WorkspaceFileInfo? SourceWorkspaceFile;
				if(ContentIdToWorkspaceFile.TryGetValue(StreamFile.ContentId, out SourceWorkspaceFile))
				{
					FilesToCopy.Add(new WorkspaceFileToCopy(StreamFile, SourceWorkspaceFile, WorkspaceFile));
				}
				else
				{
					FilesToSync.Add(new WorkspaceFileToSync(StreamFile, WorkspaceFile));
				}
			}
		}
	}
}
