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
		public StreamFile StreamFile;
		public CachedFileInfo TrackedFile;
		public WorkspaceFileInfo WorkspaceFile;

		public WorkspaceFileToMove(StreamFile StreamFile, CachedFileInfo TrackedFile, WorkspaceFileInfo WorkspaceFile)
		{
			this.StreamFile = StreamFile;
			this.TrackedFile = TrackedFile;
			this.WorkspaceFile = WorkspaceFile;
		}
	}

	class WorkspaceFileToCopy
	{
		public StreamFile StreamFile;
		public WorkspaceFileInfo SourceWorkspaceFile;
		public WorkspaceFileInfo TargetWorkspaceFile;

		public WorkspaceFileToCopy(StreamFile StreamFile, WorkspaceFileInfo SourceWorkspaceFile, WorkspaceFileInfo TargetWorkspaceFile)
		{
			this.StreamFile = StreamFile;
			this.SourceWorkspaceFile = SourceWorkspaceFile;
			this.TargetWorkspaceFile = TargetWorkspaceFile;
		}
	}

	class WorkspaceFileToSync
	{
		public StreamFile StreamFile;
		public WorkspaceFileInfo WorkspaceFile;

		public WorkspaceFileToSync(StreamFile StreamFile, WorkspaceFileInfo WorkspaceFile)
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
		public ConcurrentQueue<WorkspaceFileToCopy> FilesToCopy = new ConcurrentQueue<WorkspaceFileToCopy>();
		public ConcurrentQueue<WorkspaceFileToSync> FilesToSync = new ConcurrentQueue<WorkspaceFileToSync>();

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

		void MergeDirectory(WorkspaceDirectoryInfo WorkspaceDir, WorkspaceDirectoryInfo NewWorkspaceDir, StreamTreeRef StreamTreeRef, ThreadPoolWorkQueue Queue)
		{
			// Make sure the directory exists
			Directory.CreateDirectory(WorkspaceDir.GetFullName());

			// Update all the subdirectories
			StreamTree StreamDir = StreamSnapshot.Lookup(StreamTreeRef);
			foreach((Utf8String SubDirName, StreamTreeRef SubDirRef) in StreamDir.NameToTree)
			{
				WorkspaceDirectoryInfo? WorkspaceSubDir;
				if(WorkspaceDir.NameToSubDirectory.TryGetValue(SubDirName, out WorkspaceSubDir))
				{
					MergeSubDirectory(SubDirName, WorkspaceSubDir, SubDirRef, NewWorkspaceDir, Queue);
				}
				else
				{
					AddSubDirectory(SubDirName, NewWorkspaceDir, SubDirRef, Queue);
				}
			}

			// Move files into this folder
			foreach((Utf8String Name, StreamFile StreamFile) in StreamDir.NameToFile)
			{
				WorkspaceFileInfo? WorkspaceFile;
				if(WorkspaceDir.NameToFile.TryGetValue(Name, out WorkspaceFile))
				{
					NewWorkspaceDir.NameToFile.Add(WorkspaceFile.Name, WorkspaceFile);
				}
				else
				{
					AddFile(NewWorkspaceDir, Name, StreamFile);
				}
			}
		}

		void AddDirectory(WorkspaceDirectoryInfo NewWorkspaceDir, StreamTreeRef StreamTreeRef, ThreadPoolWorkQueue Queue)
		{
			StreamTree StreamDir = StreamSnapshot.Lookup(StreamTreeRef);

			// Make sure the directory exists
			Directory.CreateDirectory(NewWorkspaceDir.GetFullName());

			// Add all the sub directories and files
			foreach((Utf8String SubDirName, StreamTreeRef SubDirRef) in StreamDir.NameToTree)
			{
				AddSubDirectory(SubDirName, NewWorkspaceDir, SubDirRef, Queue);
			}
			foreach((Utf8String Name, StreamFile StreamFile) in StreamDir.NameToFile)
			{
				AddFile(NewWorkspaceDir, Name, StreamFile);
			}
		}

		void MergeSubDirectory(Utf8String Name, WorkspaceDirectoryInfo WorkspaceSubDir, StreamTreeRef StreamSubTreeRef, WorkspaceDirectoryInfo NewWorkspaceDir, ThreadPoolWorkQueue Queue)
		{
			WorkspaceDirectoryInfo NewWorkspaceSubDir = new WorkspaceDirectoryInfo(NewWorkspaceDir, Name, StreamSubTreeRef);
			NewWorkspaceDir.NameToSubDirectory.Add(Name, NewWorkspaceSubDir);
			Queue.Enqueue(() => MergeDirectory(WorkspaceSubDir, NewWorkspaceSubDir, StreamSubTreeRef, Queue));
		}

		void AddSubDirectory(Utf8String Name, WorkspaceDirectoryInfo NewWorkspaceDir, StreamTreeRef StreamSubTreeRef, ThreadPoolWorkQueue Queue)
		{
			WorkspaceDirectoryInfo NewWorkspaceSubDir = new WorkspaceDirectoryInfo(NewWorkspaceDir, Name, StreamSubTreeRef);
			NewWorkspaceDir.NameToSubDirectory.Add(Name, NewWorkspaceSubDir);
			Queue.Enqueue(() => AddDirectory(NewWorkspaceSubDir, StreamSubTreeRef, Queue));
		}

		void AddFile(WorkspaceDirectoryInfo NewWorkspaceDir, Utf8String Name, StreamFile StreamFile)
		{
			// Create a new file for this workspace
			WorkspaceFileInfo WorkspaceFile = new WorkspaceFileInfo(NewWorkspaceDir, Name, StreamFile.ContentId);
			NewWorkspaceDir.NameToFile.Add(Name, WorkspaceFile);

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
					FilesToCopy.Enqueue(new WorkspaceFileToCopy(StreamFile, FilesToMove[TrackedFile].WorkspaceFile, WorkspaceFile));
				}
			}
			else
			{
				WorkspaceFileInfo? SourceWorkspaceFile;
				if(ContentIdToWorkspaceFile.TryGetValue(StreamFile.ContentId, out SourceWorkspaceFile))
				{
					FilesToCopy.Enqueue(new WorkspaceFileToCopy(StreamFile, SourceWorkspaceFile, WorkspaceFile));
				}
				else
				{
					FilesToSync.Enqueue(new WorkspaceFileToSync(StreamFile, WorkspaceFile));
				}
			}
		}
	}
}
