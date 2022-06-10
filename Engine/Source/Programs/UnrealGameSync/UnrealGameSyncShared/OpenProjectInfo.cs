// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	public class OpenProjectInfo
	{
		public UserSelectedProjectSettings SelectedProject { get; }

		public IPerforceSettings PerforceSettings { get; }
		public ProjectInfo ProjectInfo { get; }
		public UserWorkspaceSettings WorkspaceSettings { get; }
		public UserWorkspaceState WorkspaceState { get; }
		public ConfigFile LatestProjectConfigFile { get; }
		public ConfigFile WorkspaceProjectConfigFile { get; }
		public IReadOnlyList<string>? WorkspaceProjectStreamFilter { get; }
		public List<KeyValuePair<FileReference, DateTime>> LocalConfigFiles { get; }

		public OpenProjectInfo(UserSelectedProjectSettings SelectedProject, IPerforceSettings PerforceSettings, ProjectInfo ProjectInfo, UserWorkspaceSettings WorkspaceSettings, UserWorkspaceState WorkspaceState, ConfigFile LatestProjectConfigFile, ConfigFile WorkspaceProjectConfigFile, IReadOnlyList<string>? WorkspaceProjectStreamFilter, List<KeyValuePair<FileReference, DateTime>> LocalConfigFiles)
		{
			this.SelectedProject = SelectedProject;

			this.PerforceSettings = PerforceSettings;
			this.ProjectInfo = ProjectInfo;
			this.WorkspaceSettings = WorkspaceSettings;
			this.WorkspaceState = WorkspaceState;
			this.LatestProjectConfigFile = LatestProjectConfigFile;
			this.WorkspaceProjectConfigFile = WorkspaceProjectConfigFile;
			this.WorkspaceProjectStreamFilter = WorkspaceProjectStreamFilter;
			this.LocalConfigFiles = LocalConfigFiles;
		}

		public static async Task<OpenProjectInfo> CreateAsync(IPerforceSettings DefaultPerforceSettings, UserSelectedProjectSettings SelectedProject, UserSettings UserSettings, ILogger<OpenProjectInfo> Logger, CancellationToken CancellationToken)
		{
			PerforceSettings PerforceSettings = Utility.OverridePerforceSettings(DefaultPerforceSettings, SelectedProject.ServerAndPort, SelectedProject.UserName);
			using IPerforceConnection Perforce = await PerforceConnection.CreateAsync(PerforceSettings, Logger);

			// Make sure we're logged in
			PerforceResponse<LoginRecord> LoginState = await Perforce.TryGetLoginStateAsync(CancellationToken);
			if (!LoginState.Succeeded)
			{
				throw new UserErrorException("User is not logged in to Perforce.");
			}

			// Execute like a regular task
			return await CreateAsync(Perforce, SelectedProject, UserSettings, Logger, CancellationToken);
		}

		public static async Task<OpenProjectInfo> CreateAsync(IPerforceConnection DefaultConnection, UserSelectedProjectSettings SelectedProject, UserSettings UserSettings, ILogger<OpenProjectInfo> Logger, CancellationToken CancellationToken)
		{
			using IDisposable LoggerScope = Logger.BeginScope("Project {SelectedProject}", SelectedProject.ToString());
			Logger.LogInformation("Detecting settings for {Project}", SelectedProject);

			// Use the cached client path to the file if it's available; it's much quicker than trying to find the correct workspace.
			IPerforceConnection? PerforceClient = null;
			try
			{
				IPerforceSettings PerforceSettings;

				FileReference NewSelectedFileName;
				string NewSelectedClientFileName;
				if (!String.IsNullOrEmpty(SelectedProject.ClientPath))
				{
					// Get the client path
					NewSelectedClientFileName = SelectedProject.ClientPath;

					// Get the client name
					string? ClientName;
					if (!PerforceUtils.TryGetClientName(NewSelectedClientFileName, out ClientName))
					{
						throw new UserErrorException($"Couldn't get client name from {NewSelectedClientFileName}");
					}

					// Create the client
					PerforceSettings = new PerforceSettings(DefaultConnection.Settings) { ClientName = ClientName };
					PerforceClient = await PerforceConnection.CreateAsync(PerforceSettings, Logger);

					// Figure out the path on the client. Use the cached location if it's valid.
					string? LocalPath = SelectedProject.LocalPath;
					if (LocalPath == null || !File.Exists(LocalPath))
					{
						List<WhereRecord> Records = await PerforceClient.WhereAsync(NewSelectedClientFileName, CancellationToken).Where(x => !x.Unmap).ToListAsync(CancellationToken);
						if (Records.Count != 1)
						{
							throw new UserErrorException($"Couldn't get client path for {NewSelectedClientFileName}");
						}
						LocalPath = Path.GetFullPath(Records[0].Path);
					}
					NewSelectedFileName = new FileReference(LocalPath);
				}
				else
				{
					// Get the perforce server settings
					InfoRecord PerforceInfo = await DefaultConnection.GetInfoAsync(InfoOptions.ShortOutput, CancellationToken);

					// Use the path as the selected filename
					NewSelectedFileName = new FileReference(SelectedProject.LocalPath!);

					// Make sure the project exists
					if (!FileReference.Exists(NewSelectedFileName))
					{
						throw new UserErrorException($"{SelectedProject.LocalPath} does not exist.");
					}

					// Find all the clients for this user
					Logger.LogInformation("Enumerating clients for {UserName}...", PerforceInfo.UserName);

					List<ClientsRecord> Clients = await DefaultConnection.GetClientsAsync(ClientsOptions.None, DefaultConnection.Settings.UserName, CancellationToken);

					List<IPerforceSettings> CandidateClients = await FilterClients(Clients, NewSelectedFileName, DefaultConnection.Settings, PerforceInfo.ClientHost, Logger, CancellationToken);
					if (CandidateClients.Count == 0)
					{
						// Search through all workspaces. We may find a suitable workspace which is for any user.
						Logger.LogInformation("Enumerating shared clients...");
						Clients = await DefaultConnection.GetClientsAsync(ClientsOptions.None, "", CancellationToken);

						// Filter this list of clients
						CandidateClients = await FilterClients(Clients, NewSelectedFileName, DefaultConnection.Settings, PerforceInfo.ClientHost, Logger, CancellationToken);

						// If we still couldn't find any, fail.
						if (CandidateClients.Count == 0)
						{
							throw new UserErrorException($"Couldn't find any Perforce workspace containing {NewSelectedFileName}. Check your connection settings.");
						}
					}

					// Check there's only one client
					if (CandidateClients.Count > 1)
					{
						throw new UserErrorException(String.Format("Found multiple workspaces containing {0}:\n\n{1}\n\nCannot determine which to use.", Path.GetFileName(NewSelectedFileName.GetFileName()), String.Join("\n", CandidateClients.Select(x => x.ClientName))));
					}

					// Take the client we've chosen
					PerforceSettings = CandidateClients[0];
					PerforceClient = await PerforceConnection.CreateAsync(PerforceSettings, Logger);

					// Get the client path for the project file
					List<WhereRecord> Records = await PerforceClient.WhereAsync(NewSelectedFileName.FullName, CancellationToken).Where(x => !x.Unmap).ToListAsync(CancellationToken);
					if (Records.Count == 0)
					{
						throw new UserErrorException("File is not mapped to any client");
					}
					else if (Records.Count > 1)
					{
						throw new UserErrorException($"File is mapped to {Records.Count} locations: {String.Join(", ", Records.Select(x => x.Path))}");
					}

					NewSelectedClientFileName = Records[0].ClientFile;
				}

				// Make sure the drive containing the project exists, to prevent other errors down the line
				string PathRoot = Path.GetPathRoot(NewSelectedFileName.FullName)!;
				if (!Directory.Exists(PathRoot))
				{
					throw new UserErrorException($"Path '{NewSelectedFileName}' is invalid");
				}

				// Make sure the path case is correct. This can cause UBT intermediates to be out of date if the case mismatches.
				NewSelectedFileName = FileReference.FindCorrectCase(NewSelectedFileName);

				// Update the selected project with all the data we've found
				SelectedProject = new UserSelectedProjectSettings(SelectedProject.ServerAndPort, SelectedProject.UserName, SelectedProject.Type, NewSelectedClientFileName, NewSelectedFileName.FullName);

				// Get the local branch root
				string? BranchClientPath = null;
				DirectoryReference? BranchDirectoryName = null;

				// Figure out where the engine is in relation to it
				int EndIdx = NewSelectedClientFileName.Length - 1;
				if (EndIdx != -1 && NewSelectedClientFileName.EndsWith(".uproject", StringComparison.InvariantCultureIgnoreCase))
				{
					EndIdx = NewSelectedClientFileName.LastIndexOf('/') - 1;
				}
				for (; EndIdx >= 2; EndIdx--)
				{
					if (NewSelectedClientFileName[EndIdx] == '/')
					{
						List<PerforceResponse<FStatRecord>> FileRecords = await PerforceClient.TryFStatAsync(FStatOptions.None, NewSelectedClientFileName.Substring(0, EndIdx) + "/Engine/Build/Build.version", CancellationToken).ToListAsync();
						if (FileRecords.Succeeded() && FileRecords.Count > 0)
						{
							FStatRecord FileRecord = FileRecords[0].Data;
							if (FileRecord.ClientFile == null)
							{
								throw new UserErrorException($"Missing client path for {FileRecord.DepotFile}");
							}

							BranchClientPath = NewSelectedClientFileName.Substring(0, EndIdx);
							BranchDirectoryName = new FileReference(FileRecord.ClientFile).Directory.ParentDirectory?.ParentDirectory;
							break;
						}
					}
				}
				if(BranchClientPath == null || BranchDirectoryName == null)
				{
					throw new UserErrorException($"Could not find engine in Perforce relative to project path ({NewSelectedClientFileName})");
				}

				Logger.LogInformation("Found branch root at {RootPath}", BranchClientPath);

				// Read the existing workspace settings from disk, and update them with any info computed here
				int BranchIdx = BranchClientPath.IndexOf('/', 2);
				string BranchPath = (BranchIdx == -1) ? String.Empty : BranchClientPath.Substring(BranchIdx);
				string ProjectPath = NewSelectedClientFileName.Substring(BranchClientPath.Length);
				UserWorkspaceSettings UserWorkspaceSettings = UserSettings.FindOrAddWorkspaceSettings(BranchDirectoryName, PerforceSettings.ServerAndPort, PerforceSettings.UserName, PerforceSettings.ClientName!, BranchPath, ProjectPath, Logger);

				// Now compute the updated project info
				ProjectInfo ProjectInfo = await ProjectInfo.CreateAsync(PerforceClient, UserWorkspaceSettings, CancellationToken);

				// Update the cached workspace state
				UserWorkspaceState UserWorkspaceState = UserSettings.FindOrAddWorkspaceState(ProjectInfo, UserWorkspaceSettings, Logger);

				// Read the initial config file
				List<KeyValuePair<FileReference, DateTime>> LocalConfigFiles = new List<KeyValuePair<FileReference, DateTime>>();
				ConfigFile LatestProjectConfigFile = await ConfigUtils.ReadProjectConfigFileAsync(PerforceClient, ProjectInfo, LocalConfigFiles, Logger, CancellationToken);

				// Get the local config file and stream filter
				ConfigFile WorkspaceProjectConfigFile = await WorkspaceUpdate.ReadProjectConfigFile(BranchDirectoryName, NewSelectedFileName, Logger);
				IReadOnlyList<string>? WorkspaceProjectStreamFilter = await WorkspaceUpdate.ReadProjectStreamFilter(PerforceClient, WorkspaceProjectConfigFile, Logger, CancellationToken);

				OpenProjectInfo WorkspaceSettings = new OpenProjectInfo(SelectedProject, PerforceSettings, ProjectInfo, UserWorkspaceSettings, UserWorkspaceState, LatestProjectConfigFile, WorkspaceProjectConfigFile, WorkspaceProjectStreamFilter, LocalConfigFiles);

				return WorkspaceSettings;
			}
			finally
			{
				PerforceClient?.Dispose();
			}
		}

		static async Task<List<IPerforceSettings>> FilterClients(List<ClientsRecord> Clients, FileReference NewSelectedFileName, IPerforceSettings DefaultPerforceSettings, string? HostName, ILogger Logger, CancellationToken CancellationToken)
		{
			List<IPerforceSettings> CandidateClients = new List<IPerforceSettings>();
			foreach(ClientsRecord Client in Clients)
			{
				// Make sure the client is well formed
				if(!String.IsNullOrEmpty(Client.Name) && (!String.IsNullOrEmpty(Client.Host) || !String.IsNullOrEmpty(Client.Owner)) && !String.IsNullOrEmpty(Client.Root))
				{
					// Require either a username or host name match
					if((String.IsNullOrEmpty(Client.Host) || String.Compare(Client.Host, HostName, StringComparison.OrdinalIgnoreCase) == 0) && (String.IsNullOrEmpty(Client.Owner) || String.Compare(Client.Owner, DefaultPerforceSettings.UserName, StringComparison.OrdinalIgnoreCase) == 0))
					{
						if(!Utility.SafeIsFileUnderDirectory(NewSelectedFileName.FullName, Client.Root))
						{
							Logger.LogInformation("Rejecting {ClientName} due to root mismatch ({RootPath})", Client.Name, Client.Root);
							continue;
						}

						PerforceSettings CandidateSettings = new PerforceSettings(DefaultPerforceSettings) { ClientName = Client.Name };
						using IPerforceConnection CandidateClient = await PerforceConnection.CreateAsync(CandidateSettings, Logger);

						List<PerforceResponse<WhereRecord>> WhereRecords = await CandidateClient.TryWhereAsync(NewSelectedFileName.FullName, CancellationToken).Where(x => x.Failed || !x.Data.Unmap).ToListAsync(CancellationToken);
						if(!WhereRecords.Succeeded() || WhereRecords.Count != 1)
						{
							Logger.LogInformation("Rejecting {ClientName} due to file not existing in workspace", Client.Name);
							continue;
						}

						List<PerforceResponse<FStatRecord>> Records = await CandidateClient.TryFStatAsync(FStatOptions.None, NewSelectedFileName.FullName, CancellationToken).ToListAsync(CancellationToken);
						if (!Records.Succeeded())
						{
							Logger.LogInformation("Rejecting {ClientName} due to {FileName} not in depot", Client.Name, NewSelectedFileName);
							continue;
						}

						Records.RemoveAll(x => !x.Data.IsMapped);
						if(Records.Count == 0)
						{
							Logger.LogInformation("Rejecting {ClientName} due to {NumRecords} matching records", Client.Name, Records.Count);
							continue;
						}

						Logger.LogInformation("Found valid client {ClientName}", Client.Name);
						CandidateClients.Add(CandidateSettings);
					}
				}
			}
			return CandidateClients;
		}
	}
}
