// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using Serilog;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using UnrealGameSync;

namespace UnrealGameSyncCmd
{
	using ILogger = Microsoft.Extensions.Logging.ILogger;

	sealed class UserErrorException : Exception
	{
		public LogEvent Event { get; }
		public int Code { get; }

		public UserErrorException(LogEvent Event)
			: base(Event.ToString())
		{
			this.Event = Event;
			this.Code = 1;
		}

		public UserErrorException(string Message, params object[] Args)
			: this(LogEvent.Create(LogLevel.Error, Message, Args))
		{
		}
	}

	public class Program
	{
		static BuildConfig EditorConfig => BuildConfig.Development;

		class CommandInfo
		{
			public string Name { get; }
			public Type Type { get; }
			public string Usage { get; }
			public string Brief { get; }

			public CommandInfo(string Name, Type Type, string Usage, string Brief)
			{
				this.Name = Name;
				this.Type = Type;
				this.Usage = Usage;
				this.Brief = Brief;
			}
		}

		static CommandInfo[] Commands =
		{
			new CommandInfo("init", typeof(InitCommand),
				"ugs init [stream-path] [-client=..] [-server=..] [-user=..] [-branch=..] [-project=..]",
				"Create a client for the given stream, or initializes an existing client for use by UGS."
			),
			new CommandInfo("switch", typeof(SwitchCommand),
				"ugs switch [project name|project path|stream]",
				"Changes the active project to the one in the workspace with the given name, or switches to a new stream."
			),
			new CommandInfo("changes", typeof(ChangesCommand),
				"ugs changes",
				"List recently submitted changes to the current branch."
			),
			new CommandInfo("config", typeof(ConfigCommand),
				"ugs config",
				"Updates the configuration for the current workspace."
			),
			new CommandInfo("filter", typeof(FilterCommand),
				"ugs filter [-reset] [-include=..] [-exclude=..] [-view=..] [-addview=..] [-removeview=..] [-global]",
				"Displays or updates the workspace or global sync filter"
			),
			new CommandInfo("sync", typeof(SyncCommand),
				"ugs sync [change|'latest'] [-build] [-remove] [-only]",
				"Syncs the current workspace to the given changelist, optionally removing all local state."
			),
			new CommandInfo("clients", typeof(ClientsCommand),
				"ugs clients",
				"Lists all clients suitable for use on the current machine."
			),
			new CommandInfo("run", typeof(RunCommand),
				"ugs run",
				"Runs the editor for the current branch."
			),
			new CommandInfo("build", typeof(BuildCommand),
				"ugs build [id] [-list]",
				"Runs the default build steps for the current project, or a particular step referenced by id."
			),
			new CommandInfo("status", typeof(StatusCommand),
				"ugs status [-update]",
				"Shows the status of the currently synced branch."
			),
			new CommandInfo("version", typeof(VersionCommand),
				"ugs version",
				"Prints the current application version"
			),
		};

		class CommandContext
		{
			public CommandLineArguments Arguments { get; }
			public ILogger Logger { get; }
			public ILoggerFactory LoggerFactory { get; }
			public GlobalSettingsFile UserSettings { get; }

			public CommandContext(CommandLineArguments Arguments, ILogger Logger, ILoggerFactory LoggerFactory, GlobalSettingsFile UserSettings)
			{
				this.Arguments = Arguments;
				this.Logger = Logger;
				this.LoggerFactory = LoggerFactory;
				this.UserSettings = UserSettings;
			}
		}

		class ServerOptions
		{
			[CommandLine("-Server=")]
			public string? ServerAndPort { get; set; }

			[CommandLine("-User=")]
			public string? UserName { get; set; }
		}

		class ProjectConfigOptions : ServerOptions
		{
			public void ApplyTo(UserWorkspaceSettings Settings)
			{
				if (ServerAndPort != null)
				{
					Settings.ServerAndPort = (ServerAndPort.Length == 0) ? null : ServerAndPort;
				}
				if (UserName != null)
				{
					Settings.UserName = (UserName.Length == 0) ? null : UserName;
				}
			}
		}

		class ProjectInitOptions : ProjectConfigOptions
		{
			[CommandLine("-Client=")]
			public string? ClientName { get; set; }

			[CommandLine("-Branch=")]
			public string? BranchPath { get; set; }

			[CommandLine("-Project=")]
			public string? ProjectName { get; set; }
		}

		public static async Task<int> Main(string[] RawArgs)
		{
			DirectoryReference GlobalConfigFolder;
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				GlobalConfigFolder = DirectoryReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.LocalApplicationData)!, "UnrealGameSync");
			}
			else
			{
				GlobalConfigFolder = DirectoryReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.UserProfile)!, ".config", "UnrealGameSync");
			}
			DirectoryReference.CreateDirectory(GlobalConfigFolder);

			string LogName;
			DirectoryReference LogFolder;
			if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
			{
				LogFolder = DirectoryReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.UserProfile)!, "Library", "Logs", "Unreal Engine", "UnrealGameSync");
				LogName = "UnrealGameSync-.log";
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
			{
				LogFolder = DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.UserProfile)!;
				LogName = ".ugs-.log";
			}
			else
			{
				LogFolder = GlobalConfigFolder;
				LogName = "UnrealGameSyncCmd-.log";
			}

			Serilog.ILogger SerilogLogger = new LoggerConfiguration()
				.Enrich.FromLogContext()
				.WriteTo.Console(Serilog.Events.LogEventLevel.Information, outputTemplate: "{Message:lj}{NewLine}")
				.WriteTo.File(FileReference.Combine(LogFolder, LogName).FullName, Serilog.Events.LogEventLevel.Debug, rollingInterval: RollingInterval.Day, rollOnFileSizeLimit: true, fileSizeLimitBytes: 20 * 1024 * 1024, retainedFileCountLimit: 10)
				.CreateLogger();

			using ILoggerFactory LoggerFactory = new Serilog.Extensions.Logging.SerilogLoggerFactory(SerilogLogger, true);
			ILogger Logger = LoggerFactory.CreateLogger("Main");
			try
			{
				GlobalSettingsFile Settings;
				if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
				{
					Settings = UserSettings.Create(GlobalConfigFolder, Logger);
				}
				else
				{
					Settings = GlobalSettingsFile.Create(FileReference.Combine(GlobalConfigFolder, "Global.json"));
				}

				CommandLineArguments Args = new CommandLineArguments(RawArgs);

				string? CommandName;
				if (!Args.TryGetPositionalArgument(out CommandName))
				{
					PrintHelp();
					return 0;
				}

				CommandInfo? Command = Commands.FirstOrDefault(x => x.Name.Equals(CommandName, StringComparison.OrdinalIgnoreCase));
				if (Command == null)
				{
					Logger.LogError($"unknown command '{CommandName}'");
					Console.WriteLine();
					PrintHelp();
					return 1;
				}

				Command Instance = (Command)Activator.CreateInstance(Command.Type)!;
				await Instance.ExecuteAsync(new CommandContext(Args, Logger, LoggerFactory, Settings));
				return 0;
			}
			catch (UserErrorException Ex)
			{
				Logger.Log(Ex.Event.Level, "{Message}", Ex.Event.ToString());
				return Ex.Code;
			}
			catch (PerforceException Ex)
			{
				Logger.LogError(Ex, "{Message}", Ex.Message);
				return 1;
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Unhandled exception.\n{Str}", Ex);
				return 1;
			}
		}

		static void PrintHelp()
		{
			Console.WriteLine("Usage:");
			foreach (CommandInfo Command in Commands)
			{
				Console.WriteLine();
				ConsoleUtils.WriteLineWithWordWrap(Command.Usage, 2, 8);
				ConsoleUtils.WriteLineWithWordWrap(Command.Brief, 4, 4);
			}
		}

		public static UserWorkspaceSettings? ReadOptionalUserWorkspaceSettings()
		{
			DirectoryReference? Dir = DirectoryReference.GetCurrentDirectory();
			for (; Dir != null; Dir = Dir.ParentDirectory)
			{
				try
				{
					UserWorkspaceSettings? Settings;
					if (UserWorkspaceSettings.TryLoad(Dir, out Settings))
					{
						return Settings;
					}
				}
				catch
				{
					// Guard against directories we can't access, eg. /Users/.ugs
				}
			}
			return null;
		}

		public static UserWorkspaceSettings ReadRequiredUserWorkspaceSettings()
		{
			UserWorkspaceSettings? Settings = ReadOptionalUserWorkspaceSettings();
			if (Settings == null)
			{
				throw new UserErrorException("Unable to find UGS workspace in current directory.");
			}
			return Settings;
		}

		public static async Task<UserWorkspaceState> ReadWorkspaceState(IPerforceConnection PerforceClient, UserWorkspaceSettings Settings, GlobalSettingsFile UserSettings, ILogger Logger)
		{
			UserWorkspaceState State = UserSettings.FindOrAddWorkspaceState(Settings, Logger);
			if (State.SettingsTimeUtc != Settings.LastModifiedTimeUtc)
			{
				Logger.LogDebug("Updating state due to modified settings timestamp");
				ProjectInfo Info = await ProjectInfo.CreateAsync(PerforceClient, Settings, CancellationToken.None);
				State.UpdateCachedProjectInfo(Info, Settings.LastModifiedTimeUtc);
				State.Save(Logger);
			}
			return State;
		}

		public static Task<IPerforceConnection> ConnectAsync(string? ServerAndPort, string? UserName, string? ClientName, ILoggerFactory LoggerFactory)
		{
			PerforceSettings Settings = new PerforceSettings(PerforceSettings.Default);
			Settings.ClientName = ClientName;
			Settings.PreferNativeClient = true;
			if (!String.IsNullOrEmpty(ServerAndPort))
			{
				Settings.ServerAndPort = ServerAndPort;
			}
			if (!String.IsNullOrEmpty(UserName))
			{
				Settings.UserName = UserName;
			}

			return PerforceConnection.CreateAsync(Settings, LoggerFactory.CreateLogger("Perforce"));
		}

		public static Task<IPerforceConnection> ConnectAsync(UserWorkspaceSettings Settings, ILoggerFactory LoggerFactory)
		{
			return ConnectAsync(Settings.ServerAndPort, Settings.UserName, Settings.ClientName, LoggerFactory);
		}

		static string[] ReadSyncFilter(UserWorkspaceSettings WorkspaceSettings, GlobalSettingsFile UserSettings, ConfigFile ProjectConfig)
		{
			Dictionary<Guid, WorkspaceSyncCategory> SyncCategories = ConfigUtils.GetSyncCategories(ProjectConfig);
			string[] CombinedSyncFilter = GlobalSettingsFile.GetCombinedSyncFilter(SyncCategories, UserSettings.Global.Filter, WorkspaceSettings.Filter);

			ConfigSection PerforceSection = ProjectConfig.FindSection("Perforce");
			if (PerforceSection != null)
			{
				IEnumerable<string> AdditionalPaths = PerforceSection.GetValues("AdditionalPathsToSync", new string[0]);
				CombinedSyncFilter = AdditionalPaths.Union(CombinedSyncFilter).ToArray();
			}

			return CombinedSyncFilter;
		}

		static async Task<string> FindProjectPathAsync(IPerforceConnection Perforce, string ClientName, string BranchPath, string? ProjectName)
		{
			using IPerforceConnection PerforceClient = await PerforceConnection.CreateAsync(new PerforceSettings(Perforce.Settings) { ClientName = ClientName }, Perforce.Logger);

			// Find or validate the selected project
			string SearchPath;
			if (ProjectName == null)
			{
				SearchPath = $"//{ClientName}{BranchPath}/*.uprojectdirs";
			}
			else if (ProjectName.Contains('.'))
			{
				SearchPath = $"//{ClientName}{BranchPath}/{ProjectName.TrimStart('/')}";
			}
			else
			{
				SearchPath = $"//{ClientName}{BranchPath}/.../{ProjectName}.uproject";
			}

			List<FStatRecord> ProjectFileRecords = await PerforceClient.FStatAsync(FStatOptions.ClientFileInPerforceSyntax, SearchPath).ToListAsync();
			ProjectFileRecords.RemoveAll(x => x.HeadAction == FileAction.Delete || x.HeadAction == FileAction.MoveDelete);
			ProjectFileRecords.RemoveAll(x => !x.IsMapped);

			List<string> Paths = ProjectFileRecords.Select(x => PerforceUtils.GetClientRelativePath(x.ClientFile!)).Distinct(StringComparer.Ordinal).ToList();
			if (Paths.Count == 0)
			{
				throw new UserErrorException("No project file found matching {SearchPath}", SearchPath);
			}
			if (Paths.Count > 1)
			{
				throw new UserErrorException("Multiple projects found matching {SearchPath}: {Paths}", SearchPath, String.Join(", ", Paths));
			}

			return "/" + Paths[0];
		}

		abstract class Command
		{
			public abstract Task ExecuteAsync(CommandContext Context);
		}

		class InitCommand : Command
		{
			public override async Task ExecuteAsync(CommandContext Context)
			{
				ILogger Logger = Context.Logger;

				// Get the positional argument indicating the file to look for
				string? InitName;
				Context.Arguments.TryGetPositionalArgument(out InitName);

				// Get the config settings from the command line
				ProjectInitOptions Options = new ProjectInitOptions();
				Context.Arguments.ApplyTo(Options);
				Context.Arguments.CheckAllArgumentsUsed();

				// Get the host name
				using IPerforceConnection Perforce = await ConnectAsync(Options.ServerAndPort, Options.UserName, null, Context.LoggerFactory);
				InfoRecord PerforceInfo = await Perforce.GetInfoAsync(InfoOptions.ShortOutput);
				string HostName = PerforceInfo.ClientHost ?? Dns.GetHostName();

				// Create the perforce connection
				if (InitName != null)
				{
					await InitNewClientAsync(Perforce, Context, InitName, HostName, Options, Logger);
				}
				else
				{
					await InitExistingClientAsync(Perforce, Context, HostName, Options, Logger);
				}
			}

			async Task InitNewClientAsync(IPerforceConnection Perforce, CommandContext Context, string StreamName, string HostName, ProjectInitOptions Options, ILogger Logger)
			{
				Logger.LogInformation("Checking stream...");

				// Get the given stream
				PerforceResponse<StreamRecord> StreamResponse = await Perforce.TryGetStreamAsync(StreamName, true);
				if (!StreamResponse.Succeeded)
				{
					throw new UserErrorException($"Unable to find stream '{StreamName}'");
				}
				StreamRecord Stream = StreamResponse.Data;

				// Get the new directory for the client
				DirectoryReference ClientDir = DirectoryReference.Combine(DirectoryReference.GetCurrentDirectory(), Stream.Stream.Replace('/', '+'));
				DirectoryReference.CreateDirectory(ClientDir);

				// Make up a new client name 
				string ClientName = Options.ClientName ?? Regex.Replace($"{Perforce.Settings.UserName}_{HostName}_{Stream.Stream.Trim('/')}", "[^0-9a-zA-Z_.-]", "+");

				// Check there are no existing clients under the current path
				List<ClientsRecord> Clients = await FindExistingClients(Perforce, HostName, ClientDir);
				if (Clients.Count > 0)
				{
					if (Clients.Count == 1 && ClientName.Equals(Clients[0].Name, StringComparison.OrdinalIgnoreCase) && ClientDir == TryParseRoot(Clients[0].Root))
					{
						Logger.LogInformation("Reusing existing client for {ClientDir} ({ClientName})", ClientDir, Options.ClientName);
					}
					else
					{
						throw new UserErrorException("Current directory is already within a Perforce workspace ({ClientName})", Clients[0].Name);
					}
				}

				// Create the new client
				ClientRecord Client = new ClientRecord(ClientName, Perforce.Settings.UserName, ClientDir.FullName);
				Client.Host = HostName;
				Client.Stream = Stream.Stream;
				Client.Options = ClientOptions.Rmdir;
				await Perforce.CreateClientAsync(Client);

				// Branch root is currently hard-coded at the root
				string BranchPath = Options.BranchPath ?? String.Empty;
				string ProjectPath = await FindProjectPathAsync(Perforce, ClientName, BranchPath, Options.ProjectName);

				// Create the settings object
				UserWorkspaceSettings Settings = new UserWorkspaceSettings();
				Settings.RootDir = ClientDir;
				Settings.Init(Perforce.Settings.ServerAndPort, Perforce.Settings.UserName, ClientName, BranchPath, ProjectPath);
				Options.ApplyTo(Settings);
				Settings.Save(Logger);

				Logger.LogInformation("Initialized {ClientName} with root at {RootDir}", ClientName, ClientDir);
			}

			static DirectoryReference? TryParseRoot(string Root)
			{
				try
				{
					return new DirectoryReference(Root);
				}
				catch
				{
					return null;
				}
			}

			async Task InitExistingClientAsync(IPerforceConnection Perforce, CommandContext Context, string HostName, ProjectInitOptions Options, ILogger Logger)
			{
				DirectoryReference CurrentDir = DirectoryReference.GetCurrentDirectory();

				// Make sure the client name is set
				string? ClientName = Options.ClientName;
				if (ClientName == null)
				{
					List<ClientsRecord> Clients = await FindExistingClients(Perforce, HostName, CurrentDir);
					if (Clients.Count == 0)
					{
						throw new UserErrorException("Unable to find client for {HostName} under {ClientDir}", HostName, CurrentDir);
					}
					if (Clients.Count > 1)
					{
						throw new UserErrorException("Multiple clients found for {HostName} under {ClientDir}: {ClientList}", HostName, CurrentDir, String.Join(", ", Clients.Select(x => x.Name)));
					}

					ClientName = Clients[0].Name;
					Logger.LogInformation("Found client {ClientName}", ClientName);
				}

				// Get the client info
				ClientRecord Client = await Perforce.GetClientAsync(ClientName);
				DirectoryReference ClientDir = new DirectoryReference(Client.Root);

				// If a project path was specified in local syntax, try to convert it to client-relative syntax
				string? ProjectName = Options.ProjectName;
				if (Options.ProjectName != null && Options.ProjectName.Contains('.'))
				{
					Options.ProjectName = FileReference.Combine(CurrentDir, Options.ProjectName).MakeRelativeTo(ClientDir).Replace('\\', '/');
				}

				// Branch root is currently hard-coded at the root
				string BranchPath = Options.BranchPath ?? String.Empty;
				string ProjectPath = await FindProjectPathAsync(Perforce, ClientName, BranchPath, ProjectName);

				// Create the settings object
				UserWorkspaceSettings Settings = new UserWorkspaceSettings();
				Settings.RootDir = ClientDir;
				Settings.Init(Perforce.Settings.ServerAndPort, Perforce.Settings.UserName, ClientName, BranchPath, ProjectPath);
				Options.ApplyTo(Settings);
				Settings.Save(Logger);

				Logger.LogInformation("Initialized workspace at {RootDir} for {ClientProject}", ClientDir, Settings.ClientProjectPath);
			}

			static async Task<List<ClientsRecord>> FindExistingClients(IPerforceConnection Perforce, string HostName, DirectoryReference ClientDir)
			{
				List<ClientsRecord> MatchingClients = new List<ClientsRecord>();

				List<ClientsRecord> Clients = await Perforce.GetClientsAsync(ClientsOptions.None, Perforce.Settings.UserName);
				foreach (ClientsRecord Client in Clients)
				{
					if (!String.IsNullOrEmpty(Client.Root) && !String.IsNullOrEmpty(Client.Host) && String.Compare(HostName, Client.Host, StringComparison.OrdinalIgnoreCase) == 0)
					{
						DirectoryReference? RootDir;
						try
						{
							RootDir = new DirectoryReference(Client.Root);
						}
						catch
						{
							RootDir = null;
						}

						if (RootDir != null && ClientDir.IsUnderDirectory(RootDir))
						{
							MatchingClients.Add(Client);
						}
					}
				}

				return MatchingClients;
			}
		}

		class SyncCommand : Command
		{
			class SyncOptions
			{
				[CommandLine("-Only")]
				public bool SingleChange { get; set; }

				[CommandLine("-Build")]
				public bool Build { get; set; }

				[CommandLine("-NoGPF", Value = "false")]
				[CommandLine("-NoProjectFiles", Value = "false")]
				public bool ProjectFiles { get; set; } = true;

				[CommandLine("-Clobber")]
				public bool Clobber { get; set; }

				[CommandLine("-Refilter")]
				public bool Refilter { get; set; }
			}

			public override async Task ExecuteAsync(CommandContext Context)
			{
				ILogger Logger = Context.Logger;
				Context.Arguments.TryGetPositionalArgument(out string? ChangeString);

				SyncOptions SyncOptions = new SyncOptions();
				Context.Arguments.ApplyTo(SyncOptions);

				Context.Arguments.CheckAllArgumentsUsed();

				UserWorkspaceSettings Settings = ReadRequiredUserWorkspaceSettings();
				using IPerforceConnection PerforceClient = await ConnectAsync(Settings, Context.LoggerFactory);
				UserWorkspaceState State = await ReadWorkspaceState(PerforceClient, Settings, Context.UserSettings, Logger);

				ChangeString ??= "latest";

				int Change;
				if (!int.TryParse(ChangeString, out Change))
				{
					if (String.Equals(ChangeString, "latest", StringComparison.OrdinalIgnoreCase))
					{
						List<ChangesRecord> Changes = await PerforceClient.GetChangesAsync(ChangesOptions.None, 1, ChangeStatus.Submitted, $"//{Settings.ClientName}/...");
						Change = Changes[0].Number;
					}
					else
					{
						throw new UserErrorException("Unknown change type for sync '{Change}'", ChangeString);
					}
				}

				WorkspaceUpdateOptions Options = SyncOptions.SingleChange? WorkspaceUpdateOptions.SyncSingleChange : WorkspaceUpdateOptions.Sync;
				if (SyncOptions.Build)
				{
					Options |= WorkspaceUpdateOptions.Build;
				}
				if (SyncOptions.ProjectFiles)
				{
					Options |= WorkspaceUpdateOptions.GenerateProjectFiles;
				}
				if (SyncOptions.Clobber)
				{
					Options |= WorkspaceUpdateOptions.Clobber;
				}
				if (SyncOptions.Refilter)
				{
					Options |= WorkspaceUpdateOptions.Refilter;
				}
				Options |= WorkspaceUpdateOptions.RemoveFilteredFiles;

				ProjectInfo ProjectInfo = State.CreateProjectInfo();
				UserProjectSettings ProjectSettings = Context.UserSettings.FindOrAddProjectSettings(ProjectInfo, Settings, Logger);

				ConfigFile ProjectConfig = await ConfigUtils.ReadProjectConfigFileAsync(PerforceClient, ProjectInfo, Logger, CancellationToken.None);
				string[] SyncFilter = ReadSyncFilter(Settings, Context.UserSettings, ProjectConfig);

				WorkspaceUpdateContext UpdateContext = new WorkspaceUpdateContext(Change, Options, BuildConfig.Development, SyncFilter, ProjectSettings.BuildSteps, null);

				WorkspaceUpdate Update = new WorkspaceUpdate(UpdateContext);
				(WorkspaceUpdateResult Result, string Message) = await Update.ExecuteAsync(PerforceClient.Settings, ProjectInfo, State, Context.Logger, CancellationToken.None);
				if (Result == WorkspaceUpdateResult.FilesToClobber)
				{
					Logger.LogWarning("The following files are modified in your workspace:");
					foreach (string File in UpdateContext.ClobberFiles.Keys.OrderBy(x => x))
					{
						Logger.LogWarning("  {File}", File);
					}
					Logger.LogWarning("Use -Clobber to overwrite");
				}
				else if (Result != WorkspaceUpdateResult.Success)
				{
					Logger.LogError("{Message} (Result: {Result})", Message, Result);
				}

				State.SetLastSyncState(Result, UpdateContext, Message);
				State.Save(Logger);
			}
		}

		class ClientsCommand : Command
		{
			public class ClientsOptions : ServerOptions
			{
			}

			public override async Task ExecuteAsync(CommandContext Context)
			{
				ILogger Logger = Context.Logger;

				ClientsOptions Options = Context.Arguments.ApplyTo<ClientsOptions>(Logger);
				Context.Arguments.CheckAllArgumentsUsed();

				using IPerforceConnection PerforceClient = await ConnectAsync(Options.ServerAndPort, Options.UserName, null, Context.LoggerFactory);
				InfoRecord Info = await PerforceClient.GetInfoAsync(InfoOptions.ShortOutput);

				List<ClientsRecord> Clients = await PerforceClient.GetClientsAsync(EpicGames.Perforce.ClientsOptions.None, PerforceClient.Settings.UserName);
				foreach (ClientsRecord Client in Clients)
				{
					if (String.Equals(Info.ClientHost, Client.Host, StringComparison.OrdinalIgnoreCase))
					{
						Logger.LogInformation("{Client,-50} {Root}", Client.Name, Client.Root);
					}
				}
			}
		}

		class RunCommand : Command
		{
			public override async Task ExecuteAsync(CommandContext Context)
			{
				ILogger Logger = Context.Logger;

				UserWorkspaceSettings Settings = ReadRequiredUserWorkspaceSettings();
				using IPerforceConnection PerforceClient = await ConnectAsync(Settings, Context.LoggerFactory);
				UserWorkspaceState State = await ReadWorkspaceState(PerforceClient, Settings, Context.UserSettings, Logger);

				ProjectInfo ProjectInfo = State.CreateProjectInfo();
				ConfigFile ProjectConfig = await ConfigUtils.ReadProjectConfigFileAsync(PerforceClient, ProjectInfo, Logger, CancellationToken.None);

				FileReference ReceiptFile = ConfigUtils.GetEditorReceiptFile(ProjectInfo, ProjectConfig, EditorConfig);
				Logger.LogDebug("Receipt file: {Receipt}", ReceiptFile);

				if (!ConfigUtils.TryReadEditorReceipt(ProjectInfo, ReceiptFile, out TargetReceipt? Receipt) || String.IsNullOrEmpty(Receipt.Launch))
				{
					throw new UserErrorException("The editor needs to be built before you can run it. (Missing {ReceiptFile}).", ReceiptFile);
				}
				if (!File.Exists(Receipt.Launch))
				{
					throw new UserErrorException("The editor needs to be built before you can run it. (Missing {LaunchFile}).", Receipt.Launch);
				}

				List<string> LaunchArguments = new List<string>();
				if (Settings.LocalProjectPath.HasExtension(".uproject"))
				{
					LaunchArguments.Add($"\"{Settings.LocalProjectPath}\"");
				}
				if (EditorConfig == BuildConfig.Debug || EditorConfig == BuildConfig.DebugGame)
				{
					LaunchArguments.Append(" -debug");
				}
				for (int Idx = 0; Idx < Context.Arguments.Count; Idx++)
				{
					if (!Context.Arguments.HasBeenUsed(Idx))
					{
						LaunchArguments.Add(Context.Arguments[Idx]);
					}
				}

				string CommandLine = CommandLineArguments.Join(LaunchArguments);
				Logger.LogInformation("Spawning: {LaunchFile} {CommandLine}", CommandLineArguments.Quote(Receipt.Launch), CommandLine);

				if (!Utility.SpawnProcess(Receipt.Launch, CommandLine))
				{
					Logger.LogError("Unable to spawn {0} {1}", Receipt.Launch, LaunchArguments.ToString());
				}
			}
		}

		class ChangesCommand : Command
		{
			public override async Task ExecuteAsync(CommandContext Context)
			{
				ILogger Logger = Context.Logger;

				int Count = Context.Arguments.GetIntegerOrDefault("-Count=", 10);
				int LineCount = Context.Arguments.GetIntegerOrDefault("-Lines=", 3);
				Context.Arguments.CheckAllArgumentsUsed(Context.Logger);

				UserWorkspaceSettings Settings = ReadRequiredUserWorkspaceSettings();
				using IPerforceConnection PerforceClient = await ConnectAsync(Settings, Context.LoggerFactory);

				List<ChangesRecord> Changes = await PerforceClient.GetChangesAsync(ChangesOptions.None, Count, ChangeStatus.Submitted, $"//{Settings.ClientName}/...");
				foreach(IEnumerable<ChangesRecord> ChangesBatch in Changes.Batch(10))
				{
					List<DescribeRecord> DescribeRecords = await PerforceClient.DescribeAsync(ChangesBatch.Select(x => x.Number).ToArray());

					Logger.LogInformation("  Change    Type     Author          Description");
					foreach (DescribeRecord DescribeRecord in DescribeRecords)
					{
						PerforceChangeDetails Details = new PerforceChangeDetails(DescribeRecord);

						string Type;
						if (Details.bContainsCode)
						{
							if (Details.bContainsContent)
							{
								Type = "Both";
							}
							else
							{
								Type = "Code";
							}
						}
						else
						{
							if (Details.bContainsContent)
							{
								Type = "Content";
							}
							else
							{
								Type = "None";
							}
						}

						string Author = StringUtils.Truncate(DescribeRecord.User, 15);

						List<string> Lines = StringUtils.WordWrap(Details.Description, Math.Max(ConsoleUtils.WindowWidth - 40, 10)).ToList();
						if (Lines.Count == 0)
						{
							Lines.Add(String.Empty);
						}

						Logger.LogInformation("  {Change,-9} {Type,-8} {Author,-15} {Description}", DescribeRecord.Number, Type, Author, Lines[0]);
						for (int LineIndex = 1; LineIndex < LineCount; LineIndex++)
						{
							Logger.LogInformation("                                     {Description}", Lines[LineIndex]);
						}
					}
				}
			}
		}

		class ConfigCommand : Command
		{
			public override Task ExecuteAsync(CommandContext Context)
			{
				ILogger Logger = Context.Logger;

				UserWorkspaceSettings Settings = ReadRequiredUserWorkspaceSettings();
				if (!Context.Arguments.GetUnusedArguments().Any())
				{
					ProcessStartInfo StartInfo = new ProcessStartInfo();
					StartInfo.FileName = Settings.ConfigFile.FullName;
					StartInfo.UseShellExecute = true;
					using (Process? Editor = Process.Start(StartInfo))
					{
						if (Editor != null)
						{
							Editor.WaitForExit();
						}
					}
				}
				else
				{
					ProjectConfigOptions Options = new ProjectConfigOptions();
					Context.Arguments.ApplyTo(Options);
					Context.Arguments.CheckAllArgumentsUsed(Context.Logger);

					Options.ApplyTo(Settings);
					Settings.Save(Logger);

					Logger.LogInformation("Updated {ConfigFile}", Settings.ConfigFile);
				}
				
				return Task.CompletedTask;
			}
		}

		class FilterCommand : Command
		{
			class FilterCommandOptions
			{
				[CommandLine("-Reset")]
				public bool Reset = false;

				[CommandLine("-Include=")]
				public List<string> Include { get; set; } = new List<string>();

				[CommandLine("-Exclude=")]
				public List<string> Exclude { get; set; } = new List<string>();

				[CommandLine("-View=", ListSeparator = ';')]
				public List<string>? View { get; set; } 

				[CommandLine("-AddView=", ListSeparator = ';')]
				public List<string> AddView { get; set; } = new List<string>();

				[CommandLine("-RemoveView=", ListSeparator = ';')]
				public List<string> RemoveView { get; set; } = new List<string>();

				[CommandLine("-AllProjects", Value = "true")]
				[CommandLine("-OnlyCurrent", Value = "false")]
				public bool? AllProjects = null;

				[CommandLine("-GpfAllProjects", Value ="true")]
				[CommandLine("-GpfOnlyCurrent", Value = "false")]
				public bool? AllProjectsInSln = null;

				[CommandLine("-Global")]
				public bool Global { get; set; }
			}

			public override async Task ExecuteAsync(CommandContext Context)
			{
				ILogger Logger = Context.Logger;

				UserWorkspaceSettings WorkspaceSettings = ReadRequiredUserWorkspaceSettings();
				using IPerforceConnection PerforceClient = await ConnectAsync(WorkspaceSettings, Context.LoggerFactory);
				UserWorkspaceState WorkspaceState = await ReadWorkspaceState(PerforceClient, WorkspaceSettings, Context.UserSettings, Logger);
				ProjectInfo ProjectInfo = WorkspaceState.CreateProjectInfo();

				ConfigFile ProjectConfig = await ConfigUtils.ReadProjectConfigFileAsync(PerforceClient, ProjectInfo, Logger, CancellationToken.None);
				Dictionary<Guid, WorkspaceSyncCategory> SyncCategories = ConfigUtils.GetSyncCategories(ProjectConfig);

				FilterSettings GlobalFilter = Context.UserSettings.Global.Filter;
				FilterSettings WorkspaceFilter = WorkspaceSettings.Filter;

				FilterCommandOptions Options = Context.Arguments.ApplyTo<FilterCommandOptions>(Logger);
				Context.Arguments.CheckAllArgumentsUsed(Context.Logger);

				if (Options.Global)
				{
					ApplyCommandOptions(Context.UserSettings.Global.Filter, Options, SyncCategories.Values, Logger);
					Context.UserSettings.Save(Logger);
				}
				else
				{
					ApplyCommandOptions(WorkspaceSettings.Filter, Options, SyncCategories.Values, Logger);
					WorkspaceSettings.Save(Logger);
				}

				Dictionary<Guid, bool> GlobalCategories = GlobalFilter.GetCategories();
				Dictionary<Guid, bool> WorkspaceCategories = WorkspaceFilter.GetCategories();

				Logger.LogInformation("Categories:");
				foreach (WorkspaceSyncCategory SyncCategory in SyncCategories.Values)
				{
					bool bEnabled;

					string Scope = "(Default)";
					if (GlobalCategories.TryGetValue(SyncCategory.UniqueId, out bEnabled))
					{
						Scope = "(Global)";
					}
					else if (WorkspaceCategories.TryGetValue(SyncCategory.UniqueId, out bEnabled))
					{
						Scope = "(Workspace)";
					}
					else
					{
						bEnabled = SyncCategory.bEnable;
					}

					Logger.LogInformation("  {Id,30} {Enabled,3} {Scope,-9} {Name}", SyncCategory.UniqueId, bEnabled? "Yes" : "No", Scope, SyncCategory.Name);
				}

				if (GlobalFilter.View.Count > 0)
				{
					Logger.LogInformation("");
					Logger.LogInformation("Global View:");
					foreach (string Line in GlobalFilter.View)
					{
						Logger.LogInformation("  {Line}", Line);
					}
				}
				if (WorkspaceFilter.View.Count > 0)
				{
					Logger.LogInformation("");
					Logger.LogInformation("Workspace View:");
					foreach (string Line in WorkspaceFilter.View)
					{
						Logger.LogInformation("  {Line}", Line);
					}
				}

				string[] Filter = ReadSyncFilter(WorkspaceSettings, Context.UserSettings, ProjectConfig);

				Logger.LogInformation("");
				Logger.LogInformation("Combined view:");
				foreach (string FilterLine in Filter)
				{
					Logger.LogInformation("  {FilterLine}", FilterLine);
				}
			}

			static void ApplyCommandOptions(FilterSettings Settings, FilterCommandOptions CommandOptions, IEnumerable<WorkspaceSyncCategory> SyncCategories, ILogger Logger)
			{
				if (CommandOptions.Reset)
				{
					Logger.LogInformation("Resetting settings...");
					Settings.Reset();
				}

				HashSet<Guid> IncludeCategories = new HashSet<Guid>(CommandOptions.Include.Select(x => GetCategoryId(x, SyncCategories)));
				HashSet<Guid> ExcludeCategories = new HashSet<Guid>(CommandOptions.Exclude.Select(x => GetCategoryId(x, SyncCategories)));

				Guid Id = IncludeCategories.FirstOrDefault(x => ExcludeCategories.Contains(x));
				if (Id != Guid.Empty)
				{
					throw new UserErrorException("Category {Id} cannot be both included and excluded", Id);
				}

				IncludeCategories.ExceptWith(Settings.IncludeCategories);
				Settings.IncludeCategories.AddRange(IncludeCategories);

				ExcludeCategories.ExceptWith(Settings.ExcludeCategories);
				Settings.ExcludeCategories.AddRange(ExcludeCategories);

				if (CommandOptions.View != null)
				{
					Settings.View = CommandOptions.View;
				}
				if (CommandOptions.RemoveView.Count > 0)
				{
					HashSet<string> ViewRemove = new HashSet<string>(CommandOptions.RemoveView, StringComparer.OrdinalIgnoreCase);
					Settings.View.RemoveAll(x => ViewRemove.Contains(x));
				}
				if (CommandOptions.AddView.Count > 0)
				{
					HashSet<string> ViewLines = new HashSet<string>(Settings.View, StringComparer.OrdinalIgnoreCase);
					Settings.View.AddRange(CommandOptions.AddView.Where(x => !ViewLines.Contains(x)));
				}

				Settings.AllProjects = CommandOptions.AllProjects ?? Settings.AllProjects;
				Settings.AllProjectsInSln = CommandOptions.AllProjectsInSln ?? Settings.AllProjectsInSln;
			}

			static Guid GetCategoryId(string Text, IEnumerable<WorkspaceSyncCategory> SyncCategories)
			{
				Guid Id;
				if (Guid.TryParse(Text, out Id))
				{
					return Id;
				}

				WorkspaceSyncCategory? Category = SyncCategories.FirstOrDefault(x => x.Name.Equals(Text, StringComparison.OrdinalIgnoreCase));
				if (Category != null)
				{
					return Category.UniqueId;
				}

				throw new UserErrorException("Unable to find category '{Category}'", Text);
			}
		}

		class BuildCommand : Command
		{
			public override async Task ExecuteAsync(CommandContext Context)
			{
				ILogger Logger = Context.Logger;
				Context.Arguments.TryGetPositionalArgument(out string? Target);
				bool ListOnly = Context.Arguments.HasOption("-List");
				Context.Arguments.CheckAllArgumentsUsed();

				UserWorkspaceSettings Settings = ReadRequiredUserWorkspaceSettings();
				using IPerforceConnection PerforceClient = await ConnectAsync(Settings, Context.LoggerFactory);
				UserWorkspaceState State = await ReadWorkspaceState(PerforceClient, Settings, Context.UserSettings, Logger);

				ProjectInfo ProjectInfo = State.CreateProjectInfo();

				if (ListOnly)
				{
					ConfigFile ProjectConfig = await ConfigUtils.ReadProjectConfigFileAsync(PerforceClient, ProjectInfo, Logger, CancellationToken.None);

					FileReference EditorTarget = ConfigUtils.GetEditorTargetFile(ProjectInfo, ProjectConfig);

					Dictionary<Guid, ConfigObject> BuildStepObjects = ConfigUtils.GetDefaultBuildStepObjects(ProjectInfo, EditorTarget.GetFileNameWithoutAnyExtensions(), EditorConfig, ProjectConfig, false);

					Logger.LogInformation("Available build steps:");
					Logger.LogInformation("");
					Logger.LogInformation("  Id                                   | Description                              | Type       | Enabled");
					Logger.LogInformation("  -------------------------------------|------------------------------------------|------------|-----------------");
					foreach (BuildStep BuildStep in BuildStepObjects.Values.Select(x => new BuildStep(x)).OrderBy(x => x.OrderIndex))
					{
						Logger.LogInformation("  {Id,-36} | {Name,-40} | {Type,-10} | {Enabled,-8}", BuildStep.UniqueId, BuildStep.Description, BuildStep.Type, BuildStep.bNormalSync);
					}
					return;
				}

				HashSet<Guid>? Steps = null;
				if (Target != null)
				{
					Guid Id;
					if (!Guid.TryParse(Target, out Id))
					{
						Logger.LogError("Unable to parse '{Target}' as a GUID. Pass -List to show all available build steps and their identifiers.", Target);
					}
					Steps = new HashSet<Guid> { Id };
				}

				WorkspaceUpdateContext UpdateContext = new WorkspaceUpdateContext(State.CurrentChangeNumber, WorkspaceUpdateOptions.Build, BuildConfig.Development, null, new List<ConfigObject>(), Steps);

				WorkspaceUpdate Update = new WorkspaceUpdate(UpdateContext);
				await Update.ExecuteAsync(PerforceClient.Settings, ProjectInfo, State, Context.Logger, CancellationToken.None);
			}
		}

		class StatusCommand : Command
		{
			public override async Task ExecuteAsync(CommandContext Context)
			{
				ILogger Logger = Context.Logger;
				bool bUpdate = Context.Arguments.HasOption("-Update");
				Context.Arguments.CheckAllArgumentsUsed();

				UserWorkspaceSettings Settings = ReadRequiredUserWorkspaceSettings();
				Logger.LogInformation("User: {UserName}", Settings.UserName);
				Logger.LogInformation("Server: {ServerAndPort}", Settings.ServerAndPort);
				Logger.LogInformation("Project: {ClientProjectPath}", Settings.ClientProjectPath);

				using IPerforceConnection PerforceClient = await ConnectAsync(Settings, Context.LoggerFactory);

				UserWorkspaceState State = await ReadWorkspaceState(PerforceClient, Settings, Context.UserSettings, Logger);
				if (bUpdate)
				{
					ProjectInfo NewProjectInfo = await ProjectInfo.CreateAsync(PerforceClient, Settings, CancellationToken.None);
					State.UpdateCachedProjectInfo(NewProjectInfo, Settings.LastModifiedTimeUtc);
				}

				string StreamOrBranchName = State.StreamName ?? Settings.BranchPath.TrimStart('/');
				if (State.LastSyncResultMessage == null)
				{
					Logger.LogInformation("Not currently synced to {Stream}", StreamOrBranchName);
				}
				else if (State.LastSyncResult == WorkspaceUpdateResult.Success)
				{
					Logger.LogInformation("Synced to {Stream} CL {Change}", StreamOrBranchName, State.LastSyncChangeNumber);
				}
				else
				{
					Logger.LogWarning("Last sync to {Stream} CL {Change} failed: {Result}", StreamOrBranchName, State.LastSyncChangeNumber, State.LastSyncResultMessage);
				}
			}
		}

		class SwitchCommand : Command
		{
			public override async Task ExecuteAsync(CommandContext Context)
			{
				// Get the positional argument indicating the file to look for
				string? TargetName;
				if (!Context.Arguments.TryGetPositionalArgument(out TargetName))
				{
					throw new UserErrorException("Missing stream or project name to switch to.");
				}

				bool Force = TargetName.StartsWith("//", StringComparison.Ordinal) && Context.Arguments.HasOption("-Force");

				// Finish argument parsing
				Context.Arguments.CheckAllArgumentsUsed();

				// Get a connection to the client for this workspace
				UserWorkspaceSettings Settings = ReadRequiredUserWorkspaceSettings();
				using IPerforceConnection PerforceClient = await ConnectAsync(Settings, Context.LoggerFactory);

				// Check whether we're switching stream or project
				if (TargetName.StartsWith("//", StringComparison.Ordinal))
				{
					await SwitchStreamAsync(PerforceClient, TargetName, Force, Context.Logger);
				}
				else
				{
					await SwitchProjectAsync(PerforceClient, Settings, TargetName, Context.Logger);
				}
			}

			public async Task SwitchStreamAsync(IPerforceConnection PerforceClient, string StreamName, bool Force, ILogger Logger)
			{
				if (!Force && await PerforceClient.OpenedAsync(OpenedOptions.None, FileSpecList.Any).AnyAsync())
				{
					throw new UserErrorException("Client {ClientName} has files opened. Use -Force to switch anyway.", PerforceClient.Settings.ClientName!);
				}

				await PerforceClient.SwitchClientToStreamAsync(StreamName, SwitchClientOptions.IgnoreOpenFiles);

				Logger.LogInformation("Switched to stream {StreamName}", StreamName);
			}

			public async Task SwitchProjectAsync(IPerforceConnection PerforceClient, UserWorkspaceSettings Settings, string ProjectName, ILogger Logger)
			{
				Settings.ProjectPath = await FindProjectPathAsync(PerforceClient, Settings.ClientName, Settings.BranchPath, ProjectName);
				Settings.Save(Logger);
				Logger.LogInformation("Switched to project {ProjectPath}", Settings.ClientProjectPath);
			}
		}

		class VersionCommand : Command
		{
			public override Task ExecuteAsync(CommandContext Context)
			{
				ILogger Logger = Context.Logger;
 
				AssemblyInformationalVersionAttribute? Version = Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>();
				Logger.LogInformation("UnrealGameSync {Version}", Version?.InformationalVersion ?? "Unknown");

				return Task.CompletedTask;
			}
		}
	}
}
