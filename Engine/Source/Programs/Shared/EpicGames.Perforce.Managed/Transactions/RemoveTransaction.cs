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
		public ConcurrentBag<WorkspaceFileInfo> FilesToDelete = new ConcurrentBag<WorkspaceFileInfo>();
		public ConcurrentBag<WorkspaceDirectoryInfo> DirectoriesToDelete = new ConcurrentBag<WorkspaceDirectoryInfo>();

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

		void Merge(WorkspaceDirectoryInfo WorkspaceDir, WorkspaceDirectoryInfo NewWorkspaceDir, Digest<Sha1> StreamDirHash, ThreadPoolWorkQueue Queue)
		{
			StreamDirectoryInfo StreamDir = StreamSnapshot.Lookup(StreamDirHash);

			// Update all the subdirectories
			foreach(WorkspaceDirectoryInfo WorkspaceSubDir in WorkspaceDir.NameToSubDirectory.Values)
			{
				Digest<Sha1> StreamSubDirHash;
				if(StreamDir.NameToSubDirectory.TryGetValue(WorkspaceSubDir.Name, out StreamSubDirHash))
				{
					MergeSubDirectory(WorkspaceSubDir.Name, NewWorkspaceDir, WorkspaceSubDir, StreamSubDirHash, Queue);
				}
				else
				{
					RemoveDirectory(WorkspaceSubDir, Queue);
				}
			}

			// Update the staged files
			foreach(WorkspaceFileInfo WorkspaceFile in WorkspaceDir.NameToFile.Values)
			{
				StreamFileInfo? StreamFile;
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

		void MergeSubDirectory(ReadOnlyUtf8String Name, WorkspaceDirectoryInfo NewWorkspaceDir, WorkspaceDirectoryInfo WorkspaceSubDir, Digest<Sha1> StreamSubDirHash, ThreadPoolWorkQueue Queue)
		{
			WorkspaceDirectoryInfo NewWorkspaceSubDir = new WorkspaceDirectoryInfo(NewWorkspaceDir, Name);
			NewWorkspaceDir.NameToSubDirectory.Add(NewWorkspaceSubDir.Name, NewWorkspaceSubDir);
			Queue.Enqueue(() => Merge(WorkspaceSubDir, NewWorkspaceSubDir, StreamSubDirHash, Queue));
		}

		void RemoveDirectory(WorkspaceDirectoryInfo WorkspaceDir, ThreadPoolWorkQueue Queue)
		{
			DirectoriesToDelete.Add(WorkspaceDir);

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
				FilesToDelete.Add(WorkspaceFile);
			}
		}
	}
}
