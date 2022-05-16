// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	static class DeleteFilesTask
	{
		public static async Task RunAsync(IPerforceSettings PerforceSettings, List<FileInfo> FilesToSync, List<FileInfo> FilesToDelete, List<DirectoryInfo> DirectoriesToDelete, ILogger Logger, CancellationToken CancellationToken)
		{
			StringBuilder FailMessage = new StringBuilder();

			if(FilesToSync.Count > 0)
			{
				using IPerforceConnection Perforce = await PerforceConnection.CreateAsync(PerforceSettings, Logger);

				List<string> RevisionsToSync = new List<string>();
				foreach(FileInfo FileToSync in FilesToSync)
				{
					RevisionsToSync.Add(String.Format("{0}#have", PerforceUtils.EscapePath(FileToSync.FullName)));
				}

				List<PerforceResponse<SyncRecord>> FailedRecords = await Perforce.TrySyncAsync(SyncOptions.Force, RevisionsToSync, CancellationToken).Where(x => x.Failed).ToListAsync(CancellationToken);
				foreach (PerforceResponse<SyncRecord> FailedRecord in FailedRecords)
				{
					FailMessage.Append(FailedRecord.ToString());
				}
			}

			foreach(FileInfo FileToDelete in FilesToDelete)
			{
				try
				{
					FileToDelete.IsReadOnly = false;
					FileToDelete.Delete();
				}
				catch(Exception Ex)
				{
					Logger.LogWarning(Ex, "Unable to delete {File}", FileToDelete.FullName);
					FailMessage.AppendFormat("{0} ({1})\r\n", FileToDelete.FullName, Ex.Message.Trim());
				}
			}
			foreach(DirectoryInfo DirectoryToDelete in DirectoriesToDelete)
			{
				try
				{
					DirectoryToDelete.Delete(true);
				}
				catch(Exception Ex)
				{
					Logger.LogWarning(Ex, "Unable to delete {Directory}", DirectoryToDelete.FullName);
					FailMessage.AppendFormat("{0} ({1})\r\n", DirectoryToDelete.FullName, Ex.Message.Trim());
				}
			}

			if(FailMessage.Length > 0)
			{
				throw new UserErrorException(FailMessage.ToString());
			}
		}
	}
}
