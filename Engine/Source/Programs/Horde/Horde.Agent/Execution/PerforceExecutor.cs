// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce.Managed;
using HordeAgent.Execution.Interfaces;
using HordeAgent.Parser;
using HordeAgent.Parser.Interfaces;
using HordeAgent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using HordeCommon.Rpc.Messages;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;
using Google.Protobuf.WellKnownTypes;
using OpenTracing;
using OpenTracing.Util;

namespace HordeAgent.Execution
{
	class PerforceExecutor : BuildGraphExecutor
	{
		protected AgentWorkspace? AutoSdkWorkspaceInfo;
		protected AgentWorkspace WorkspaceInfo;
		protected DirectoryReference RootDir;
		protected DirectoryReference? SharedStorageDir;

		protected WorkspaceInfo? AutoSdkWorkspace;
		protected WorkspaceInfo Workspace;

		protected Dictionary<string, string> EnvVars = new Dictionary<string, string>();

		public PerforceExecutor(IRpcConnection RpcConnection, string JobId, string BatchId, string AgentTypeName, AgentWorkspace? AutoSdkWorkspaceInfo, AgentWorkspace WorkspaceInfo, DirectoryReference RootDir)
			: base(RpcConnection, JobId, BatchId, AgentTypeName)
		{
			this.AutoSdkWorkspaceInfo = AutoSdkWorkspaceInfo;
			this.WorkspaceInfo = WorkspaceInfo;
			this.RootDir = RootDir;

			this.Workspace = null!;
		}

		public override async Task InitializeAsync(ILogger Logger, CancellationToken CancellationToken)
		{
			await base.InitializeAsync(Logger, CancellationToken);

			// Setup and sync the autosdk workspace
			if (AutoSdkWorkspaceInfo != null)
			{
				using (IScope Scope = GlobalTracer.Instance.BuildSpan("Workspace").WithResourceName("AutoSDK").StartActive())
				{
					AutoSdkWorkspace = await Utility.WorkspaceInfo.SetupWorkspaceAsync(AutoSdkWorkspaceInfo, RootDir, Logger, CancellationToken);

					DirectoryReference LegacyDir = DirectoryReference.Combine(AutoSdkWorkspace.MetadataDir, "HostWin64");
					if (DirectoryReference.Exists(LegacyDir))
					{
						try
						{
							FileUtils.ForceDeleteDirectory(LegacyDir);
						}
						catch(Exception Ex)
						{
							Logger.LogInformation(Ex, "Unable to delete {Dir}", LegacyDir);
						}
					}

					int AutoSdkChangeNumber = await AutoSdkWorkspace.GetLatestChangeAsync(CancellationToken);

					FileReference AutoSdkCacheFile = FileReference.Combine(AutoSdkWorkspace.MetadataDir, "Contents.dat");
					await AutoSdkWorkspace.UpdateLocalCacheMarker(AutoSdkCacheFile, AutoSdkChangeNumber, -1);
					await AutoSdkWorkspace.SyncAsync(AutoSdkChangeNumber, -1, AutoSdkCacheFile, Logger, CancellationToken);
				}
			}

			using (IScope Scope = GlobalTracer.Instance.BuildSpan("Workspace").WithResourceName(WorkspaceInfo.Identifier).StartActive())
			{
				// Sync the regular workspace
				Workspace = await Utility.WorkspaceInfo.SetupWorkspaceAsync(WorkspaceInfo, RootDir, Logger, CancellationToken);

				// Figure out the change to build
				if (Job.Change == 0)
				{
					List<ChangesRecord> Changes = await Workspace.PerforceClient.GetChangesAsync(ChangesOptions.None, 1, ChangeStatus.Submitted, new[] { Stream.Name + "/..." }, CancellationToken);
					Job.Change = Changes[0].Number;

					UpdateJobRequest UpdateJobRequest = new UpdateJobRequest();
					UpdateJobRequest.JobId = JobId;
					UpdateJobRequest.Change = Job.Change;
					await RpcConnection.InvokeAsync(x => x.UpdateJobAsync(UpdateJobRequest, null, null, CancellationToken), new RpcContext(), CancellationToken);
				}

				// Sync the workspace
				int SyncPreflightChange = (Job.ClonedPreflightChange != 0) ? Job.ClonedPreflightChange : Job.PreflightChange;
				await Workspace.SyncAsync(Job.Change, SyncPreflightChange, null, Logger, CancellationToken);

				// Remove any cached BuildGraph manifests
				DirectoryReference ManifestDir = DirectoryReference.Combine(Workspace.WorkspaceDir, "Engine", "Saved", "BuildGraph");
				if (DirectoryReference.Exists(ManifestDir))
				{
					try
					{
						FileUtils.ForceDeleteDirectoryContents(ManifestDir);
					}
					catch (Exception Ex)
					{
						Logger.LogWarning(Ex, "Unable to delete contents of {ManifestDir}", ManifestDir);
					}
				}
			}

			// Remove all the local settings directories
			DeleteEngineUserSettings(Logger);

			// Get the temp storage directory
			if (!String.IsNullOrEmpty(AgentType!.TempStorageDir))
			{
				string EscapedStreamName = Regex.Replace(Stream!.Name, "[^a-zA-Z0-9_-]", "+");
				SharedStorageDir = DirectoryReference.Combine(new DirectoryReference(AgentType!.TempStorageDir), EscapedStreamName, $"CL {Job!.Change} - Job {JobId}");
				CopyAutomationTool(SharedStorageDir, Workspace.WorkspaceDir, Logger);
			}

			// Get all the environment variables for jobs
			EnvVars["IsBuildMachine"] = "1";
			EnvVars["uebp_LOCAL_ROOT"] = Workspace.WorkspaceDir.FullName;
			EnvVars["uebp_PORT"] = Workspace.ServerAndPort;
			EnvVars["uebp_USER"] = Workspace.UserName;
			EnvVars["uebp_CLIENT"] = Workspace.ClientName;
			EnvVars["uebp_BuildRoot_P4"] = Stream!.Name;
			EnvVars["uebp_BuildRoot_Escaped"] = Stream!.Name.Replace('/', '+');
			EnvVars["uebp_CLIENT_ROOT"] = $"//{Workspace.ClientName}";
			EnvVars["uebp_CL"] = Job!.Change.ToString();
			EnvVars["uebp_CodeCL"] = Job!.CodeChange.ToString();
			EnvVars["P4USER"] = Workspace.UserName;
			EnvVars["P4CLIENT"] = Workspace.ClientName;

			if (AutoSdkWorkspace != null)
			{
				EnvVars["UE_SDKS_ROOT"] = AutoSdkWorkspace.WorkspaceDir.FullName;
			}
		}

		private static void DeleteEngineUserSettings(ILogger Logger)
		{
			if(RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				DirectoryReference? AppDataDir = DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.LocalApplicationData);
				if(AppDataDir != null)
				{
					string[] DirNames = { "Unreal Engine", "UnrealEngine", "UnrealEngineLauncher", "UnrealHeaderTool", "UnrealPak" };
					DeleteEngineUserSettings(AppDataDir, DirNames, Logger);
				}
			}
			else if(RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
			{
				string? HomeDir = Environment.GetEnvironmentVariable("HOME");
				if(!String.IsNullOrEmpty(HomeDir))
				{
					string[] DirNames = { "Library/Preferences/Unreal Engine", "Library/Application Support/Epic" };
					DeleteEngineUserSettings(new DirectoryReference(HomeDir), DirNames, Logger);
				}
			}
		}

		private static void DeleteEngineUserSettings(DirectoryReference BaseDir, string[] SubDirNames, ILogger Logger)
		{
			foreach (string SubDirName in SubDirNames)
			{
				DirectoryReference SettingsDir = DirectoryReference.Combine(BaseDir, SubDirName);
				if (DirectoryReference.Exists(SettingsDir))
				{
					Logger.LogInformation($"Removing local settings directory ({SettingsDir})...");
					try
					{
						FileUtils.ForceDeleteDirectory(SettingsDir);
					}
					catch (Exception Ex)
					{
						Logger.LogWarning(Ex, "Error while deleting directory.");
					}
				}
			}
		}

		protected override async Task<bool> SetupAsync(BeginStepResponse Step, ILogger Logger, CancellationToken CancellationToken)
		{
			await Workspace.Repository.LogFortniteStatsInfoAsync(Workspace.PerforceClient);
			return await SetupAsync(Step, Workspace.WorkspaceDir, SharedStorageDir, EnvVars, Logger, CancellationToken);
		}

		protected override async Task<bool> ExecuteAsync(BeginStepResponse Step, ILogger Logger, CancellationToken CancellationToken)
		{
			await Workspace.Repository.LogFortniteStatsInfoAsync(Workspace.PerforceClient);
			return await ExecuteAsync(Step, Workspace.WorkspaceDir, SharedStorageDir, EnvVars, Logger, CancellationToken);
		}

		public override async Task FinalizeAsync(ILogger Logger, CancellationToken CancellationToken)
		{
			await Workspace.CleanAsync(CancellationToken);
		}

		public static async Task ConformAsync(DirectoryReference RootDir, IList<AgentWorkspace> PendingWorkspaces, ILogger Logger, CancellationToken CancellationToken)
		{
			// Print out all the workspaces we're going to sync
			Logger.LogInformation("Workspaces:");
			foreach (AgentWorkspace PendingWorkspace in PendingWorkspaces)
			{
				Logger.LogInformation("  Identifier={Identifier}, Stream={StreamName}, Incremental={Incremental}", PendingWorkspace.Identifier, PendingWorkspace.Stream, PendingWorkspace.Incremental);
			}

			// Make workspaces for all the unique configurations on this agent
			List<WorkspaceInfo> Workspaces = new List<WorkspaceInfo>();
			foreach (AgentWorkspace PendingWorkspace in PendingWorkspaces)
			{
				WorkspaceInfo Workspace = await Utility.WorkspaceInfo.SetupWorkspaceAsync(PendingWorkspace, RootDir, Logger, CancellationToken);
				Workspaces.Add(Workspace);
			}

			// Find all the unique Perforce servers
			List<PerforceConnection> PerforceConnections = new List<PerforceConnection>();
			foreach (WorkspaceInfo Workspace in Workspaces)
			{
				if (!PerforceConnections.Any(x => x.ServerAndPort!.Equals(Workspace.ServerAndPort, StringComparison.OrdinalIgnoreCase) && x.UserName!.Equals(Workspace.PerforceClient.UserName, StringComparison.Ordinal)))
				{
					PerforceConnections.Add(new PerforceConnection(Workspace.PerforceClient.ServerAndPort, Workspace.PerforceClient.UserName, Workspace.PerforceClient.Logger));
				}
			}

			// Enumerate all the workspaces
			foreach(PerforceConnection Perforce in PerforceConnections)
			{
				// Get the server info
				InfoRecord Info = await Perforce.GetInfoAsync(InfoOptions.ShortOutput, CancellationToken.None);

				// Enumerate all the clients on the server
				List<ClientsRecord> Clients = await Perforce.GetClientsAsync(ClientsOptions.None, null, -1, null, Perforce.UserName, CancellationToken);
				foreach (ClientsRecord Client in Clients)
				{
					// Check the host matches
					if (!String.Equals(Client.Host, Info.ClientHost, StringComparison.OrdinalIgnoreCase))
					{
						continue;
					}

					// Check the edge server id matches
					if(!String.IsNullOrEmpty(Client.ServerID) && !String.Equals(Client.ServerID, Info.ServerID, StringComparison.OrdinalIgnoreCase))
					{
						continue;
					}

					// Check it's under the managed root directory
					DirectoryReference? ClientRoot;
					try
					{
						ClientRoot = new DirectoryReference(Client.Root);
					}
					catch
					{
						ClientRoot = null;
					}

					if (ClientRoot == null || !ClientRoot.IsUnderDirectory(RootDir))
					{
						continue;
					}

					// Check it doesn't match one of the workspaces we want to keep
					if (Workspaces.Any(x => String.Equals(Client.Name, x.ClientName, StringComparison.OrdinalIgnoreCase)))
					{
						continue;
					}

					// Revert all the files in this clientspec and delete it
					Logger.LogInformation("Deleting client {ClientName}...", Client.Name);
					PerforceConnection PerforceClient = new PerforceConnection(Perforce) { ClientName = Client.Name };
					await Utility.WorkspaceInfo.RevertAllChangesAsync(PerforceClient, Logger, CancellationToken);
					await Perforce.DeleteClientAsync(DeleteClientOptions.None, Client.Name, CancellationToken);
				}
			}

			// Delete all the directories that aren't a workspace root
			if (DirectoryReference.Exists(RootDir))
			{
				// Delete all the files in the root
				foreach (FileInfo File in RootDir.ToDirectoryInfo().EnumerateFiles())
				{
					FileUtils.ForceDeleteFile(File);
				}

				// Build a set of directories to protect
				HashSet<DirectoryReference> ProtectDirs = new HashSet<DirectoryReference>();
				ProtectDirs.Add(DirectoryReference.Combine(RootDir, "Temp"));
				ProtectDirs.Add(DirectoryReference.Combine(RootDir, "Saved"));
				ProtectDirs.UnionWith(Workspaces.Select(x => x.MetadataDir));

				// Delete all the directories which aren't a workspace root
				foreach (DirectoryReference Dir in DirectoryReference.EnumerateDirectories(RootDir))
				{
					if (ProtectDirs.Contains(Dir))
					{
						Logger.LogInformation("Keeping directory {KeepDir}", Dir);
					}
					else
					{
						Logger.LogInformation("Deleting directory {DeleteDir}", Dir);
						FileUtils.ForceDeleteDirectory(Dir);
					}
				}
				Logger.LogInformation("");
			}

			// Revert any open files in any workspace
			HashSet<string> RevertedClientNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (WorkspaceInfo Workspace in Workspaces)
			{
				if (RevertedClientNames.Add(Workspace.ClientName))
				{
					await Workspace.Repository.RevertAsync(new PerforceClientConnection(Workspace.PerforceClient, Workspace.ClientName), CancellationToken);
				}
			}

			// Sync all the branches.
			List<Func<Task>> SyncFuncs = new List<Func<Task>>();
			foreach (IGrouping<DirectoryReference, WorkspaceInfo> WorkspaceGroup in Workspaces.GroupBy(x => x.MetadataDir).OrderBy(x => x.Key.FullName))
			{
				List<PopulateRequest> PopulateRequests = new List<PopulateRequest>();
				foreach (WorkspaceInfo Workspace in WorkspaceGroup)
				{
					PerforceClientConnection PerforceClient = new PerforceClientConnection(Workspace.PerforceClient, Workspace.ClientName);
					PopulateRequests.Add(new PopulateRequest(PerforceClient, Workspace.StreamName, Workspace.View));
				}

				ManagedWorkspace Repository = WorkspaceGroup.First().Repository;
				Tuple<int, StreamSnapshot>[] StreamStates = await Repository.PopulateCleanAsync(PopulateRequests, false, CancellationToken);
				SyncFuncs.Add(() => Repository.PopulateSyncAsync(PopulateRequests, StreamStates, false, CancellationToken));
			}
			foreach (Func<Task> SyncFunc in SyncFuncs)
			{
				await SyncFunc();
			}
		}
	}
}
