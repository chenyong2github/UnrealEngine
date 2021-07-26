// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Perforce.Managed
{
	class RemoveTransaction
	{
		Dictionary<FileContentId, CachedFileInfo> ContentIdToTrackedFile;

		public WorkspaceDirectoryInfo NewWorkspaceRootDir;
		public StreamSnapshot StreamSnapshot;
		public ConcurrentDictionary<FileContentId, WorkspaceFileInfo> FilesToMove = new ConcurrentDictionary<FileContentId, WorkspaceFileInfo>();
		public ConcurrentQueue<WorkspaceFileInfo> FilesToDelete = new ConcurrentQueue<WorkspaceFileInfo>();
		public ConcurrentQueue<WorkspaceDirectoryInfo> DirectoriesToDelete = new ConcurrentQueue<WorkspaceDirectoryInfo>();

		public RemoveTransaction(WorkspaceDirectoryInfo WorkspaceRootDir, StreamSnapshot StreamSnapshot, Dictionary<FileContentId, CachedFileInfo> ContentIdToTrackedFile)
		{
			this.NewWorkspaceRootDir = new WorkspaceDirectoryInfo(WorkspaceRootDir.GetLocation());
			this.StreamSnapshot = StreamSnapshot;
			this.ContentIdToTrackedFile = ContentIdToTrackedFile;

			using(ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
			{
				Queue.Enqueue(() => Merge(WorkspaceRootDir, NewWorkspaceRootDir, StreamSnapshot.Root, Queue));
			}
		}

		void Merge(WorkspaceDirectoryInfo WorkspaceDir, WorkspaceDirectoryInfo NewWorkspaceDir, StreamTreeRef StreamTreeRef, ThreadPoolWorkQueue Queue)
		{
			StreamTree StreamDir = StreamSnapshot.Lookup(StreamTreeRef);

			// Update all the subdirectories
			foreach(WorkspaceDirectoryInfo WorkspaceSubDir in WorkspaceDir.NameToSubDirectory.Values)
			{
				StreamTreeRef? StreamSubTreeRef;
				if(StreamDir.NameToTree.TryGetValue(WorkspaceSubDir.Name, out StreamSubTreeRef))
				{
					MergeSubDirectory(WorkspaceSubDir.Name, NewWorkspaceDir, WorkspaceSubDir, StreamSubTreeRef, Queue);
				}
				else
				{
					RemoveDirectory(WorkspaceSubDir, Queue);
				}
			}

			// Update the staged files
			foreach(WorkspaceFileInfo WorkspaceFile in WorkspaceDir.NameToFile.Values)
			{
				StreamFile? StreamFile;
				if(StreamDir != null && StreamDir.NameToFile.TryGetValue(WorkspaceFile.Name, out StreamFile) && StreamFile.ContentId.Equals(WorkspaceFile.ContentId))
				{
					NewWorkspaceDir.NameToFile.Add(WorkspaceFile.Name, WorkspaceFile);
				}
				else
				{
					RemoveFile(WorkspaceFile);
				}
			}
		}

		void MergeSubDirectory(Utf8String Name, WorkspaceDirectoryInfo NewWorkspaceDir, WorkspaceDirectoryInfo WorkspaceSubDir, StreamTreeRef StreamSubTreeRef, ThreadPoolWorkQueue Queue)
		{
			WorkspaceDirectoryInfo NewWorkspaceSubDir = new WorkspaceDirectoryInfo(NewWorkspaceDir, Name, StreamSubTreeRef);
			NewWorkspaceDir.NameToSubDirectory.Add(NewWorkspaceSubDir.Name, NewWorkspaceSubDir);
			Queue.Enqueue(() => Merge(WorkspaceSubDir, NewWorkspaceSubDir, StreamSubTreeRef, Queue));
		}

		void RemoveDirectory(WorkspaceDirectoryInfo WorkspaceDir, ThreadPoolWorkQueue Queue)
		{
			DirectoriesToDelete.Enqueue(WorkspaceDir);

			foreach(WorkspaceDirectoryInfo WorkspaceSubDir in WorkspaceDir.NameToSubDirectory.Values)
			{
				Queue.Enqueue(() => RemoveDirectory(WorkspaceSubDir, Queue));
			}
			foreach(WorkspaceFileInfo WorkspaceFile in WorkspaceDir.NameToFile.Values)
			{
				RemoveFile(WorkspaceFile);
			}
		}

		void RemoveFile(WorkspaceFileInfo WorkspaceFile)
		{
			if(ContentIdToTrackedFile.ContainsKey(WorkspaceFile.ContentId) || !FilesToMove.TryAdd(WorkspaceFile.ContentId, WorkspaceFile))
			{
				FilesToDelete.Enqueue(WorkspaceFile);
			}
		}
	}
}
