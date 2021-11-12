// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using OpenTracing;
using OpenTracing.Util;

namespace HordeServer.Services
{
	using P4 = Perforce.P4;
	using UserId = ObjectId<IUser>;

	static class PerforceExtensions
	{
		static FieldInfo Field = typeof(P4.Changelist).GetField("_baseForm", BindingFlags.Instance | BindingFlags.NonPublic)!;

		public static string GetPath(this P4.Changelist Changelist)
		{
			P4.FormBase? FormBase = Field.GetValue(Changelist) as P4.FormBase;
			object? PathValue = null;
			FormBase?.TryGetValue("path", out PathValue);
			return (PathValue as string) ?? "//...";
		}
	}

	/// <summary>
	/// P4API implementation of the Perforce service
	/// </summary>
	class PerforceService : IPerforceService, IDisposable
	{
		class CachedTicketInfo
		{
			public IPerforceServer Server;
			public string UserName;
			public P4.Credential Ticket;

			public CachedTicketInfo(IPerforceServer Server, string UserName, P4.Credential Ticket)
			{
				this.Server = Server;
				this.UserName = UserName;
				this.Ticket = Ticket;
			}
		}

		PerforceLoadBalancer LoadBalancer;
		LazyCachedValue<Task<Globals>> CachedGlobals;
		ILogger Logger;

		/// <summary>
		/// Object used for controlling access to the access user tickets
		/// </summary>
		static object TicketLock = new object();

		/// <summary>
		/// Object used for controlling access to the p4 command output
		/// </summary>
		static object P4LogLock = new object();

		/// <summary>
		/// Native -> managed debug logging callback
		/// </summary>
		P4.P4CallBacks.LogMessageDelegate LogBridgeDelegate;

		/// <summary>
		/// The server settings
		/// </summary>
		ServerSettings Settings;

		Dictionary<string, Dictionary<string, CachedTicketInfo>> ClusterTickets = new Dictionary<string, Dictionary<string, CachedTicketInfo>>(StringComparer.OrdinalIgnoreCase);

		IUserCollection UserCollection;
		MemoryCache UserCache = new MemoryCache(new MemoryCacheOptions { SizeLimit = 2000 });

		/// <summary>
		/// Constructor
		/// </summary>
		public PerforceService(PerforceLoadBalancer LoadBalancer, DatabaseService DatabaseService, IUserCollection UserCollection, IOptions<ServerSettings> Settings, ILogger<PerforceService> Logger)
		{
			this.LoadBalancer = LoadBalancer;
			this.CachedGlobals = new LazyCachedValue<Task<Globals>>(() => DatabaseService.GetGlobalsAsync(), TimeSpan.FromSeconds(30.0));
			this.UserCollection = UserCollection;
			this.Settings = Settings.Value;
			this.Logger = Logger;

			LogBridgeDelegate = new P4.P4CallBacks.LogMessageDelegate(LogBridgeMessage);
			P4.P4Debugging.SetBridgeLogFunction(LogBridgeDelegate);

			P4.LogFile.SetLoggingFunction(LogPerforce);
		}

		public void Dispose()
		{
			UserCache.Dispose();
		}

		public async ValueTask<IUser> FindOrAddUserAsync(string ClusterName, string UserName)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.FindOrAddUserAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("UserName", UserName);
			
			IUser? User;
			if (!UserCache.TryGetValue((ClusterName, UserName), out User))
			{
				User = await UserCollection.FindUserByLoginAsync(UserName);
				if (User == null)
				{
					PerforceUserInfo? UserInfo = await GetUserInfoAsync(ClusterName, UserName);
					User = await UserCollection.FindOrAddUserByLoginAsync(UserName, UserInfo?.FullName, UserInfo?.Email);
				}

				using (ICacheEntry Entry = UserCache.CreateEntry((ClusterName, UserName)))
				{
					Entry.SetValue(User);
					Entry.SetSize(1);
					Entry.SetAbsoluteExpiration(TimeSpan.FromDays(1.0));
				}
			}
			return User!;
		}

		async Task<PerforceCluster> GetClusterAsync(string? ClusterName)
		{
			Globals Globals = await CachedGlobals.GetCached();

			PerforceCluster? Cluster = Globals.FindPerforceCluster(ClusterName);
			if (Cluster == null)
			{
				throw new Exception($"Unknown Perforce cluster '{ClusterName}'");
			}

			return Cluster;
		}

		async Task<IPerforceServer> SelectServer(PerforceCluster Cluster)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.SelectServer").StartActive();
			Scope.Span.SetTag("ClusterName", Cluster.Name);
			
			IPerforceServer? Server = await LoadBalancer.SelectServerAsync(Cluster);
			if (Server == null)
			{
				throw new Exception($"Unable to select server from '{Cluster.Name}'");
			}
			return Server;
		}

		[SuppressMessage("Microsoft.Reliability", "CA2000:DisposeObjectsBeforeLosingScope")]
		static P4.Repository CreateConnection(IPerforceServer Server, string? UserName, string? Ticket)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.CreateConnection").StartActive();
			Scope.Span.SetTag("Cluster", Server.Cluster);
			Scope.Span.SetTag("ServerAndPort", Server.ServerAndPort);
			Scope.Span.SetTag("UserName", UserName);
			
			P4.Repository Repository = new P4.Repository(new P4.Server(new P4.ServerAddress(Server.ServerAndPort)));
			try
			{
				P4.Connection Connection = Repository.Connection;
				if (UserName != null)
				{
					Connection.UserName = UserName;
				}

				P4.Options Options = new P4.Options();
				if (Ticket != null)
				{
					Options["Ticket"] = Ticket;
				}

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

		/// <summary>
		/// 
		/// </summary>
		/// <param name="ClusterName"></param>
		/// <returns></returns>
		public async Task<NativePerforceConnection?> GetServiceUserConnection(string? ClusterName)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetServiceUserConnection").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);

			PerforceCluster Cluster = await GetClusterAsync(ClusterName);

			string? UserName = Cluster.ServiceAccount ?? Environment.UserName;

			string? Password = null;
			if (Cluster.ServiceAccount != null)
			{
				PerforceCredentials? Credentials = Cluster.Credentials.FirstOrDefault(x => x.UserName.Equals(UserName, StringComparison.OrdinalIgnoreCase));
				if (Credentials == null)
				{
					throw new Exception($"No credentials defined for {Cluster.ServiceAccount} on {Cluster.Name}");
				}
				Password = Credentials.Password;
			}

			IPerforceServer Server = await SelectServer(Cluster);

			PerforceSettings Settings = new PerforceSettings();
			Settings.ServerAndPort = Server.ServerAndPort;
			Settings.User = UserName;
			Settings.Password = Password;
			Settings.AppName = "Horde.Build";
			Settings.Client = "__DOES_NOT_EXIST__";

			NativePerforceConnection NativeConnection = new NativePerforceConnection(Logger);
			await NativeConnection.ConnectAsync(Settings);
			return NativeConnection;
		}

		async Task<P4.Repository> GetServiceUserConnection(PerforceCluster Cluster)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetServiceUserConnectionAsync").StartActive();
			Scope.Span.SetTag("ClusterName", Cluster.Name);
			
			IPerforceServer Server = await SelectServer(Cluster);
			return GetServiceUserConnection(Cluster, Server);
		}

		P4.Repository GetServiceUserConnection(PerforceCluster Cluster, IPerforceServer Server)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetServiceUserConnection").StartActive();
			Scope.Span.SetTag("ClusterName", Cluster.Name);

			if (Cluster.Name == PerforceCluster.DefaultName && Settings.P4BridgeServiceUsername != null && Settings.P4BridgeServicePassword != null)
			{
				return CreateConnection(Server, Settings.P4BridgeServiceUsername, Settings.P4BridgeServicePassword);
			}

			string? UserName = null;
			string? Password = null;
			if (Cluster.ServiceAccount != null)
			{
				PerforceCredentials? Credentials = Cluster.Credentials.FirstOrDefault(x => x.UserName.Equals(Cluster.ServiceAccount, StringComparison.OrdinalIgnoreCase));
				if (Credentials == null)
				{
					throw new Exception($"No credentials defined for {Cluster.ServiceAccount} on {Cluster.Name}");
				}
				UserName = Credentials.UserName;
				Password = Credentials.Password;
			}
			return CreateConnection(Server, UserName, Password);
		}

		async Task<CachedTicketInfo> GetImpersonateCredential(PerforceCluster Cluster, string ImpersonateUser)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetImpersonateCredential").StartActive();
			Scope.Span.SetTag("ClusterName", Cluster.Name);
			Scope.Span.SetTag("ImpersonateUser", ImpersonateUser);
			
			if (!Cluster.CanImpersonate)
			{
				throw new Exception($"Service account required to impersonate user {ImpersonateUser}");
			}

			CachedTicketInfo? TicketInfo = null;

			Dictionary<string, CachedTicketInfo>? UserTickets;
			lock (TicketLock)
			{
				// Check if we have a ticket
				if (!ClusterTickets.TryGetValue(Cluster.Name, out UserTickets))
				{
					UserTickets = new Dictionary<string, CachedTicketInfo>(StringComparer.OrdinalIgnoreCase);
					ClusterTickets[Cluster.Name] = UserTickets;
				}
				if (UserTickets.TryGetValue(ImpersonateUser, out TicketInfo))
				{
					// if the credential expires within the next 15 minutes, refresh
					TimeSpan Time = new TimeSpan(0, 15, 0);
					if (TicketInfo.Ticket.Expires.Subtract(Time) <= DateTime.UtcNow)
					{
						UserTickets.Remove(ImpersonateUser);
						TicketInfo = null;
					}
				}
			}

			if (TicketInfo == null)
			{
				IPerforceServer Server = await SelectServer(Cluster);

				P4.Credential? Credential;
				using (P4.Repository Repository = GetServiceUserConnection(Cluster, Server))
				{
					Credential = Repository.Connection.Login(null, new P4.LoginCmdOptions(P4.LoginCmdFlags.AllHosts | P4.LoginCmdFlags.DisplayTicket, null), ImpersonateUser);
				}
				if (Credential == null)
				{
					throw new Exception($"GetImpersonateCredential - Unable to get impersonation credential for {ImpersonateUser} from {Dns.GetHostName()}");
				}

				TicketInfo = new CachedTicketInfo(Server, ImpersonateUser, Credential);

				lock (TicketLock)
				{
					UserTickets[ImpersonateUser] = TicketInfo;
				}
			}

			return TicketInfo;
		}

		async Task<P4.Repository> GetImpersonatedConnection(PerforceCluster Cluster, string ImpersonateUser)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetImpersonatedConnection").StartActive();
			Scope.Span.SetTag("ClusterName", Cluster.Name);
			Scope.Span.SetTag("ImpersonateUser", ImpersonateUser);
			
			CachedTicketInfo TicketInfo = await GetImpersonateCredential(Cluster, ImpersonateUser);
			return CreateConnection(TicketInfo.Server, TicketInfo.UserName, TicketInfo.Ticket.Ticket);
		}

		[SuppressMessage("Microsoft.Reliability", "CA2000:DisposeObjectsBeforeLosingScope")]
		async Task<P4.Repository> GetConnection(PerforceCluster Cluster, string? Stream = null, string? Username = null, bool ReadOnly = true, bool CreateChange = false, int? ClientFromChange = null, bool UseClientFromChange = false, bool UsePortFromChange = false, string? ClientId = null, bool NoClient = false)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetConnection").StartActive();
			Scope.Span.SetTag("ClusterName", Cluster.Name);
			Scope.Span.SetTag("Stream", Stream);
			Scope.Span.SetTag("Username", Username);
			Scope.Span.SetTag("NoClient", NoClient);
			Scope.Span.SetTag("ClientId", ClientId);
			
			P4.Repository Repository;
			if (Username == null || !Cluster.CanImpersonate || Username.Equals(Cluster.ServiceAccount, StringComparison.OrdinalIgnoreCase))
			{
				Repository = await GetServiceUserConnection(Cluster);
			}
			else
			{
				Repository = await GetImpersonatedConnection(Cluster, Username);
			}

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
						P4.Client Client = GetOrCreateClient(Cluster.ServiceAccount, Repository, Stream, Username, ReadOnly, CreateChange, ClientFromChange, UseClientFromChange, UsePortFromChange);
						Repository.Connection.SetClient(Client.Name);

					}

					if (UseClientFromChange)
					{
						string? ClientHost = Repository.Connection.Client.Host;
						if (!string.IsNullOrEmpty(ClientHost))
						{
							Repository.Connection.getP4Server().SetConnectionHost(ClientHost);
						}
					}

					/*

					This is disabled until https://jira.it.epicgames.com/servicedesk/customer/portal/1/ITH-144069 is resolved and edge server is sorted (required for things like deleting shelves)

					string? ClientPort = null;

					if (UsePortFromChange)
					{
						if (!Repository.Connection.Client.Name.StartsWith("horde-p4bridge-", StringComparison.OrdinalIgnoreCase))
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
					*/

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

			Repository.Connection.CommandEcho += LogPerforceCommand;

			return Repository;

		}

		static string GetClientName(string? ServiceUserName, string Stream, bool ReadOnly, bool CreateChange, string? Username = null)
		{
			string ClientName = $"horde-p4bridge-{Dns.GetHostName()}-{ServiceUserName ?? "default"}-{Stream.Replace("/", "+", StringComparison.OrdinalIgnoreCase)}";

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

		static P4.Client GetOrCreateClient(string? ServiceUserName, P4.Repository Repository, string? Stream, string? Username = null, bool ReadOnly = true, bool CreateChange = false, int? ClientFromChange = null, bool UseClientFromChange = false, bool UsePortFromChange = false)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetOrCreateClient").StartActive();
			Scope.Span.SetTag("ServiceUserName", ServiceUserName);
			Scope.Span.SetTag("Stream", Stream);
			Scope.Span.SetTag("Username", Username);
			
			P4.Client? Client = null;
			P4.Changelist? Changelist = null;

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
				string ClientName = GetClientName(ServiceUserName, Stream, ReadOnly, CreateChange, Username);

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

					NewClient.OwnerName = Username ?? ServiceUserName;

					Client = Repository.CreateClient(NewClient);

					if (Client == null)
					{
						throw new Exception($"Unable to create client for ${Stream} : {Username ?? ServiceUserName}");
					}
				}
			}

			if (Client == null)
			{
				throw new Exception($"Unable to create client for Stream:{Stream} Username:{Username} ReadOnly:{ReadOnly} CreateChange:{CreateChange} ClientFromChange:{ClientFromChange} UseClientFromChange:{UseClientFromChange} UsePortFromChange:{UsePortFromChange}");
			}

			return Client;

		}

		/// <summary>
		/// Logs perforce commands 
		/// </summary>
		/// <param name="Log">The p4 command log info</param>
		static void LogPerforceCommand(string Log)
		{
			lock (P4LogLock)
			{
				Serilog.Log.Information("Perforce: {Message}", Log);
			}

		}

		/// <summary>
		/// Perforce logging funcion
		/// </summary>
		/// <param name="LogLevel">The level whether error, warning, message, or debug</param>
		/// <param name="Source">The log source</param>
		/// <param name="Message">The log message</param>
		static void LogPerforce(int LogLevel, String Source, String Message)
		{
			lock (P4LogLock)
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
			}
		}

		static void LogBridgeMessage(int LogLevel, String Filename, int Line, String Message)
		{

			// Note, we do not get log level 4 unless it is defined in native code as it is very, very spammy (P4BridgeServer.cpp)

			// remove the full path to the source, keep just the file name
			String fileName = Path.GetFileName(Filename);

			string category = String.Format(CultureInfo.CurrentCulture, "P4Bridge({0}:{1})", fileName, Line);

			P4.LogFile.LogMessage(LogLevel, category, Message);
		}

		class StreamView : IStreamView
		{
			P4.P4MapApi MapApi;

			public StreamView(P4.P4MapApi MapApi)
			{
				this.MapApi = MapApi;
			}

			public void Dispose()
			{
				MapApi.Dispose();
			}

			public bool TryGetStreamPath(string DepotPath, out string StreamPath)
			{
				StreamPath = MapApi.Translate(DepotPath, P4.P4MapApi.Direction.LeftRight);
				return StreamPath != null;
			}
		}

		/// <inheritdoc/>
		public async Task<IStreamView> GetStreamViewAsync(string ClusterName, string StreamName)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetStreamViewAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("StreamName", StreamName);
			
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, NoClient: true))
			{
				P4.Stream Stream = Repository.GetStream(StreamName, new P4.StreamCmdOptions(P4.StreamCmdFlags.View, null, null));

				P4.P4MapApi? MapApi = null;
				try
				{
					MapApi = new P4.P4MapApi(Repository.Server.Metadata.UnicodeEnabled);
					foreach (P4.MapEntry Entry in Stream.View)
					{
						P4.P4MapApi.Type MapType;
						switch (Entry.Type)
						{
							case P4.MapType.Include:
								MapType = P4.P4MapApi.Type.Include;
								break;
							case P4.MapType.Exclude:
								MapType = P4.P4MapApi.Type.Exclude;
								break;
							case P4.MapType.Overlay:
								MapType = P4.P4MapApi.Type.Overlay;
								break;
							default:
								throw new Exception($"Invalid map type: {Entry.Type}");
						}
						MapApi.Insert(Entry.Left.ToString(), Entry.Right.ToString(), MapType);
					}

					StreamView View = new StreamView(MapApi);
					MapApi = null;
					return View;
				}
				finally
				{
					MapApi?.Dispose();
				}
			}
		}

		static int GetSyncRevision(string Path, P4.FileAction HeadAction, int HeadRev)
		{
			switch (HeadAction)
			{
				case P4.FileAction.None:
				case P4.FileAction.Add:
				case P4.FileAction.Branch:
				case P4.FileAction.MoveAdd:
				case P4.FileAction.Edit:
				case P4.FileAction.Integrate:
					return HeadRev;
				case P4.FileAction.Delete:
				case P4.FileAction.MoveDelete:
					return -1;
				default:
					throw new Exception($"Unrecognized P4 file change type '{HeadAction}' for file {Path}#{HeadRev}");
			}
		}

		/// <summary>
		/// Creates a <see cref="ChangeFile"/> from a <see cref="P4.FileMetaData"/>
		/// </summary>
		/// <param name="RelativePath"></param>
		/// <param name="MetaData"></param>
		/// <returns></returns>
		static ChangeFile CreateChangeFile(string RelativePath, P4.FileMetaData MetaData)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.CreateChangeFile (FileMetaData)").StartActive();
			Scope.Span.SetTag("RelativePath", RelativePath);
			
			int Revision = GetSyncRevision(MetaData.DepotPath.Path, MetaData.HeadAction, MetaData.HeadRev);
			Md5Hash? Digest = String.IsNullOrEmpty(MetaData.Digest) ? (Md5Hash?)null : Md5Hash.Parse(MetaData.Digest);
			return new ChangeFile(RelativePath, MetaData.DepotPath.Path, Revision, MetaData.FileSize, Digest, MetaData.HeadType.ToString());
		}

		/// <summary>
		/// Creates a <see cref="ChangeFile"/> from a <see cref="P4.FileMetaData"/>
		/// </summary>
		/// <param name="RelativePath"></param>
		/// <param name="MetaData"></param>
		/// <returns></returns>
		static ChangeFile CreateChangeFile(string RelativePath, P4.ShelvedFile MetaData)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.CreateChangeFile (ShelvedFile)").StartActive();
			Scope.Span.SetTag("RelativePath", RelativePath);
			
			int Revision = GetSyncRevision(MetaData.Path.Path, MetaData.Action, MetaData.Revision);
			Md5Hash? Digest = String.IsNullOrEmpty(MetaData.Digest) ? (Md5Hash?)null : Md5Hash.Parse(MetaData.Digest);
			return new ChangeFile(RelativePath, MetaData.Path.Path, Revision, MetaData.Size, Digest, MetaData.Type.ToString());
		}

		/// <inheritdoc/>
		public async Task<List<ChangeFile>> GetStreamSnapshotAsync(string ClusterName, string StreamName, int Change)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetStreamSnapshotAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("StreamName", StreamName);
			Scope.Span.SetTag("Change", Change);
			
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, Stream: StreamName))
			{
				P4.FileSpec FileSpec = new P4.FileSpec(new P4.DepotPath($"//UE5/Main/Engine/Build/..."), new P4.ChangelistIdVersion(Change));
				Repository.Connection.Client.SyncFiles(new P4.SyncFilesCmdOptions(P4.SyncFilesCmdFlags.ServerOnly | P4.SyncFilesCmdFlags.Quiet), FileSpec);

				string ClientPrefix = $"//{Repository.Connection.Client.Name}/";
				P4.FileSpec StatSpec = new P4.FileSpec(new P4.ClientPath($"{ClientPrefix}..."));

				IList<P4.FileMetaData> Files = Repository.GetFileMetaData(new P4.GetFileMetaDataCmdOptions(P4.GetFileMetadataCmdFlags.Synced | P4.GetFileMetadataCmdFlags.LocalPath | P4.GetFileMetadataCmdFlags.FileSize, null, null, 0, null, null, null), StatSpec);

				List<ChangeFile> Results = new List<ChangeFile>();
				foreach (P4.FileMetaData File in Files)
				{
					string RelativePath = File.ClientPath.Path;
					if (RelativePath.StartsWith(ClientPrefix, StringComparison.OrdinalIgnoreCase))
					{
						RelativePath = RelativePath.Substring(ClientPrefix.Length);
					}
					else
					{
						throw new Exception($"Client path does not start with client name: {RelativePath}");
					}
					Results.Add(CreateChangeFile(RelativePath, File));
				}
				return Results;
			}
		}

		/// <inheritdoc/>
		public async Task<List<ChangeSummary>> GetChangesAsync(string ClusterName, int? MinChange, int? MaxChange, int MaxResults)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetChangesAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("MinChange", MinChange ?? -1);
			Scope.Span.SetTag("MaxResults", MaxResults);
			
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, NoClient: true))
			{
				P4.ChangesCmdOptions Options = new P4.ChangesCmdOptions(P4.ChangesCmdFlags.IncludeTime | P4.ChangesCmdFlags.FullDescription, null, MaxResults, P4.ChangeListStatus.Submitted, null);

				IList<P4.Changelist> Changelists = Repository.GetChangelists(Options, new P4.FileSpec(new P4.DepotPath(GetFilter("//...", MinChange, MaxChange)), null, null, null));

				List<ChangeSummary> Changes = new List<ChangeSummary>();
				if (Changelists != null)
				{
					foreach (P4.Changelist Changelist in Changelists)
					{
						IUser User = await FindOrAddUserAsync(ClusterName, Changelist.OwnerName);
						Changes.Add(new ChangeSummary(Changelist.Id, User, Changelist.GetPath(), Changelist.Description));
					}
				}
				return Changes;
			}
		}

		/// <inheritdoc/>
		public async Task<List<ChangeSummary>> GetChangesAsync(string ClusterName, string StreamName, int? MinChange, int? MaxChange, int Results, string? ImpersonateUser)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetChangesAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("MinChange", MinChange ?? -1);
			Scope.Span.SetTag("MaxChange", MaxChange ?? -1);
			Scope.Span.SetTag("Results", Results);
			Scope.Span.SetTag("ImpersonateUser", ImpersonateUser);
			
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, StreamName, ImpersonateUser))
			{

				P4.ChangesCmdOptions Options = new P4.ChangesCmdOptions(P4.ChangesCmdFlags.IncludeTime | P4.ChangesCmdFlags.FullDescription, null, Results, P4.ChangeListStatus.Submitted, null);

				string Filter = GetFilter($"//{Repository.Connection.Client.Name}/...", MinChange, MaxChange);

				P4.FileSpec FileSpec = new P4.FileSpec(new P4.DepotPath(Filter), null, null, null);

				IList<P4.Changelist> Changelists = Repository.GetChangelists(Options, FileSpec);

				List<ChangeSummary> Changes = new List<ChangeSummary>();

				if (Changelists != null)
				{
					foreach (P4.Changelist Changelist in Changelists)
					{
						IUser User = await FindOrAddUserAsync(ClusterName, Changelist.OwnerName);
						Changes.Add(new ChangeSummary(Changelist.Id, User, Changelist.GetPath(), Changelist.Description));
					}
				}

				return Changes;
			}
		}

		/// <inheritdoc/>
		public async Task<ChangeDetails> GetChangeDetailsAsync(string ClusterName, string StreamName, int ChangeNumber)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetChangeDetailsAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("StreamName", StreamName);
			Scope.Span.SetTag("ChangeNumber", ChangeNumber);
			
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, NoClient: true))
			{
				List<ChangeDetails> Results = new List<ChangeDetails>();

				List<string> Args = new List<string> { "-s", "-S", $"{ChangeNumber}" };

				using (P4.P4Command Command = new P4.P4Command(Repository, "describe", true, Args.ToArray()))
				{
					P4.P4CommandResult Result = Command.Run();

					if (!Result.Success || Result.TaggedOutput == null || Result.TaggedOutput.Count == 0)
					{
						throw new Exception("Unable to get changes");
					}

					List<P4.Changelist> Changelists = new List<P4.Changelist>() { };

					bool DSTMismatch = false;
					string Offset = string.Empty;

					P4.Server Server = Repository.Server;
					if (Server != null && Server.Metadata != null)
					{
						Offset = Server.Metadata.DateTimeOffset;
						DSTMismatch = P4.FormBase.DSTMismatch(Server.Metadata);
					}

					P4.TaggedObject TaggedObject = Result.TaggedOutput[0];
					{
						List<ChangeFile> Files = new List<ChangeFile>();

						P4.Changelist Change = new P4.Changelist();
						Change.FromChangeCmdTaggedOutput(TaggedObject, true, Offset, DSTMismatch);

						foreach (P4.FileMetaData DescribeFile in Change.Files)
						{
							string? RelativePath;
							if (TryGetStreamRelativePath(DescribeFile.DepotPath.Path, StreamName, out RelativePath))
							{
								Files.Add(CreateChangeFile(RelativePath, DescribeFile));
							}
						}

						IUser User = await FindOrAddUserAsync(ClusterName, Change.OwnerName);
						return new ChangeDetails(Change.Id, User, Change.GetPath(), Change.Description, Files, Change.ModifiedDate);
					}
				}
			}
		}

		/// <inheritdoc/>
		public async Task<List<ChangeDetails>> GetChangeDetailsAsync(string ClusterName, string StreamName, IReadOnlyList<int> ChangeNumbers, string? ImpersonateUser)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetChangeDetailsAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("StreamName", StreamName);
			Scope.Span.SetTag("ChangeNumbers.Count", ChangeNumbers.Count);
			Scope.Span.SetTag("ImpersonateUser", ImpersonateUser);
			
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, Stream: StreamName, Username: ImpersonateUser))
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

					P4.Server Server = Repository.Server;
					if (Server != null && Server.Metadata != null)
					{
						Offset = Server.Metadata.DateTimeOffset;
						DSTMismatch = P4.FormBase.DSTMismatch(Server.Metadata);
					}

					foreach (P4.TaggedObject TaggedObject in Result.TaggedOutput)
					{
						List<ChangeFile> Files = new List<ChangeFile>();

						P4.Changelist Change = new P4.Changelist();
						Change.FromChangeCmdTaggedOutput(TaggedObject, true, Offset, DSTMismatch);

						foreach (P4.FileMetaData DescribeFile in Change.Files)
						{
							string? RelativePath;
							if (TryGetStreamRelativePath(DescribeFile.DepotPath.Path, StreamName, out RelativePath))
							{
								Files.Add(CreateChangeFile(RelativePath, DescribeFile));
							}
						}

						if (Change.ShelvedFiles != null && Change.ShelvedFiles.Count > 0)
						{
							foreach (P4.ShelvedFile ShelvedFile in Change.ShelvedFiles)
							{
								string? RelativePath;
								if (TryGetStreamRelativePath(ShelvedFile.Path.ToString(), StreamName, out RelativePath))
								{
									Files.Add(CreateChangeFile(RelativePath, ShelvedFile));
								}
							}

						}

						IUser User = await FindOrAddUserAsync(ClusterName, Change.OwnerName);
						Results.Add(new ChangeDetails(Change.Id, User, Change.GetPath(), Change.Description, Files, Change.ModifiedDate));
					}

				}

				return Results;
			}
		}

		/// <inheritdoc />
		public async Task<string> CreateTicket(string ClusterName, string ImpersonateUser)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.CreateTicket").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("ImpersonateUser", ImpersonateUser);
			
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);

			CachedTicketInfo Credential = await GetImpersonateCredential(Cluster, ImpersonateUser);

			if (Credential == null)
			{
				throw new Exception($"Unable to get ticket for user {ImpersonateUser}");
			}

			return Credential.Ticket.Ticket;
		}

		/// <inheritdoc/>
		public async Task<List<FileSummary>> FindFilesAsync(string ClusterName, IEnumerable<string> Paths)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.FindFilesAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			
			List<FileSummary> Results = new List<FileSummary>();

			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, NoClient: true))
			{
				P4.GetFileMetaDataCmdOptions Options = new P4.GetFileMetaDataCmdOptions(P4.GetFileMetadataCmdFlags.ExcludeClientData, "", "", 0, "", "", "");

				IList<P4.FileMetaData>? Files = Repository.GetFileMetaData(Options, Paths.Select(Path => { P4.FileSpec FileSpec = new P4.FileSpec(new P4.DepotPath(Path)); return FileSpec; }).ToArray());

				if (Files == null)
				{
					Files = new List<P4.FileMetaData>();
				}

				foreach (string Path in Paths)
				{
					P4.FileMetaData? Meta = Files.FirstOrDefault(File => File.DepotPath?.Path == Path);

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
		public async Task<byte[]> PrintAsync(string ClusterName, string DepotPath)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.PrintAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("DepotPath", DepotPath);
			
			if (DepotPath.EndsWith("...", StringComparison.OrdinalIgnoreCase) || DepotPath.EndsWith("*", StringComparison.OrdinalIgnoreCase))
			{
				throw new Exception("PrintAsync requires exactly one file to be specified");
			}

			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, NoClient: true))
			{
				using (P4.P4Command Command = new P4.P4Command(Repository, "print", false, new string[] { "-q", DepotPath }))
				{
					P4.P4CommandResult Result = Command.Run();

					if (Result.BinaryOutput != null)
					{
						return Result.BinaryOutput;
					}

					return Encoding.Default.GetBytes(Result.TextOutput);

				}

			}
		}

		/// <inheritdoc/>
		public async Task<int> DuplicateShelvedChangeAsync(string ClusterName, int ShelvedChange)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.DuplicateShelvedChangeAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("ShelvedChange", ShelvedChange);

			string? ChangeOwner = null;

			// Get the owner of the shelf
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, NoClient: true))
			{
				List<string> Args = new List<string> { "-S", ShelvedChange.ToString(CultureInfo.InvariantCulture) };

				using (P4.P4Command Command = new P4.P4Command(Repository, "describe", true, Args.ToArray()))
				{
					P4.P4CommandResult Result = Command.Run();

					if (!Result.Success)
					{
						throw new Exception($"Unable to get change {ShelvedChange}");
					}

					if (Result.TaggedOutput == null || Result.TaggedOutput.Count != 1)
					{

						throw new Exception($"Unable to get tagged output for change: {ShelvedChange}");
					}

					P4.Changelist Changelist = new P4.Changelist();
					Changelist.FromChangeCmdTaggedOutput(Result.TaggedOutput[0], true, string.Empty, false);
					ChangeOwner = Changelist.OwnerName;

				}
			}

			if (string.IsNullOrEmpty(ChangeOwner))
			{
				throw new Exception($"Unable to get owner for shelved change {ShelvedChange}");
			}

			using (P4.Repository Repository = await GetConnection(Cluster, ReadOnly: false, ClientFromChange: ShelvedChange, UseClientFromChange: false, Username: ChangeOwner))
			{
				return ReshelveChange(Repository, ShelvedChange);
			}

			throw new Exception($"Unable to duplicate shelve change {ShelvedChange}");
		}

		/// <inheritdoc/>
		public async Task DeleteShelvedChangeAsync(string ClusterName, int ShelvedChange)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.DeleteShelvedChangeAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("ShelvedChange", ShelvedChange);
			
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, NoClient: true))
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

				using (P4.Repository ClientRepository = await GetConnection(Cluster, Changelist.Stream, Changelist.OwnerName, ClientId: ClientId, UseClientFromChange: true, UsePortFromChange: true))
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
		public async Task UpdateChangelistDescription(string ClusterName, int Change, string Description)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.UpdateChangelistDescription").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("Change", Change);

			try
			{
				PerforceCluster Cluster = await GetClusterAsync(ClusterName);
				using (P4.Repository Repository = await GetConnection(Cluster, NoClient: true))
				{
					P4.Changelist Changelist = Repository.GetChangelist(Change);

					Repository.Connection.Disconnect();

					// the client must exist for the change list, otherwise will fail (for example, CreateNewChangeAsync deletes the client before returning)
					using (P4.Repository UpdateRepository = await GetConnection(Cluster, ClientFromChange: Change, UseClientFromChange: true, Username: Changelist.OwnerName))
					{
						P4.Changelist UpdatedChangelist = UpdateRepository.GetChangelist(Change);
						UpdatedChangelist.Description = Description;
						UpdateRepository.UpdateChangelist(UpdatedChangelist);
					}
				}
			}
			catch (Exception Ex)
			{
				LogPerforce(1, "", $"Unable to update Changelist for CL {Change} to ${Description}, {Ex.Message}");
			}
		}

		/// <inheritdoc/>
		public async Task<int> CreateNewChangeAsync(string ClusterName, string StreamName, string FilePath)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.CreateNewChangeAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("StreamName", StreamName);
			Scope.Span.SetTag("FilePath", FilePath);
			
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			P4.SubmitResults? SubmitResults = null;

			using (P4.Repository Repository = await GetConnection(Cluster, Stream: StreamName, ReadOnly: false, CreateChange: true))
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
		public async Task<(int? Change, string Message)> SubmitShelvedChangeAsync(string ClusterName, int Change, int OriginalChange)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.SubmitShelvedChangeAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("Change", Change);
			Scope.Span.SetTag("OriginalChange", OriginalChange);
			
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, NoClient: true))
			{

				List<string> Args = new List<string> { "-S", Change.ToString(CultureInfo.InvariantCulture), OriginalChange.ToString(CultureInfo.InvariantCulture) };

				using (P4.P4Command Command = new P4.P4Command(Repository, "describe", true, Args.ToArray()))
				{
					P4.P4CommandResult Result = Command.Run();

					if (!Result.Success)
					{
						return (null, $"Unable to get change {Change}");

					}

					if (Result.TaggedOutput == null || Result.TaggedOutput.Count != 2)
					{

						return (null, $"Unable to get tagged output for change: {Change} and original change: {OriginalChange}");
					}

					bool DSTMismatch = false;
					string Offset = string.Empty;

					P4.Server Server = Repository.Server;
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
						return (null, $"Mismatched number of shelved files for change: {Change} and original change: {OriginalChange}");
					}

					if (OriginalChangelist.ShelvedFiles.Count == 0)
					{
						return (null, $"No shelved file for change: {Change} and original change: {OriginalChange}");
					}

					foreach (P4.ShelvedFile ShelvedFile in Changelist.ShelvedFiles)
					{
						P4.ShelvedFile? Found = OriginalChangelist.ShelvedFiles.FirstOrDefault(Original => Original.Digest == ShelvedFile.Digest && Original.Action == ShelvedFile.Action);

						if (Found == null)
						{
							return (null, $"Mismatch in shelved file digest or action for {ShelvedFile.Path}");
						}
					}

					Repository.Connection.Disconnect();

					using (P4.Repository SubmitRepository = await GetConnection(Cluster, ReadOnly: false, ClientFromChange: Change, UseClientFromChange: true, Username: Changelist.OwnerName))
					{
						// we might not need a client here, possibly -e below facilitates this, check!
						using (P4.P4Command SubmitCommand = new P4.P4Command(SubmitRepository, "submit", null, true, new string[] { "-e", Change.ToString(CultureInfo.InvariantCulture) }))
						{
							try
							{
								Result = SubmitCommand.Run();
							}
							catch (Exception Ex)
							{
								return (null, $"Submit command failed: {Ex.Message}");
							}

							if (!Result.Success)
							{
								string Message = (Result.ErrorList != null && Result.ErrorList.Count > 0) ? Result.ErrorList[0].ErrorMessage : "Unknown error, no errors in list";
								return (null, $"Unable to submit {Change}, {Message}");
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
								return (null, $"Submit command succeeded, though unable to parse submitted change number");
							}

							return (SubmittedChangeId, Changelist.Description);

						}
					}
				}
			}

			throw new Exception($"Unable to get shelve change: {Change} for original change: {OriginalChange}");
		}

		/// <inheritdoc/>		
		public async Task<PerforceUserInfo?> GetUserInfoAsync(string ClusterName, string UserName)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetUserInfoAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("UserName", UserName);
			
			PerforceCluster Cluster = await GetClusterAsync(ClusterName);
			using (P4.Repository Repository = await GetConnection(Cluster, NoClient: true))
			{
				P4.User User = Repository.GetUser(UserName, new P4.UserCmdOptions(P4.UserCmdFlags.Output));

				if (User == null)
				{
					return null;
				}

				return new PerforceUserInfo { Login = UserName, FullName = User.FullName, Email = User.EmailAddress };
			}
		}

		static int ReshelveChange(P4.Repository Repository, int Change)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.ReshelveChange").StartActive();
			Scope.Span.SetTag("Change", Change);

			bool EdgeServer = false;
			string? Value;
			if (Repository.Server.Metadata.RawData.TryGetValue("serverServices", out Value))
			{
				if (Value == "edge-server")
				{
					EdgeServer = true;
				}
			}

			List<string> Arguments = new List<string>();

			// promote shelf if we're on an edge server
			if (EdgeServer)
			{
				Arguments.Add("-p");
			}

			Arguments.AddRange(new string[] { "-s", Change.ToString(CultureInfo.InvariantCulture), "-f" });

			using (P4.P4Command Cmd = new P4.P4Command(Repository, "reshelve", false, Arguments.ToArray()))
			{
				P4.P4CommandResult Results = Cmd.Run();

				if (Results.Success)
				{

					if (Results.InfoOutput.Count == 0)
					{
						Serilog.Log.Logger.Information("Perforce: Unexpected info output when reshelving change");
						throw new Exception("Unexpected info output when reshelving change");
					}

					bool Error = true;
					int ReshelvedChange = 0;
					string Message = Results.InfoOutput[Results.InfoOutput.Count - 1].Message;
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

		/// <inheritdoc/>
		public virtual async Task<int> GetCodeChangeAsync(string ClusterName, string StreamName, int Change)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("PerforceService.GetCodeChangeAsync").StartActive();
			Scope.Span.SetTag("ClusterName", ClusterName);
			Scope.Span.SetTag("StreamName", StreamName);
			Scope.Span.SetTag("Change", Change);
			
			int MaxChange = Change;
			for (; ; )
			{
				// Query for the changes before this point
				List<ChangeSummary> Changes = await GetChangesAsync(ClusterName, StreamName, null, MaxChange, 10, null);
				Serilog.Log.Logger.Information("Finding last code change in {Stream} before {MaxChange}: {NumResults}", StreamName, MaxChange, Changes.Count);
				if (Changes.Count == 0)
				{
					return 0;
				}

				// Get the details for them
				List<ChangeDetails> DetailsList = await GetChangeDetailsAsync(ClusterName, StreamName, Changes.ConvertAll(x => x.Number), null);
				foreach (ChangeDetails Details in DetailsList.OrderByDescending(x => x.Number))
				{
					ChangeContentFlags ContentFlags = Details.GetContentFlags();
					Serilog.Log.Logger.Information("Change {Change} = {Flags}", Details.Number, ContentFlags.ToString());
					if ((Details.GetContentFlags() & ChangeContentFlags.ContainsCode) != 0)
					{
						return Details.Number;
					}
				}

				// Loop round again, adjusting our maximum changelist number
				MaxChange = Changes.Min(x => x.Number) - 1;
			}
		}

		/// <summary>
		/// Gets the wildcard filter for a particular range of changes
		/// </summary>
		/// <param name="BasePath">Base path to find files for</param>
		/// <param name="MinChange">Minimum changelist number to query</param>
		/// <param name="MaxChange">Maximum changelist number to query</param>
		/// <returns>Filter string</returns>
		public static string GetFilter(string BasePath, int? MinChange, int? MaxChange)
		{
			StringBuilder Filter = new StringBuilder(BasePath);
			if (MinChange != null && MaxChange != null)
			{
				Filter.Append(CultureInfo.InvariantCulture, $"@{MinChange},{MaxChange}");
			}
			else if (MinChange != null)
			{
				Filter.Append(CultureInfo.InvariantCulture, $"@>={MinChange}");
			}
			else if (MaxChange != null)
			{
				Filter.Append(CultureInfo.InvariantCulture, $"@<={MaxChange}");
			}
			return Filter.ToString();
		}

		/// <summary>
		/// Gets a stream-relative path from a depot path
		/// </summary>
		/// <param name="DepotFile">The depot file to check</param>
		/// <param name="StreamName">Name of the stream</param>
		/// <param name="RelativePath">Receives the relative path to the file</param>
		/// <returns>True if the stream-relative path was returned</returns>
		public static bool TryGetStreamRelativePath(string DepotFile, string StreamName, [NotNullWhen(true)] out string? RelativePath)
		{
			if (DepotFile.StartsWith(StreamName, StringComparison.OrdinalIgnoreCase) && DepotFile.Length > StreamName.Length && DepotFile[StreamName.Length] == '/')
			{
				RelativePath = DepotFile.Substring(StreamName.Length);
				return true;
			}

			Match Match = Regex.Match(DepotFile, "^//[^/]+/[^/]+(/.*)$");
			if (Match.Success)
			{
				RelativePath = Match.Groups[1].Value;
				return true;
			}

			RelativePath = null;
			return false;
		}
	}

}
