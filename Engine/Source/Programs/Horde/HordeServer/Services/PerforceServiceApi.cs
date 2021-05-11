// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System.Linq;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Net;
using System.Text;
using System.Threading.Tasks;
using System.IO;

using P4 = Perforce.P4;
using Microsoft.Extensions.Options;
using System.Runtime.InteropServices;
using System.Globalization;
using System.Text.RegularExpressions;

namespace HordeServer.Services
{
	/// <summary>
	/// P4API implementation of the Perforce service
	/// </summary>
	public class P4APIPerforceService : PerforceServiceBase
	{

		IOptionsMonitor<ServerSettings> Settings;

		ILogger<P4APIPerforceService> Logger;

		P4.Server Server;

		/// <summary>
		/// Object used for controlling access to the access user tickets
		/// </summary>
		object TicketLock = new object();

		Dictionary<string, P4.Credential> UserTickets = new Dictionary<string, P4.Credential>();

		/// <summary>
		/// Constructor
		/// </summary>
		public P4APIPerforceService(IOptionsMonitor<ServerSettings> Settings, ILogger<P4APIPerforceService> Logger)
		{
			this.Settings = Settings;
			this.Logger = Logger;

			Server = new P4.Server(new P4.ServerAddress(Settings.CurrentValue.P4BridgeServer));

			P4.LogFile.SetLoggingFunction((int LogLevel, String Source, String Message) =>
			{

				switch (LogLevel)
				{
					case 0:
					case 1:
						Serilog.Log.Error("Perforce (Error): {Message} {Source} : ", Message, Source);
						break;
					case 2:
						Serilog.Log.Warning("Perforce (Warning): {Message} {Source} : ", Message, Source);
						break;
					case 3:
						Serilog.Log.Information("Perforce: {Message} {Source} : ", Message, Source);
						break;
					default:
						Serilog.Log.Debug("Perforce (Debug): {Message} {Source} : ", Message, Source);
						break;
				};

			});
		}


		P4.Repository GetServiceUserConnection()
		{

			ServerSettings Settings = this.Settings.CurrentValue;

			P4.Repository Repository = new P4.Repository(Server);

			try
			{
				P4.Connection Connection = Repository.Connection;
				Connection.UserName = Settings.P4BridgeServiceUsername;

				P4.Options Options = new P4.Options();

				Options["Ticket"] = Settings.P4BridgeServicePassword;

				// connect to the server
				if (!Connection.Connect(Options))
				{
					throw new Exception("Unable to get P4 server connection");
				}

			}
			catch
			{
				Repository.Dispose();
				throw;
			}


			return Repository;
		}

		P4.Credential GetImpersonateCredential(string ImpersonateUser)
		{

			if (!CanImpersonate)
			{
				throw new Exception($"Service account required to impersonate user {ImpersonateUser}");
			}

			lock (TicketLock)
			{

				ServerSettings Settings = this.Settings.CurrentValue;

				// Check if we have a ticket
				P4.Credential? Credential = null;

				if (UserTickets.TryGetValue(ImpersonateUser, out Credential))
				{
					// if the credential expires within the next 15 minutes, refresh
					TimeSpan Time = new TimeSpan(0, 15, 0);
					if (Credential.Expires.Subtract(Time) <= DateTime.UtcNow)
					{
						UserTickets.Remove(ImpersonateUser);
						Credential = null;
					}
				}

				if (Credential != null)
				{
					return Credential;
				}

				using (P4.Repository Repository = GetServiceUserConnection())
				{
					Credential = Repository.Connection.Login(null, new P4.LoginCmdOptions(P4.LoginCmdFlags.AllHosts | P4.LoginCmdFlags.DisplayTicket, null), ImpersonateUser);
				}

				if (Credential == null)
				{
					throw new Exception($"GetImpersonateCredential - Unable to get impersonation credential for {ImpersonateUser} from {Dns.GetHostName()}");
				}

				UserTickets.Add(ImpersonateUser, Credential);

				return Credential;
			}

		}

		P4.Repository GetImpersonateConnection(string ImpersonateUser)
		{

			if (!CanImpersonate)
			{
				throw new Exception($"Service account required to impersonate user {ImpersonateUser}");
			}

			P4.Credential Credential = GetImpersonateCredential(ImpersonateUser);

			// this might be able to be reused and would be threaded internally
			P4.Repository Repository = new P4.Repository(Server);

			P4.Connection Connection = Repository.Connection;
			Connection.UserName = ImpersonateUser;

			P4.Options Options = new P4.Options();
			Options["Ticket"] = Credential.Ticket;

			// connect to the server
			if (!Connection.Connect(Options))
			{
				throw new Exception($"GetImpersonateConnection - Unable to get impersonated P4 connection for {ImpersonateUser} from {Dns.GetHostName()}");

			}

			return Repository;
		}

		P4.Repository GetConnection(string? Stream = null, string? Username = null, bool ReadOnly = true, bool CreateChange = false, int? ClientFromChange = null, bool UseClientFromChange = false, bool UsePortFromChange = false, string? ClientId = null, bool NoClient = false)
		{
			ServerSettings Settings = this.Settings.CurrentValue;

			P4.Repository? Repository = (Username == null || Settings.P4BridgeServiceUsername == Username || !CanImpersonate) ? GetServiceUserConnection() : GetImpersonateConnection(Username);

			if (!NoClient)
			{
				try
				{
					if (ClientId != null)
					{
						Repository.Connection.SetClient(ClientId);
					}
					else
					{
						P4.Client Client = GetOrCreateClient(Repository, Stream, Username, ReadOnly, CreateChange, ClientFromChange, UseClientFromChange, UsePortFromChange);
						Repository.Connection.SetClient(Client.Name);

						if (UseClientFromChange)
						{
							string? ClientHost = Repository.Connection.Client.Host;
							if (!string.IsNullOrEmpty(ClientHost))
							{
								Repository.Connection.getP4Server().SetConnectionHost(ClientHost);
							}
						}
					}
				}
				catch
				{
					Repository.Dispose();
					throw;
				}
			}

			if (Repository == null)
			{
				throw new Exception($"Unable to get connection for Stream:{Stream} Username:{Username} ReadOnly:{ReadOnly} CreateChange:{CreateChange} ClientFromChange:{ClientFromChange} UseClientFromChange:{UseClientFromChange} UsePortFromChange:{UsePortFromChange}");
			}

			Repository.Connection.CommandEcho += (Data) => { Serilog.Log.Information(Data);};

			return Repository;

		}

		string GetClientName(string Stream, bool ReadOnly, bool CreateChange, string? Username = null)
		{
			ServerSettings Settings = this.Settings.CurrentValue;

			string ClientName = $"horde-p4bridge-{Dns.GetHostName()}-{Settings.P4BridgeServiceUsername}-{Stream.Replace("/", "+", StringComparison.OrdinalIgnoreCase)}";

			if (!ReadOnly)
			{
				ClientName += "-write";
			}

			if (CreateChange)
			{
				ClientName += "-create";
			}

			if (!string.IsNullOrEmpty(Username))
			{
				ClientName += "-" + Username;
			}

			return ClientName;

		}

		P4.Client GetOrCreateClient(P4.Repository Repository, string? Stream, string? Username = null, bool ReadOnly = true, bool CreateChange = false, int? ClientFromChange = null, bool UseClientFromChange = false, bool UsePortFromChange = false)
		{
			ServerSettings Settings = this.Settings.CurrentValue;

			P4.Client? Client = null;
			P4.Changelist? Changelist = null;

			string? ClientPort = null;
			string? ClientHost = null;

			if (ClientFromChange.HasValue)
			{
				Changelist = Repository.GetChangelist(ClientFromChange.Value);

				if (Changelist == null)
				{
					throw new Exception($"Unable to get changelist for client {ClientFromChange}");
				}

				Client = Repository.GetClient(Changelist.ClientId);

				if (Client == null)
				{
					throw new Exception($"Unable to get client for id {Changelist.ClientId}");
				}

				if (UsePortFromChange)
				{
					if (!Client.Name.StartsWith("horde-p4bridge-", StringComparison.OrdinalIgnoreCase))
					{
						using (P4.P4Command Cmd = new P4.P4Command(Repository, "info", true))
						{
							P4.P4CommandResult Results = Cmd.Run();

							if (!Results.Success)
							{
								throw new Exception("Unable to get server info");
							}

							P4.TaggedObject? Tag = Results.TaggedOutput.Find(T => T.ContainsKey("changeServer"));

							if (Tag != null)
							{
								if (!Tag.TryGetValue("changeServer", out ClientPort))
								{
									throw new Exception("Unable to get change server from tagged server info output");
								}
							}
						}
					}
				}

				Stream = Client.Stream;
				Username = Client.OwnerName;
				ClientHost = Client.Host;


			}

			if (!UseClientFromChange)
			{
				if (Stream == null)
				{
					throw new Exception("Stream required for client");

				}
				string ClientName = GetClientName(Stream, ReadOnly, CreateChange, Username);

				IList<P4.Client>? Clients = Repository.GetClients(new P4.ClientsCmdOptions(P4.ClientsCmdFlags.None, Username, ClientName, 1, Stream));

				if (Clients != null && Clients.Count == 1)
				{
					Client = Clients[0];
					if (!string.IsNullOrEmpty(Client.Host))
					{
						Client.Host = string.Empty;
						Repository.UpdateClient(Client);
					}
				}
				else
				{

					P4.Client NewClient = new P4.Client();

					NewClient.Name = ClientName;
					NewClient.Root = RuntimeInformation.IsOSPlatform(OSPlatform.Linux) ? $"/tmp/{ClientName}/" : $"{Path.GetTempPath()}{ClientName}\\";
					NewClient.Stream = Stream;
					if (ReadOnly)
					{
						NewClient.ClientType = P4.ClientType.@readonly;
					}

					NewClient.OwnerName = Username ?? Settings.P4BridgeServiceUsername;

					Client = Repository.CreateClient(NewClient);

					if (Client == null)
					{
						throw new Exception($"Unable to create client for ${Stream} : {Username ?? Settings.P4BridgeServiceUsername}");
					}
				}
			}

			if (Client == null)
			{
				throw new Exception($"Unable to create client for Stream:{Stream} Username:{Username} ReadOnly:{ReadOnly} CreateChange:{CreateChange} ClientFromChange:{ClientFromChange} UseClientFromChange:{UseClientFromChange} UsePortFromChange:{UsePortFromChange}");
			}

			return Client;

		}

		/// <inheritdoc/>
		public override async Task<List<ChangeSummary>> GetChangesAsync(string StreamName, int? MinChange, int? MaxChange, int Results, string? ImpersonateUser)
		{

			await Task.Yield();

			using (P4.Repository Repository = GetConnection(StreamName, ImpersonateUser))
			{

				P4.ChangesCmdOptions Options = new P4.ChangesCmdOptions(P4.ChangesCmdFlags.IncludeTime | P4.ChangesCmdFlags.FullDescription, null, Results, P4.ChangeListStatus.Submitted, null);

				string Filter = GetFilter($"//{Repository.Connection.Client.Name}/...", MinChange, MaxChange);

				P4.FileSpec FileSpec = new P4.FileSpec(new P4.DepotPath(Filter), null, null, null);

				IList<P4.Changelist> Changelists = Repository.GetChangelists(Options, FileSpec);

				List<ChangeSummary> Changes = new List<ChangeSummary>();
				foreach (P4.Changelist Changelist in Changelists)
				{
					Changes.Add(new ChangeSummary(Changelist.Id, Changelist.OwnerName, Changelist.Description));
				}

				return Changes;
			}

		}

		/// <inheritdoc/>
		public override async Task<List<ChangeDetails>> GetChangeDetailsAsync(string StreamName, IReadOnlyList<int> ChangeNumbers, string? ImpersonateUser)
		{
			await Task.Yield();

			using (P4.Repository Repository = GetConnection(StreamName, ImpersonateUser))
			{
				List<ChangeDetails> Results = new List<ChangeDetails>();

				List<string> Args = new List<string> { "-s", "-S" };
				Args.AddRange(ChangeNumbers.Select(Change => Change.ToString(CultureInfo.InvariantCulture)));

				using (P4.P4Command Command = new P4.P4Command(Repository, "describe", true, Args.ToArray()))
				{
					P4.P4CommandResult Result = Command.Run();

					if (!Result.Success)
					{
						throw new Exception("Unable to get changes");
					}

					if (Result.TaggedOutput == null || Result.TaggedOutput.Count <= 0)
					{
						return Results;
					}

					List<P4.Changelist> Changelists = new List<P4.Changelist>() { };

					bool DSTMismatch = false;
					string Offset = string.Empty;

					if (Server != null && Server.Metadata != null)
					{
						Offset = Server.Metadata.DateTimeOffset;
						DSTMismatch = P4.FormBase.DSTMismatch(Server.Metadata);
					}

					foreach (P4.TaggedObject TaggedObject in Result.TaggedOutput)
					{
						List<string> Files = new List<string>();

						P4.Changelist Change = new P4.Changelist();
						Change.FromChangeCmdTaggedOutput(TaggedObject, true, Offset, DSTMismatch);

						foreach (P4.FileMetaData DescribeFile in Change.Files)
						{
							string? RelativePath;
							if (TryGetStreamRelativePath(DescribeFile.DepotPath.Path, StreamName, out RelativePath))
							{
								Files.Add(RelativePath);
							}
						}

						if (Change.ShelvedFiles != null && Change.ShelvedFiles.Count > 0)
						{
							foreach (P4.ShelvedFile ShelvedFile in Change.ShelvedFiles)
							{
								string? RelativePath;
								if (TryGetStreamRelativePath(ShelvedFile.Path.ToString(), StreamName, out RelativePath))
								{
									Files.Add(RelativePath);
								}
							}

						}

						Results.Add(new ChangeDetails(Change.Id, Change.OwnerName, Change.Description, Files));
					}

				}

				return Results;
			}
		}

		/// <inheritdoc />
		public override async Task<string> CreateTicket(string ImpersonateUser)
		{
			await Task.Yield();

			P4.Credential Credential = GetImpersonateCredential(ImpersonateUser);

			if (Credential == null)
			{
				throw new Exception($"Unable to get ticket for user {ImpersonateUser}");
			}

			return Credential.Ticket;

		}

		/// <inheritdoc/>
		public override async Task<List<FileSummary>> FindFilesAsync(IEnumerable<string> Paths)
		{
			await Task.Yield();

			List<FileSummary> Results = new List<FileSummary>();

			using (P4.Repository Repository = GetConnection(NoClient: true))
			{
				P4.GetFileMetaDataCmdOptions Options = new P4.GetFileMetaDataCmdOptions(P4.GetFileMetadataCmdFlags.ExcludeClientData, "", "", 0, "", "", "");

				IList<P4.FileMetaData>? Files = Repository.GetFileMetaData(Options, Paths.Select(Path => { P4.FileSpec FileSpec = new P4.FileSpec(new P4.DepotPath(Path)); return FileSpec; }).ToArray());

				if (Files == null)
				{
					Files = new List<P4.FileMetaData>();
				}

				foreach (string Path in Paths)
				{
					P4.FileMetaData Meta = Files.FirstOrDefault(File => File.DepotPath.Path == Path);

					if (Meta == null)
					{
						Results.Add(new FileSummary(Path, false, 0));
					}
					else
					{
						Results.Add(new FileSummary(Path, Meta.HeadAction != P4.FileAction.Delete && Meta.HeadAction != P4.FileAction.MoveDelete, Meta.HeadChange));
					}

				}

				return Results;
			}
		}

		/// <inheritdoc/>
		public override async Task<byte[]> PrintAsync(string DepotPath)
		{
			await Task.Yield();

			if (DepotPath.EndsWith("...", StringComparison.OrdinalIgnoreCase) || DepotPath.EndsWith("*", StringComparison.OrdinalIgnoreCase))
			{
				throw new Exception("PrintAsync requires exactly one file to be specified");
			}

			using (P4.Repository Repository = GetConnection(NoClient: true))
			{
				using (P4.P4Command Command = new P4.P4Command(Repository, "print", false, new string[] { "-q", DepotPath }))
				{
					P4.P4CommandResult Result = Command.Run();

					return Encoding.Default.GetBytes(Result.TextOutput);

				}

			}

		}

		/// <inheritdoc/>
		public override async Task<int> DuplicateShelvedChangeAsync(int ShelvedChange)
		{
			await Task.Yield();

			throw new Exception("DuplicateShelvedChangeAsync is disabled due to p4 issue: https://jira.it.epicgames.com/servicedesk/customer/portal/1/ITH-144069");

			/*
			using (P4.Repository Repository = GetConnection(NoClient: true))
			{
				return ReshelveChange(Repository, ShelvedChange);
			}

			throw new Exception($"Failed to duplicate shelved change {ShelvedChange}");
			*/

		}

		/// <inheritdoc/>
		public override async Task DeleteShelvedChangeAsync(int ShelvedChange)
		{
			await Task.Yield();

			using (P4.Repository Repository = GetConnection(NoClient: true))
			{

				P4.Changelist? Changelist = Repository.GetChangelist(ShelvedChange, new P4.DescribeCmdOptions(P4.DescribeChangelistCmdFlags.Omit, 0, 0));

				if (Changelist == null)
				{
					throw new Exception($"Unable to get shelved changelist for delete: {ShelvedChange}");
				}

				string ClientId = Changelist.ClientId;

				if (String.IsNullOrEmpty(ClientId))
				{
					throw new Exception($"Unable to get shelved changelist client id for delete: {ShelvedChange}");
				}

				using (P4.Repository ClientRepository = GetConnection(Changelist.Stream, Changelist.OwnerName, ClientId: ClientId))
				{

					if (Changelist.Shelved)
					{
						IList<P4.FileSpec> Files = ClientRepository.Connection.Client.ShelveFiles(new P4.ShelveFilesCmdOptions(P4.ShelveFilesCmdFlags.Delete, null, ShelvedChange));
					}

					ClientRepository.DeleteChangelist(Changelist, null);
				}
			}
		}

		/// <inheritdoc/>
		public override async Task UpdateChangelistDescription(int Change, string Description)
		{
			await Task.Yield();

			using (P4.Repository Repository = GetConnection(NoClient: true))
			{
				P4.Changelist Changelist = Repository.GetChangelist(Change);

				Repository.Connection.Disconnect();

				// the client must exist for the change list, otherwise will fail (for example, CreateNewChangeAsync deletes the client before returning)
				using (P4.Repository UpdateRepository = GetConnection(ClientFromChange: Change, UseClientFromChange: true, Username: Changelist.OwnerName))
				{					
					P4.Changelist UpdatedChangelist = UpdateRepository.GetChangelist(Change);		
					UpdatedChangelist.Description = Description;
					UpdateRepository.UpdateChangelist(UpdatedChangelist);					
				}
			}
		}

		/// <inheritdoc/>
		public async override Task<int> CreateNewChangeAsync(string StreamName, string FilePath)
		{
			await Task.Yield();

			ServerSettings Settings = this.Settings.CurrentValue;

			string Username = CanImpersonate ? "buildmachine" : Settings.P4BridgeServiceUsername!;

			P4.SubmitResults? SubmitResults = null;

			using (P4.Repository Repository = GetConnection(Stream: StreamName, Username: Username, ReadOnly: false, CreateChange: true))
			{
				P4.Client Client = Repository.Connection.Client;

				string WorkspaceFilePath = $"//{Client.Name}/{FilePath.TrimStart('/')}";
				string DiskFilePath = $"{Client.Root + FilePath.TrimStart('/')}";

				P4.FileSpec WorkspaceFileSpec = new P4.FileSpec(new P4.ClientPath(WorkspaceFilePath));

				IList<P4.File> Files = Repository.GetFiles(new P4.FilesCmdOptions(P4.FilesCmdFlags.None, 1), WorkspaceFileSpec);

				P4.Changelist? SubmitChangelist = null;
				P4.DepotPath? DepotPath = null;

				const int MaxRetries = 10;
				int Retry = 0;

				for (; ; )
				{
					DepotPath = null;

					if (Retry == MaxRetries)
					{
						break;
					}

					try
					{
						if (Files == null || Files.Count == 0)
						{
							// File does not exist, create it
							string? DirectoryName = Path.GetDirectoryName(DiskFilePath);

							if (string.IsNullOrEmpty(DirectoryName))
							{
								throw new Exception($"Invalid directory name for local client file, disk file path: {DiskFilePath}");
							}

							// Create the directory
							if (!Directory.Exists(DirectoryName))
							{
								Directory.CreateDirectory(DirectoryName);

								if (!Directory.Exists(DirectoryName))
								{
									throw new Exception($"Unable to create directrory: {DirectoryName}");
								}
							}

							// Create the file
							if (!File.Exists(DiskFilePath))
							{
								using (FileStream FileStream = File.OpenWrite(DiskFilePath))
								{
									FileStream.Close();
								}

								if (!File.Exists(DiskFilePath))
								{
									throw new Exception($"Unable to create local change file: {DiskFilePath}");
								}

							}

							IList<P4.FileSpec> DepotFiles = Client.AddFiles(new P4.Options(), WorkspaceFileSpec);

							if (DepotFiles == null || DepotFiles.Count != 1)
							{
								throw new Exception($"Unable to add local change file,  local: {DiskFilePath} : workspace: {WorkspaceFileSpec}");
							}

							DepotPath = DepotFiles[0].DepotPath;

						}
						else
						{
							IList<P4.FileSpec> SyncResults = Client.SyncFiles(new P4.SyncFilesCmdOptions(P4.SyncFilesCmdFlags.Force), WorkspaceFileSpec);

							if (SyncResults == null || SyncResults.Count != 1)
							{
								throw new Exception($"Unable to sync file, workspace: {WorkspaceFileSpec}");
							}

							IList<P4.FileSpec> EditResults = Client.EditFiles(new P4.FileSpec[] { new P4.FileSpec(SyncResults[0].DepotPath) }, new P4.Options());

							if (EditResults == null || EditResults.Count != 1)
							{
								throw new Exception($"Unable to edit file, workspace: {WorkspaceFileSpec}");
							}

							DepotPath = EditResults[0].DepotPath;

						}

						if (DepotPath == null || string.IsNullOrEmpty(DepotPath.Path))
						{
							throw new Exception($"Unable to get depot path for: {WorkspaceFileSpec}");
						}

						// create a new change
						if (SubmitChangelist == null)
						{
							P4.Changelist Changelist = new P4.Changelist();
							Changelist.Description = "New change for Horde job";
							Changelist.Files.Add(new P4.FileMetaData(new P4.FileSpec(DepotPath)));
							SubmitChangelist = Repository.CreateChangelist(Changelist);

							if (SubmitChangelist == null)
							{
								throw new Exception($"Unable to create a changelist for: {DepotPath}");
							}
						}

						SubmitResults = SubmitChangelist.Submit(null);

						if (SubmitResults == null)
						{
							throw new Exception($"Unable to submit changelist for: {DepotPath}");
						}

						break;
					}
					catch
					{
						Retry++;

						Client.RevertFiles(new P4.Options(), WorkspaceFileSpec);

						continue;
					}

				}

				string ClientName = Client.Name;
				try
				{
					Repository.DeleteClient(Client, new P4.Options());
				}
				catch
				{
					Logger.LogError($"Unable to delete client {ClientName}");
				}

			}

			if (SubmitResults == null)
			{
				throw new Exception($"Unable to submit change for {StreamName} {FilePath}");
			}

			return SubmitResults.ChangeIdAfterSubmit;
		}

		/// <inheritdoc/>
		public override async Task<(int? Change, string Message)> SubmitShelvedChangeAsync(int Change, int OriginalChange)
		{
			await Task.Yield();

			using (P4.Repository Repository = GetConnection(NoClient: true))
			{

				List<string> Args = new List<string> { "-S", Change.ToString(CultureInfo.InvariantCulture), OriginalChange.ToString(CultureInfo.InvariantCulture) };

				using (P4.P4Command Command = new P4.P4Command(Repository, "describe", true, Args.ToArray()))
				{
					P4.P4CommandResult Result = Command.Run();

					if (!Result.Success)
					{
						throw new Exception("Unable to get changes");
					}

					if (Result.TaggedOutput == null || Result.TaggedOutput.Count != 2)
					{

						throw new Exception($"Unable to get tagged output for change: {Change} and original change: {OriginalChange}");
					}

					bool DSTMismatch = false;
					string Offset = string.Empty;

					if (Server != null && Server.Metadata != null)
					{
						Offset = Server.Metadata.DateTimeOffset;
						DSTMismatch = P4.FormBase.DSTMismatch(Server.Metadata);
					}

					P4.Changelist Changelist = new P4.Changelist();
					Changelist.FromChangeCmdTaggedOutput(Result.TaggedOutput[0], true, Offset, DSTMismatch);

					P4.Changelist OriginalChangelist = new P4.Changelist();
					OriginalChangelist.FromChangeCmdTaggedOutput(Result.TaggedOutput[1], true, Offset, DSTMismatch);

					if (OriginalChangelist.ShelvedFiles.Count != Changelist.ShelvedFiles.Count)
					{
						throw new Exception($"Mismatched number of shelved files for change: {Change} and original change: {OriginalChange}");
					}

					if (OriginalChangelist.ShelvedFiles.Count == 0)
					{
						throw new Exception($"No shelved file for change: {Change} and original change: {OriginalChange}");
					}

					// todo, test straight up delete
					foreach (P4.ShelvedFile ShelvedFile in Changelist.ShelvedFiles)
					{
						P4.ShelvedFile? Found = OriginalChangelist.ShelvedFiles.FirstOrDefault(Original => Original.Digest == ShelvedFile.Digest && Original.Action == ShelvedFile.Action);

						if (Found == null)
						{
							throw new Exception($"Mismatch in shelved file digest or action for {ShelvedFile.Path}");
						}
					}

					Repository.Connection.Disconnect();

					using (P4.Repository SubmitRepository = GetConnection(ReadOnly: false, ClientFromChange: Change, UseClientFromChange: true, Username: Changelist.OwnerName))
					{
						// we might not need a client here, possibly -e below facilitates this, check!
						using (P4.P4Command SubmitCommand = new P4.P4Command(SubmitRepository, "submit", null, true, new string[] { "-e", Change.ToString(CultureInfo.InvariantCulture) }))
						{
							Result = SubmitCommand.Run();

							if (!Result.Success)
							{
								throw new Exception($"Unable to submit {Change}");
							}

							int? SubmittedChangeId = null;
							
							foreach (P4.TaggedObject TaggedObject in Result.TaggedOutput)
							{
								string? Submitted;
								if (TaggedObject.TryGetValue("submittedChange", out Submitted))
								{
									SubmittedChangeId = int.Parse(Submitted, CultureInfo.InvariantCulture);									
								}
							}

							if (SubmittedChangeId == null)
							{
								throw new Exception($"Unable to get submitted changelist");
							}

							return (SubmittedChangeId, Changelist.Description);							

						}
					}
				}
			}

			throw new Exception($"Unable to get shelve change: {Change} for original change: {OriginalChange}");

		}

		/// <inheritdoc/>		
		public override async Task<PerforceUserInfo?> GetUserInfoAsync(string UserName)
		{
			await Task.Yield();

			using (P4.Repository Repository = GetConnection(NoClient: true))
			{
				P4.User User = Repository.GetUser(UserName, new P4.UserCmdOptions(P4.UserCmdFlags.Output));

				if (User == null)
				{
					return null;
				}

				return new PerforceUserInfo { Name = UserName, Email = User.EmailAddress };
			}
		}

		void RequireEdgeServer()
		{
			string? Value;
			if (Server.Metadata.RawData.TryGetValue("serverServices", out Value))
			{
				if (Value != "edge-server")
				{
					Serilog.Log.Logger.Information("Perforce: servierService is not an edge server : {ServerServices}", Value);
					throw new Exception("Operation requires an edge server");
				}
			}
			else
			{
				throw new Exception("unable to get server services");
			}
		}

		bool CanImpersonate
		{
			get
			{
				ServerSettings Settings = this.Settings.CurrentValue;
				// Todo: we need a better way of identifying we're using a service account
				return Settings.P4BridgeServiceUsername == "SVC-P4-HordeProxy-P";
			}
		}

		int ReshelveChange(P4.Repository Repository, int Change)
		{

			// all attempts at using this without edge server have lead to bizarre reshelving behavior, also note that -p requires an edge server
			// https://jira.it.epicgames.com/servicedesk/customer/portal/1/ITH-144069
			RequireEdgeServer();

			using (P4.P4Command Cmd = new P4.P4Command(Repository, "reshelve", false, new string[] { "-s", Change.ToString(CultureInfo.InvariantCulture), "-f", "-p" }))
			{
				P4.P4CommandResult Results = Cmd.Run();

				if (Results.Success)
				{

					if (Results.InfoOutput.Count != 2)
					{
						Serilog.Log.Logger.Information("Perforce: Unexpected info output when reshelving change");
						throw new Exception("Unexpected info output when reshelving change");
					}

					bool Error = true;
					int ReshelvedChange = 0;
					string Message = Results.InfoOutput[1].Message;
					Match ChangeMatch = Regex.Match(Message, @"Change (\d+) files shelved");

					if (ChangeMatch.Success && ChangeMatch.Groups.Count == 2)
					{
						if (int.TryParse(ChangeMatch.Groups[1].Value, out ReshelvedChange))
						{
							Error = false;
						}
					}

					if (Error)
					{
						Serilog.Log.Logger.Information("Perforce: Unable to parse cl for reshelf: {Message}", Message);
						throw new Exception($"Unable to parse cl for reshelf: {Message}");
					}

					return ReshelvedChange;
				}
			}

			Serilog.Log.Logger.Information("Perforce: General reshelving failure");

			throw new Exception($"Unable to reshelve CL {Change}");

		}

	}

}

