// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce.Managed;
using HordeCommon.Rpc.Messages;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;
using System.Linq;

namespace HordeAgent.Utility
{
	/// <summary>
	/// Stores information about a managed Perforce workspace
	/// </summary>
	class WorkspaceInfo
	{
		/// <summary>
		/// The perforce connection
		/// </summary>
		public PerforceClientConnection PerforceClient
		{
			get;
		}

		/// <summary>
		/// The Perforce server and port. This is checked to not be null in the constructor.
		/// </summary>
		public string ServerAndPort
		{
			get { return PerforceClient.ServerAndPort!; }
		}

		/// <summary>
		/// The Perforce client name. This is checked to not be null in the constructor.
		/// </summary>
		public string ClientName
		{
			get { return PerforceClient.ClientName!; }
		}

		/// <summary>
		/// The Perforce user name. This is checked to not be null in the constructor.
		/// </summary>
		public string UserName
		{
			get { return PerforceClient.UserName!; }
		}

		/// <summary>
		/// The hostname
		/// </summary>
		public string HostName
		{
			get;
		}

		/// <summary>
		/// The current stream
		/// </summary>
		public string StreamName
		{
			get;
		}

		/// <summary>
		/// The directory containing metadata for this workspace
		/// </summary>
		public DirectoryReference MetadataDir
		{
			get;
		}

		/// <summary>
		/// The directory containing workspace data
		/// </summary>
		public DirectoryReference WorkspaceDir
		{
			get;
		}

		/// <summary>
		/// The view for files to sync
		/// </summary>
		public IReadOnlyList<string> View
		{
			get;
		}			

		/// <summary>
		/// Whether untracked files should be removed from this workspace
		/// </summary>
		public bool bRemoveUntrackedFiles
		{
			get;
		}

		/// <summary>
		/// The managed repository instance
		/// </summary>
		public ManagedWorkspace Repository
		{
			get;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Perforce">The perforce connection</param>
		/// <param name="HostName">Name of this host</param>
		/// <param name="StreamName">Name of the stream to sync</param>
		/// <param name="MetadataDir">Path to the metadata directory</param>
		/// <param name="WorkspaceDir">Path to the workspace directory</param>
		/// <param name="View">View for files to be synced</param>
		/// <param name="bRemoveUntrackedFiles">Whether to remove untracked files when syncing</param>
		/// <param name="Repository">The repository instance</param>
		public WorkspaceInfo(PerforceClientConnection Perforce, string HostName, string StreamName, DirectoryReference MetadataDir, DirectoryReference WorkspaceDir, IList<string>? View, bool bRemoveUntrackedFiles, ManagedWorkspace Repository)
		{
			this.PerforceClient = Perforce;

			if (Perforce.ServerAndPort == null)
			{
				throw new ArgumentException("Perforce connection does not have valid server and port");
			}
			if (Perforce.UserName == null)
			{
				throw new ArgumentException("PerforceConnection does not have valid username");
			}
			if (Perforce.ClientName == null)
			{
				throw new ArgumentException("PerforceConnection does not have valid client name");
			}

			this.HostName = HostName;
			this.StreamName = StreamName;
			this.MetadataDir = MetadataDir;
			this.WorkspaceDir = WorkspaceDir;
			this.View = (View == null) ? new List<string>() : new List<string>(View);
			this.bRemoveUntrackedFiles = bRemoveUntrackedFiles;
			this.Repository = Repository;
		}

		/// <summary>
		/// Creates a new managed workspace
		/// </summary>
		/// <param name="Workspace">The workspace definition</param>
		/// <param name="RootDir">Root directory for storing the workspace</param>
		/// <param name="Logger">Logger output</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>New workspace info</returns>
		public static async Task<WorkspaceInfo> SetupWorkspaceAsync(AgentWorkspace Workspace, DirectoryReference RootDir, ILogger Logger, CancellationToken CancellationToken)
		{
			// Fill in the default credentials iff they are not set
			string? ServerAndPort = String.IsNullOrEmpty(Workspace.ServerAndPort)? null : Workspace.ServerAndPort;
			string? UserName = String.IsNullOrEmpty(Workspace.UserName)? null : Workspace.UserName;
			string? Password = String.IsNullOrEmpty(Workspace.Password)? null : Workspace.Password;

			// Create the connection
			PerforceConnection Perforce = new PerforceConnection(ServerAndPort, UserName, Logger);
			if (UserName != null)
			{
				if (Password != null)
				{
					await Perforce.LoginAsync(Password, CancellationToken);
				}
				else
				{
					Logger.LogInformation($"Using locally logged in session for {UserName}");
				}
			}
			return await SetupWorkspaceAsync(Perforce, Workspace.Stream, Workspace.Identifier, Workspace.View, !Workspace.Incremental, RootDir, Logger, CancellationToken);
		}

		/// <summary>
		/// Creates a new managed workspace
		/// </summary>
		/// <param name="Perforce">The perforce connection to use</param>
		/// <param name="StreamName">The stream being synced</param>
		/// <param name="Identifier">Identifier to use to share </param>
		/// <param name="View">View for this workspace</param>
		/// <param name="bRemoveUntrackedFiles">Whether untracked files should be removed when cleaning this workspace</param>
		/// <param name="RootDir">Root directory for storing the workspace</param>
		/// <param name="Logger">Logger output</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>New workspace info</returns>
		public static async Task<WorkspaceInfo> SetupWorkspaceAsync(PerforceConnection Perforce, string StreamName, string Identifier, IList<string> View, bool bRemoveUntrackedFiles, DirectoryReference RootDir, ILogger Logger, CancellationToken CancellationToken)
		{
			// Get the host name, and fill in any missing metadata about the connection
			InfoRecord Info = await Perforce.GetInfoAsync(InfoOptions.ShortOutput, CancellationToken);

			if (Perforce.ServerAndPort == null)
			{
				Perforce.ServerAndPort = await Perforce.TryGetSettingAsync("p4port", CancellationToken) ?? "perforce:1666";
				Logger.LogInformation($"Using locally configured Perforce server: '{Perforce.ServerAndPort}'");
			}

			if (Perforce.UserName == null)
			{
				Perforce.UserName = Info.UserName;
				Logger.LogInformation("Using locally configured and logged in Perforce user: '{UserName}'", Perforce.UserName);
			}

			string? HostName = Info.ClientHost;
			if(HostName == null)
			{
				throw new Exception("Unable to determine Perforce host name");
			}

			// replace invalid characters in the workspace identifier with a '+' character

			// append the slot index, if it's non-zero
			//			my $slot_idx = $optional_arguments->{'slot_idx'} || 0;
			//			$workspace_identifier .= sprintf("+%02d", $slot_idx) if $slot_idx;

			// if running on an edge server, append the server id to the client name
			string EdgeSuffix = String.Empty;
			if(Info.Services != null && Info.ServerID != null)
			{
				string[] Services = Info.Services.Split(' ', StringSplitOptions.RemoveEmptyEntries);
				if (Services.Any(x => x.Equals("edge-server", StringComparison.OrdinalIgnoreCase)))
				{
					EdgeSuffix = $"+{Info.ServerID}";
				}
			}

			// get all the workspace settings
			string ClientName = $"Horde+{GetNormalizedHostName(HostName)}+{Identifier}{EdgeSuffix}";

			// Create the client Perforce connection
			PerforceClientConnection PerforceClient = new PerforceClientConnection(Perforce.ServerAndPort, Perforce.UserName, ClientName, Perforce.Logger);

			// get the workspace names
			DirectoryReference MetadataDir = DirectoryReference.Combine(RootDir, Identifier);
			DirectoryReference WorkspaceDir = DirectoryReference.Combine(MetadataDir, "Sync");

			// Create the repository
			ManagedWorkspace NewRepository = await ManagedWorkspace.LoadOrCreateAsync(HostName, MetadataDir, true, Logger, CancellationToken);
			if (bRemoveUntrackedFiles)
			{
				await NewRepository.DeleteClientAsync(PerforceClient, CancellationToken);
			}
			await NewRepository.SetupAsync(PerforceClient, StreamName, CancellationToken);

			// Revert any open files
			await NewRepository.RevertAsync(PerforceClient, CancellationToken);

			// Create the workspace info
			return new WorkspaceInfo(PerforceClient, HostName, StreamName, MetadataDir, WorkspaceDir, View, bRemoveUntrackedFiles, NewRepository);
		}

		/// <summary>
		/// Gets the latest change in the stream
		/// </summary>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>Latest changelist number</returns>
		public Task<int> GetLatestChangeAsync(CancellationToken CancellationToken)
		{
			return Repository.GetLatestChangeAsync(PerforceClient, StreamName, CancellationToken);
		}

		/// <summary>
		/// Revert any open files in the workspace and clean it
		/// </summary>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>Async task</returns>
		public async Task CleanAsync(CancellationToken CancellationToken)
		{
			await Repository.RevertAsync(PerforceClient, CancellationToken);
			await Repository.CleanAsync(bRemoveUntrackedFiles, CancellationToken);
		}

		/// <summary>
		/// Removes the given cache file if it's invalid, and updates the metadata to reflect the version about to be synced
		/// </summary>
		/// <param name="CacheFile">Path to the cache file</param>
		/// <param name="Change">The current change being built</param>
		/// <param name="PreflightChange">The preflight changelist number</param>
		/// <returns>Async task</returns>
		public async Task UpdateLocalCacheMarker(FileReference CacheFile, int Change, int PreflightChange)
		{
			// Create the new cache file descriptor
			string NewDescriptor;
			if(PreflightChange <= 0)
			{
				NewDescriptor = $"CL {Change}";
			}
			else
			{
				NewDescriptor = $"CL {PreflightChange} with base CL {Change}";
			}

			// Remove the cache file if the current descriptor doesn't match
			FileReference DescriptorFile = CacheFile.ChangeExtension(".txt");
			if (FileReference.Exists(CacheFile))
			{
				if (FileReference.Exists(DescriptorFile))
				{
					string OldDescriptor = await FileReference.ReadAllTextAsync(DescriptorFile);
					if (OldDescriptor == NewDescriptor)
					{
						return;
					}
					else
					{
						FileReference.Delete(DescriptorFile);
					}
				}
				FileReference.Delete(CacheFile);
			}

			// Write the new descriptor file
			FileReference.WriteAllText(DescriptorFile, NewDescriptor);
		}

		/// <summary>
		/// Sync the workspace to a given changelist, and capture it so we can quickly clean in the future
		/// </summary>
		/// <param name="Change">The changelist to sync to</param>
		/// <param name="PreflightChange">Change to preflight, or 0</param>
		/// <param name="CacheFile">Path to the cache file to use</param>
		/// <param name="Logger">The logger for output messages</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>Async task</returns>
		public async Task SyncAsync(int Change, int PreflightChange, FileReference? CacheFile, ILogger Logger, CancellationToken CancellationToken)
		{
			await Repository.SyncAsync(PerforceClient, StreamName, Change, View, bRemoveUntrackedFiles, false, CacheFile, CancellationToken);

			// Purge the cache for incremental workspaces
			if (!bRemoveUntrackedFiles)
			{
				await Repository.PurgeAsync(0, CancellationToken);
			}

			// if we're running a preflight
			if (PreflightChange > 0)
			{
				await Repository.UnshelveAsync(PerforceClient, StreamName, PreflightChange, CancellationToken);
			}
		}


		/// <summary>
		/// Strip the domain suffix to get the host name
		/// </summary>
		/// <param name="HostName">Hostname with optional domain</param>
		/// <returns>Normalized host name</returns>
		static string GetNormalizedHostName(string HostName)
		{
			return Regex.Replace(HostName, @"\..*$", "").ToUpperInvariant();
		}

		/// <summary>
		/// Revert all files in a workspace
		/// </summary>
		public static async Task RevertAllChangesAsync(PerforceConnection Perforce, ILogger Logger, CancellationToken CancellationToken)
		{
			// Make sure the client name is set
			if(Perforce.ClientName == null)
			{
				throw new ArgumentException("RevertAllChangesAsync() requires PerforceConnection with client");
			}

			// check if there are any open files, and revert them
			List<FStatRecord> Files = await Perforce.GetOpenFilesAsync(OpenedOptions.None, -1, null, null, 1, new[] { "//..." }, CancellationToken);
			if (Files.Count > 0)
			{
				await Perforce.RevertAsync(-1, null, RevertOptions.KeepWorkspaceFiles, new[] { "//..." }, CancellationToken);
			}

			// enumerate all the pending changelists
			List<ChangesRecord> PendingChanges = await Perforce.GetChangesAsync(ChangesOptions.None, Perforce.ClientName, -1, ChangeStatus.Pending, null, FileSpecList.Empty, CancellationToken);
			foreach (ChangesRecord PendingChange in PendingChanges)
			{
				// delete any shelved files if there are any
				List<DescribeRecord> Records = await Perforce.DescribeAsync(DescribeOptions.Shelved, -1, new int[] { PendingChange.Number }, CancellationToken);
				if (Records.Count > 0 && Records[0].Files.Count > 0)
				{
					Logger.LogInformation($"Deleting shelved files in changelist {PendingChange.Number}");
					await Perforce.DeleteChangeAsync(DeleteChangeOptions.None, PendingChange.Number, CancellationToken);
				}

				// delete the changelist
				Logger.LogInformation($"Deleting changelist {PendingChange.Number}");
				await Perforce.DeleteChangeAsync(DeleteChangeOptions.None, PendingChange.Number, CancellationToken);
			}
		}
	}
}
