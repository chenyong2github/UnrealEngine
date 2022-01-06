// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
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
	public static class ConfigUtils
	{
		public static async Task<ConfigFile> ReadProjectConfigFileAsync(IPerforceConnection Perforce, string BranchClientPath, string SelectedClientFileName, DirectoryReference CacheFolder, List<KeyValuePair<FileReference, DateTime>> LocalConfigFiles, ILogger Logger, CancellationToken CancellationToken)
		{
			List<string> ConfigFilePaths = Utility.GetDepotConfigPaths(BranchClientPath + "/Engine", SelectedClientFileName);

			ConfigFile ProjectConfig = new ConfigFile();

			List<PerforceResponse<FStatRecord>> Responses = await Perforce.TryFStatAsync(FStatOptions.IncludeFileSizes, ConfigFilePaths, CancellationToken).ToListAsync(CancellationToken);
			foreach (PerforceResponse<FStatRecord> Response in Responses)
			{
				if (Response.Succeeded)
				{
					string[]? Lines = null;

					// Skip file records which are still in the workspace, but were synced from a different branch. For these files, the action seems to be empty, so filter against that.
					FStatRecord FileRecord = Response.Data;
					if (FileRecord.HeadAction == FileAction.None)
					{
						continue;
					}

					// If this file is open for edit, read the local version
					string? LocalFileName = FileRecord.ClientFile;
					if (LocalFileName != null && File.Exists(LocalFileName) && (File.GetAttributes(LocalFileName) & FileAttributes.ReadOnly) == 0)
					{
						try
						{
							DateTime LastModifiedTime = File.GetLastWriteTimeUtc(LocalFileName);
							LocalConfigFiles.Add(new KeyValuePair<FileReference, DateTime>(new FileReference(LocalFileName), LastModifiedTime));
							Lines = await File.ReadAllLinesAsync(LocalFileName, CancellationToken);
						}
						catch (Exception Ex)
						{
							Logger.LogInformation(Ex, "Failed to read local config file for {Path}", LocalFileName);
						}
					}

					// Otherwise try to get it from perforce
					if (Lines == null && FileRecord.DepotFile != null)
					{
						Lines = await Utility.TryPrintFileUsingCacheAsync(Perforce, FileRecord.DepotFile, CacheFolder, FileRecord.Digest, Logger, CancellationToken);
					}

					// Merge the text with the config file
					if (Lines != null)
					{
						try
						{
							ProjectConfig.Parse(Lines.ToArray());
							Logger.LogInformation("Read config file from {DepotFile}", FileRecord.DepotFile);
						}
						catch (Exception Ex)
						{
							Logger.LogInformation(Ex, "Failed to read config file from {DepotFile}", FileRecord.DepotFile);
						}
					}
				}
			}
			return ProjectConfig;
		}
	}
}
