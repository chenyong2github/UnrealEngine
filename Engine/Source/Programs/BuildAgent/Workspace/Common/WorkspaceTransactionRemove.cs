// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent.Workspace.Common
{
	class WorkspaceTransactionRemove
	{
		Dictionary<FileContentId, TrackedFileInfo> ContentIdToTrackedFile;

		public WorkspaceDirectoryInfo NewWorkspaceRootDir;
		public ConcurrentDictionary<FileContentId, WorkspaceFileInfo> FilesToMove = new ConcurrentDictionary<FileContentId, WorkspaceFileInfo>();
		public ConcurrentBag<WorkspaceFileInfo> FilesToDelete = new ConcurrentBag<WorkspaceFileInfo>();
		public ConcurrentBag<WorkspaceDirectoryInfo> DirectoriesToDelete = new ConcurrentBag<WorkspaceDirectoryInfo>();

		public WorkspaceTransactionRemove(WorkspaceDirectoryInfo WorkspaceRootDir, StreamDirectoryInfo StreamRootDir, Dictionary<FileContentId, TrackedFileInfo> ContentIdToTrackedFile)
		{
			this.NewWorkspaceRootDir = new WorkspaceDirectoryInfo(WorkspaceRootDir.GetLocation());
			this.ContentIdToTrackedFile = ContentIdToTrackedFile;

			using(ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
			{
				Queue.Enqueue(() => Merge(WorkspaceRootDir, NewWorkspaceRootDir, StreamRootDir, Queue));
			}
		}

		void Merge(WorkspaceDirectoryInfo WorkspaceDir, WorkspaceDirectoryInfo NewWorkspaceDir, StreamDirectoryInfo StreamDir, ThreadPoolWorkQueue Queue)
		{
			// Update all the subdirectories
			foreach(WorkspaceDirectoryInfo WorkspaceSubDir in WorkspaceDir.NameToSubDirectory.Values)
			{
				StreamDirectoryInfo StreamSubDir;
				if(StreamDir.NameToSubDirectory.TryGetValue(WorkspaceSubDir.Name, out StreamSubDir))
				{
					MergeSubDirectory(NewWorkspaceDir, WorkspaceSubDir, StreamSubDir, Queue);
				}
				else
				{
					RemoveDirectory(WorkspaceSubDir, Queue);
				}
			}

			// Update the staged files
			foreach(WorkspaceFileInfo WorkspaceFile in WorkspaceDir.NameToFile.Values)
			{
				StreamFileInfo StreamFile;
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

		void MergeSubDirectory(WorkspaceDirectoryInfo NewWorkspaceDir, WorkspaceDirectoryInfo WorkspaceSubDir, StreamDirectoryInfo StreamSubDir, ThreadPoolWorkQueue Queue)
		{
			WorkspaceDirectoryInfo NewWorkspaceSubDir = new WorkspaceDirectoryInfo(NewWorkspaceDir, StreamSubDir.Name);
			NewWorkspaceDir.NameToSubDirectory.Add(NewWorkspaceSubDir.Name, NewWorkspaceSubDir);
			Queue.Enqueue(() => Merge(WorkspaceSubDir, NewWorkspaceSubDir, StreamSubDir, Queue));
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
