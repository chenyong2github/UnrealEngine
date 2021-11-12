// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Controllers;
using HordeServer.Models;
using HordeServer.Notifications;
using HordeServer.Utilities;
using Microsoft.AspNetCore.StaticFiles;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Reflection;

using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;
using System.Globalization;
using HordeServer.Storage;
using System.Text.Json.Serialization;

namespace HordeServer.Services
{
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using NamespaceId = StringId<INamespace>;
	using BucketId = StringId<IBucket>;

	/// <summary>
	/// Polls Perforce for stream config changes
	/// </summary>
	public class ConfigService : ElectedBackgroundService
	{
		const string FileScheme = "file";
		const string PerforceScheme = "p4-cluster";

		/// <summary>
		/// Config file version number
		/// </summary>
		const int Version = 5;

		/// <summary>
		/// Database service
		/// </summary>
		DatabaseService DatabaseService;

		/// <summary>
		/// Project service
		/// </summary>
		ProjectService ProjectService;

		/// <summary>
		/// Stream service
		/// </summary>
		StreamService StreamService;

		/// <summary>
		/// Instance of the perforce service
		/// </summary>
		IPerforceService PerforceService;

		/// <summary>
		/// Load balancer instance
		/// </summary>
		private PerforceLoadBalancer PerforceLoadBalancer;

		/// <summary>
		/// Instance of the notification service
		/// </summary>
		INotificationService NotificationService;

		/// <summary>
		/// The namespace collection
		/// </summary>
		INamespaceCollection NamespaceCollection;

		/// <summary>
		/// The bucket collection
		/// </summary>
		IBucketCollection BucketCollection;

		/// <summary>
		/// Singleton instance of the pool service
		/// </summary>
		private readonly PoolService PoolService;

		/// <summary>
		/// The server settings
		/// </summary>
		IOptionsMonitor<ServerSettings> Settings;

		/// <summary>
		/// Logging device
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService"></param>
		/// <param name="PerforceService"></param>
		/// <param name="PerforceLoadBalancer"></param>
		/// <param name="ProjectService"></param>
		/// <param name="StreamService"></param>
		/// <param name="NotificationService"></param>
		/// <param name="NamespaceCollection"></param>
		/// <param name="BucketCollection"></param>
		/// <param name="PoolService"></param>
		/// <param name="Settings"></param>
		/// <param name="Logger"></param>
		public ConfigService(DatabaseService DatabaseService, IPerforceService PerforceService, ProjectService ProjectService, StreamService StreamService, INotificationService NotificationService, INamespaceCollection NamespaceCollection, IBucketCollection BucketCollection, PoolService PoolService, PerforceLoadBalancer PerforceLoadBalancer, IOptionsMonitor<ServerSettings> Settings, ILogger<ConfigService> Logger)
			: base(DatabaseService, new ObjectId("5ff60549e7632b15e64ac2f7"), Logger)
		{
			this.DatabaseService = DatabaseService;
			this.PerforceService = PerforceService;
			this.PerforceLoadBalancer = PerforceLoadBalancer;
			this.ProjectService = ProjectService;
			this.StreamService = StreamService;
			this.NotificationService = NotificationService;
			this.NamespaceCollection = NamespaceCollection;
			this.BucketCollection = BucketCollection;
			this.PoolService = PoolService;
			this.Settings = Settings;
			this.Logger = Logger;

			// This will trigger if the local Horde.json user configuration is changed
			this.Settings.OnChange(OnUserConfigUpdated);
		}

		GlobalConfig? CachedGlobalConfig;
		string? CachedGlobalConfigRevision;
		Dictionary<ProjectId, (ProjectConfig Config, string Revision)> CachedProjectConfigs = new Dictionary<ProjectId, (ProjectConfig, string)>();
		Dictionary<ProjectId, string?> CachedLogoRevisions = new Dictionary<ProjectId, string?>();

		async Task UpdateConfigAsync(Uri ConfigPath)
		{
			// Update the globals singleton
			GlobalConfig GlobalConfig;
			for (; ; )
			{
				Dictionary<Uri, string> GlobalRevisions = await FindRevisionsAsync(new[] { ConfigPath });
				if (GlobalRevisions.Count == 0)
				{
					throw new Exception($"Invalid config path: {ConfigPath}");
				}

				string Revision = GlobalRevisions.First().Value;
				if (CachedGlobalConfig == null || Revision != CachedGlobalConfigRevision)
				{
					Logger.LogInformation("Caching global config from {Revision}", Revision);
					try
					{
						CachedGlobalConfig = await ReadDataAsync<GlobalConfig>(ConfigPath);
						CachedGlobalConfigRevision = Revision;
					}
					catch (Exception Ex)
					{
						await SendFailureNotificationAsync(Ex, ConfigPath);
						return;
					}
				}
				GlobalConfig = CachedGlobalConfig;

				Globals Globals = await DatabaseService.GetGlobalsAsync();
				if (Globals.ConfigRevision == Revision)
				{
					Logger.LogInformation("Updating configuration from {ConfigPath}", Globals.ConfigRevision);
					break;
				}

				Globals.ConfigRevision = Revision;
				Globals.Notices = CachedGlobalConfig.Notices;
				Globals.PerforceClusters = CachedGlobalConfig.PerforceClusters;
				Globals.ScheduledDowntime = CachedGlobalConfig.Downtime;
				Globals.MaxConformCount = CachedGlobalConfig.MaxConformCount;

				await UpdateStorageConfigAsync(GlobalConfig.Storage);

				if (await DatabaseService.TryUpdateSingletonAsync(Globals))
				{
					break;
				}
			}

			// Projects to remove
			List<IProject> Projects = await ProjectService.GetProjectsAsync();

			// Get the path to all the project configs
			List<(ProjectConfigRef ProjectRef, Uri Path)> ProjectConfigs = GlobalConfig.Projects.Select(x => (x, CombinePaths(ConfigPath, x.Path))).ToList();

			Dictionary<ProjectId, (ProjectConfig Config, string Revision)> PrevCachedProjectConfigs = CachedProjectConfigs;
			CachedProjectConfigs = new Dictionary<ProjectId, (ProjectConfig, string)>();

			List<(ProjectId ProjectId, Uri Path)> ProjectLogos = new List<(ProjectId ProjectId, Uri Path)>();

			List<(ProjectId ProjectId, StreamConfigRef StreamRef, Uri Path)> StreamConfigs = new List<(ProjectId, StreamConfigRef, Uri)>();

			// List of project ids that were not able to be updated. We will avoid removing any existing project or stream definitions for these.
			HashSet<ProjectId> SkipProjectIds = new HashSet<ProjectId>();

			// Update any existing projects
			Dictionary<Uri, string> ProjectRevisions = await FindRevisionsAsync(ProjectConfigs.Select(x => x.Path));
			for (int Idx = 0; Idx < ProjectConfigs.Count; Idx++)
			{
				// Make sure we were able to fetch metadata for 
				(ProjectConfigRef ProjectRef, Uri ProjectPath) = ProjectConfigs[Idx];
				if (!ProjectRevisions.TryGetValue(ProjectPath, out string? Revision))
				{
					Logger.LogWarning("Unable to update project {ProjectId} due to missing revision information", ProjectRef.Id);
					SkipProjectIds.Add(ProjectRef.Id);
					continue;
				}

				IProject? Project = Projects.FirstOrDefault(x => x.Id == ProjectRef.Id);
				bool Update = (Project == null || Project.ConfigPath != ProjectPath.ToString() || Project.ConfigRevision != Revision);

				ProjectConfig? ProjectConfig;
				if (!Update && PrevCachedProjectConfigs.TryGetValue(ProjectRef.Id, out (ProjectConfig Config, string Revision) Result) && Result.Revision == Revision)
				{
					ProjectConfig = Result.Config;
				}
				else
				{
					Logger.LogInformation("Caching configuration for project {ProjectId} ({Revision})", ProjectRef.Id, Revision);
					try
					{
						ProjectConfig = await ReadDataAsync<ProjectConfig>(ProjectPath);
						if (Update)
						{
							Logger.LogInformation("Updating configuration for project {ProjectId} ({Revision})", ProjectRef.Id, Revision);
							await ProjectService.Collection.AddOrUpdateAsync(ProjectRef.Id, ProjectPath.ToString(), Revision, Idx, ProjectConfig);
						}
					}
					catch (Exception Ex)
					{
						await SendFailureNotificationAsync(Ex, ProjectPath);
						SkipProjectIds.Add(ProjectRef.Id);
						continue;
					}
				}

				if (ProjectConfig.Logo != null)
				{
					ProjectLogos.Add((ProjectRef.Id, CombinePaths(ProjectPath, ProjectConfig.Logo)));
				}

				CachedProjectConfigs[ProjectRef.Id] = (ProjectConfig, Revision);
				StreamConfigs.AddRange(ProjectConfig.Streams.Select(x => (ProjectRef.Id, x, CombinePaths(ProjectPath, x.Path))));
			}

			// Get the logo revisions
			Dictionary<Uri, string> LogoRevisions = await FindRevisionsAsync(ProjectLogos.Select(x => x.Path));
			for (int Idx = 0; Idx < ProjectLogos.Count; Idx++)
			{
				(ProjectId ProjectId, Uri Path) = ProjectLogos[Idx];
				if (LogoRevisions.TryGetValue(Path, out string? Revision))
				{
					string? CurrentRevision;
					if (!CachedLogoRevisions.TryGetValue(ProjectId, out CurrentRevision))
					{
						CurrentRevision = (await ProjectService.Collection.GetLogoAsync(ProjectId))?.Revision;
						CachedLogoRevisions[ProjectId] = CurrentRevision;
					}
					if (Revision != CurrentRevision)
					{
						Logger.LogInformation("Updating logo for project {ProjectId} ({Revision})", ProjectId, Revision);
						try
						{
							await ProjectService.Collection.SetLogoAsync(ProjectId, Path.ToString(), Revision, GetMimeTypeFromPath(Path), await ReadDataAsync(Path));
							CachedLogoRevisions[ProjectId] = Revision;
						}
						catch (Exception Ex)
						{
							await SendFailureNotificationAsync(Ex, Path);
							continue;
						}
					}
				}
			}

			// Get the current streams
			List<IStream> Streams = await StreamService.GetStreamsAsync();

			// Get the revisions for all the stream documents
			Dictionary<Uri, string> StreamRevisions = await FindRevisionsAsync(StreamConfigs.Select(x => x.Path));
			for (int Idx = 0; Idx < StreamConfigs.Count; Idx++)
			{
				(ProjectId ProjectId, StreamConfigRef StreamRef, Uri StreamPath) = StreamConfigs[Idx];
				if (StreamRevisions.TryGetValue(StreamPath, out string? Revision))
				{
					IStream? Stream = Streams.FirstOrDefault(x => x.Id == StreamRef.Id);
					if (Stream == null || Stream.ConfigPath != StreamPath.ToString() || Stream.ConfigRevision != Revision)
					{
						Logger.LogInformation("Updating configuration for stream {StreamRef} ({Revision})", StreamRef.Id, Revision);
						try
						{
							StreamConfig StreamConfig = await ReadDataAsync<StreamConfig>(StreamPath);
							Stream = await StreamService.StreamCollection.CreateOrReplaceAsync(StreamRef.Id, Stream, StreamPath.ToString(), Revision, ProjectId, StreamConfig);
						}
						catch (Exception Ex)
						{
							await SendFailureNotificationAsync(Ex, StreamPath);
							continue;
						}
					}
				}
			}

			// Remove any projects which are no longer used
			HashSet<ProjectId> RemoveProjectIds = new HashSet<ProjectId>(Projects.Select(x => x.Id));
			RemoveProjectIds.ExceptWith(ProjectConfigs.Select(y => y.ProjectRef.Id));

			foreach (ProjectId RemoveProjectId in RemoveProjectIds)
			{
				Logger.LogInformation("Removing project {ProjectId}", RemoveProjectId);
				await ProjectService.DeleteProjectAsync(RemoveProjectId);
			}

			// Remove any streams that are no longer used
			HashSet<StreamId> RemoveStreamIds = new HashSet<StreamId>(Streams.Where(x => !SkipProjectIds.Contains(x.ProjectId)).Select(x => x.Id));
			RemoveStreamIds.ExceptWith(StreamConfigs.Select(x => x.StreamRef.Id));

			foreach (StreamId RemoveStreamId in RemoveStreamIds)
			{
				Logger.LogInformation("Removing stream {StreamId}", RemoveStreamId);
				await StreamService.DeleteStreamAsync(RemoveStreamId);
			}
		}

		async Task UpdateStorageConfigAsync(StorageConfig? Config)
		{
			List<INamespace> Namespaces = await NamespaceCollection.FindAsync();
			List<NamespaceId> RemoveNamespaceIds = Namespaces.ConvertAll(x => x.Id);

			if (Config != null)
			{
				foreach (NamespaceConfig NamespaceConfig in Config.Namespaces)
				{
					NamespaceId NamespaceId = new NamespaceId(NamespaceConfig.Id);
					RemoveNamespaceIds.Remove(NamespaceId);
					await NamespaceCollection.AddOrUpdateAsync(NamespaceId, NamespaceConfig);

					List<IBucket> Buckets = await BucketCollection.FindAsync(NamespaceId);
					List<BucketId> RemoveBucketIds = Buckets.ConvertAll(x => x.BucketId);

					foreach(BucketConfig BucketConfig in NamespaceConfig.Buckets)
					{
						BucketId BucketId = new BucketId(BucketConfig.Id);
						await BucketCollection.AddOrUpdateAsync(NamespaceId, BucketId, BucketConfig);
						RemoveBucketIds.Remove(BucketId);
					}

					foreach (BucketId RemoveBucketId in RemoveBucketIds)
					{
						Logger.LogInformation("Removing bucket {BucketId}", RemoveBucketId);
						await BucketCollection.RemoveAsync(NamespaceId, RemoveBucketId);
					}
				}
			}

			foreach (NamespaceId RemoveNamespaceId in RemoveNamespaceIds)
			{
				Logger.LogInformation("Removing namespace {NamespaceId}", RemoveNamespaceId);
				await NamespaceCollection.RemoveAsync(RemoveNamespaceId);
			}
		}

		static FileExtensionContentTypeProvider ContentTypeProvider = new FileExtensionContentTypeProvider();

		static string GetMimeTypeFromPath(Uri Path)
		{
			string? ContentType;
			if (!ContentTypeProvider.TryGetContentType(Path.AbsolutePath, out ContentType))
			{
				ContentType = "application/octet-stream";
			}
			return ContentType;
		}

		static Uri CombinePaths(Uri BaseUri, string Path)
		{
			if (Path.StartsWith("//", StringComparison.Ordinal))
			{
				if (BaseUri.Scheme == PerforceScheme)
				{
					return new Uri($"{PerforceScheme}://{BaseUri.Host}{Path}");
				}
				else
				{
					return new Uri($"{PerforceScheme}://{PerforceCluster.DefaultName}{Path}");
				}
			}
			return new Uri(BaseUri, Path);
		}

		async Task<Dictionary<Uri, string>> FindRevisionsAsync(IEnumerable<Uri> Paths)
		{
			Dictionary<Uri, string> Revisions = new Dictionary<Uri, string>();

			// Find all the Perforce uris
			List<Uri> PerforcePaths = new List<Uri>();
			foreach (Uri Path in Paths)
			{
				if (Path.Scheme == FileScheme)
				{
					Revisions[Path] = $"ver={Version},md5={ContentHash.MD5(new FileReference(Path.LocalPath))}";
				}
				else if (Path.Scheme == PerforceScheme)
				{
					PerforcePaths.Add(Path);
				}
				else
				{
					throw new Exception($"Invalid path format: {Path}");
				}
			}

			// Query all the Perforce revisions
			foreach (IGrouping<string, Uri> PerforcePath in PerforcePaths.GroupBy(x => x.Host, StringComparer.OrdinalIgnoreCase))
			{
				List<FileSummary> Files = await PerforceService.FindFilesAsync(PerforcePath.Key, PerforcePath.Select(x => x.AbsolutePath));
				foreach (FileSummary File in Files)
				{
					Uri FileUri = new Uri($"{PerforceScheme}://{PerforcePath.Key}{File.DepotPath}");
					if (File.Error == null)
					{
						Revisions[FileUri] = $"ver={Version},chg={File.Change},path={FileUri}";
					}
					else
					{
						NotificationService.NotifyConfigUpdateFailure(File.Error, File.DepotPath);
					}
				}
			}

			return Revisions;
		}

		async Task<T> ReadDataAsync<T>(Uri ConfigPath) where T : class
		{
			byte[] Data = await ReadDataAsync(ConfigPath);

			JsonSerializerOptions Options = new JsonSerializerOptions();
			Startup.ConfigureJsonSerializer(Options);

			return JsonSerializer.Deserialize<T>(Data, Options)!;
		}

		Task<byte[]> ReadDataAsync(Uri ConfigPath)
		{
			switch(ConfigPath.Scheme)
			{
				case FileScheme:
					return File.ReadAllBytesAsync(ConfigPath.LocalPath);
				case PerforceScheme:
					return PerforceService.PrintAsync(ConfigPath.Host, ConfigPath.AbsolutePath);
				default:
					throw new Exception($"Invalid config path: {ConfigPath}");
			}
		}

		async Task SendFailureNotificationAsync(Exception Ex, Uri ConfigPath)
		{
			Logger.LogError(Ex, "Unable to read data from {ConfigPath}: {Message}", ConfigPath, Ex.Message);

			string FileName = ConfigPath.AbsolutePath;
			int Change = -1;
			IUser? Author = null;
			string? Description = null;

			if (ConfigPath.Scheme == PerforceScheme)
			{
				try
				{
					List<FileSummary> Files = await PerforceService.FindFilesAsync(ConfigPath.Host, new[] { FileName });
					Change = Files[0].Change;

					List<ChangeSummary> Changes = await PerforceService.GetChangesAsync(ConfigPath.Host, Change, Change, 1);
					if (Changes.Count > 0 && Changes[0].Number == Change)
					{
						(Author, Description) = (Changes[0].Author, Changes[0].Description);
					}
				}
				catch (Exception Ex2)
				{
					Logger.LogError(Ex2, "Unable to identify change that last modified {ConfigPath} from Perforce", ConfigPath);
				}
			}

			NotificationService.NotifyConfigUpdateFailure(Ex.Message, FileName, Change, Author, Description);
		}

		/// <inheritdoc/>
		protected override async Task<DateTime> TickLeaderAsync(CancellationToken StoppingToken)
		{

			Uri? ConfigUri = null;
			
			if (Path.IsPathRooted(Settings.CurrentValue.ConfigPath) && !Settings.CurrentValue.ConfigPath.StartsWith("//", StringComparison.Ordinal))
			{
				// absolute path to config
				ConfigUri = new Uri(Settings.CurrentValue.ConfigPath);					
			}
			else if (Settings.CurrentValue.ConfigPath != null)
			{
				// relative (development) or perforce path
				ConfigUri = CombinePaths(new Uri(FileReference.Combine(Program.AppDir, "_").FullName), Settings.CurrentValue.ConfigPath);				
			}

			if (ConfigUri != null)
			{
				await UpdateConfigAsync(ConfigUri);
			}
		
			return DateTime.UtcNow + TimeSpan.FromMinutes(1.0);
		}

		//
		// On premises configuration handling (aka Horde.json settings, though needs to have perforce global config handled too, checkout/modify/submit)
		//

		/// <summary>
		/// Update the global configuration
		/// </summary>
		/// <param name="Request"></param>
		/// <returns></returns>
		public async Task<bool> UpdateGlobalConfig(UpdateGlobalConfigRequest Request)
		{
			if (Settings.CurrentValue.ConfigPath == null || !Path.IsPathRooted(Settings.CurrentValue.ConfigPath) || Settings.CurrentValue.ConfigPath.StartsWith("//", StringComparison.Ordinal))
			{
				throw new Exception($"Global config path must be rooted for service updates.  (Perforce paths are not currently supported for live updates), {Settings.CurrentValue.ConfigPath}");
			}
			
			FileReference GlobalConfigFile = new FileReference(Settings.CurrentValue.ConfigPath);
			DirectoryReference GlobalConfigDirectory = GlobalConfigFile.Directory;

			// make sure the directory exists
			if (!DirectoryReference.Exists(GlobalConfigDirectory))
			{
				DirectoryReference.CreateDirectory(GlobalConfigDirectory);
			}

			bool ConfigDirty = false;

			// projects
			if (Request.ProjectsJson != null)
			{
				ConfigDirty = true;

				// write out the projects
				foreach (KeyValuePair<string, string> Project in Request.ProjectsJson)
				{
					FileReference.WriteAllText(FileReference.Combine(GlobalConfigDirectory, Project.Key), Project.Value);

					if (Request.ProjectLogo != null)
					{
						Byte[] bytes = Convert.FromBase64String(Request.ProjectLogo);
						FileReference.WriteAllBytes(FileReference.Combine(GlobalConfigDirectory, Project.Key.Replace(".json", ".png", StringComparison.OrdinalIgnoreCase)), bytes);
					}
				}

			}

			// streams
			if (Request.StreamsJson != null)
			{
				ConfigDirty = true;

				// write out the streams
				foreach (KeyValuePair<string, string> Stream in Request.StreamsJson)
				{
					FileReference.WriteAllText(FileReference.Combine(GlobalConfigDirectory, Stream.Key), Stream.Value);
				}
			}

			// update global config path
			if (!string.IsNullOrEmpty(Request.GlobalsJson))
			{
				ConfigDirty = true;

				FileReference.WriteAllText(GlobalConfigFile, Request.GlobalsJson);
			}

			if (ConfigDirty == true)
			{				
				await UpdateConfigAsync(new Uri("file://" + GlobalConfigFile.ToString()));				
			}

			// create the default pool 
			if (Request.DefaultPoolName != null)
			{
				PoolId PoolIdValue = PoolId.Sanitize(Request.DefaultPoolName);

				IPool? Pool = await PoolService.GetPoolAsync(PoolIdValue);
				if (Pool == null)
				{
					await PoolService.CreatePoolAsync(Request.DefaultPoolName, Condition: "OSFamily == 'Windows'", Properties: new Dictionary<string, string>() { ["Color"] = "0" });
				}
			}

			return true;

		}

		/// <summary>
		/// Update the server settings
		/// </summary>
		/// <param name="Request"></param>
		/// <returns></returns>
		public async Task<ServerUpdateResponse> UpdateServerSettings(UpdateServerSettingsRequest Request)
		{

			ServerUpdateResponse Response = new ServerUpdateResponse();

			Dictionary<string, object> NewUserSettings = new Dictionary<string, object>();

			try
			{

				if (Request.Settings == null || Request.Settings.Count == 0)
				{
					return Response;
				}

				if (UserConfigUpdated != null)
				{
					Response.Errors.Add("User configuation already being updated");
					return Response;
				}

				// Load the current user configuration
				IConfiguration Config = new ConfigurationBuilder()
					.AddJsonFile(Program.UserConfigFile.FullName, optional: true)
					.Build();

				ServerSettings UserSettings = new ServerSettings();
				Config.GetSection("Horde").Bind(UserSettings);

				// Figure out current and new settings
				HashSet<string> UserProps = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

				if (FileReference.Exists(Program.UserConfigFile))
				{
					byte[] Data = await FileReference.ReadAllBytesAsync(Program.UserConfigFile);

					JsonDocument Document = JsonDocument.Parse(Data);
					JsonElement HordeElement;

					if (Document.RootElement.TryGetProperty("Horde", out HordeElement))
					{
						foreach (JsonProperty Property in HordeElement.EnumerateObject())
						{
							UserProps.Add(Property.Name);
						}
					}
				}

				foreach (string Name in Request.Settings.Keys)
				{
					UserProps.Add(Name);
				}

				// Apply the changes from the request
				foreach (KeyValuePair<string, object> Pair in Request.Settings)
				{
					PropertyInfo? Property = UserSettings.GetType().GetProperty(Pair.Key, BindingFlags.IgnoreCase | BindingFlags.Public | BindingFlags.Instance);
					if (Property == null)
					{
						Response.Errors.Add($"Horde configuration property {Pair.Key} does not exist when reading server settings");
						Logger.LogError(Response.Errors.Last());
						continue;
					}

					if (Property.SetMethod == null)
					{
						Response.Errors.Add($"Horde configuration property {Pair.Key} does not have a set method");
						Logger.LogError(Response.Errors.Last());
						continue;
					}

					// explicit setting removal
					if (Pair.Value == null)
					{
						UserProps.Remove(Pair.Key);
						continue;
					}					


					JsonElement Element = (JsonElement)Pair.Value;

					object? Value = null;

					switch (Element.ValueKind)
					{
						case JsonValueKind.True:
							Value = true;
							break;
						case JsonValueKind.False:
							Value = false;
							break;
						case JsonValueKind.Number:
							Value = Element.GetDouble();
							break;
						case JsonValueKind.String:
							Value = Element.GetString();
							break;
					}

					// unable to map type
					if (Value == null)
					{
						Response.Errors.Add($"Unable to map type for Property {Property.Name}");
						continue;
					}					

					// handle common conversions
					if (Property.GetType() != Value.GetType())
					{
						try
						{
							Value =	Convert.ChangeType(Value, Property.PropertyType, CultureInfo.CurrentCulture);						
						}
						catch (Exception Ex)
						{
							Response.Errors.Add($"Property {Property.Name} raised exception during conversion, {Ex.Message}");
							continue;							
						}
						
						if (Value == null)
						{
							Response.Errors.Add($"Property {Property.Name} had null value on conversion");
							continue;							
						}
					}
					else 
					{
						Response.Errors.Add($"Property {Property.Name} is not assignable to {Value.ToString()}");
						continue;
					}
					

					//  Set the value, providing some validation
					try
					{
						Property.SetMethod.Invoke(UserSettings, new object[] { Value });
					}
					catch (Exception Ex)
					{
						Response.Errors.Add($"Exception updating property {Pair.Key}, {Ex.Message}");
					}
					
				}

				// Construct the new user settings
				foreach (string Name in UserProps)
				{
					PropertyInfo? Property = UserSettings.GetType().GetProperty(Name, BindingFlags.IgnoreCase | BindingFlags.Public | BindingFlags.Instance);
					if (Property == null)
					{
						Response.Errors.Add($"Horde configuration property {Name} does not exist when writing server settings");
						Logger.LogError(Response.Errors.Last());

						continue;
					}

					if (Property.GetMethod == null)
					{
						Response.Errors.Add($"Horde configuration property {Name} does not have a get method");
						Logger.LogError(Response.Errors.Last());
						continue;
					}

					object? Result = Property.GetMethod.Invoke(UserSettings, null);

					if (Result == null)
					{
						Response.Errors.Add($"Horde configuration property {Name} was null while writing and should have already been filtered out");
						Logger.LogError(Response.Errors.Last());
						continue;
					}

					NewUserSettings.Add(Property.Name, Result);

				}
			}
			catch (Exception Ex)
			{
				Response.Errors.Add($"Exception while updating settings: {Ex.Message}");
				Logger.LogError(Ex, "{Error}", Response.Errors.Last());
			}

			if (Response.Errors.Count != 0)
			{
				return Response;
			}

			Dictionary<string, object> NewLocalSettings = new Dictionary<string, object>();
			NewLocalSettings["Horde"] = NewUserSettings;

			try
			{
				UserConfigUpdated = new TaskCompletionSource<bool>();

				// This will trigger a setting update as the user config json is set to reload on change
				try
				{
					await FileReference.WriteAllBytesAsync(Program.UserConfigFile, JsonSerializer.SerializeToUtf8Bytes(NewLocalSettings, new JsonSerializerOptions { WriteIndented = true, DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull }));
				}
				catch (Exception Ex)
				{
					Response.Errors.Add($"Unable to serialize json settings to {Program.UserConfigFile.ToString()}, {Ex.Message}");
					Logger.LogError(Ex, "Unable to serialize json settings to {ConfigFile}, {Message}", Program.UserConfigFile.ToString(), Ex.Message);
				}

				if (Response.Errors.Count == 0)
				{
					if (await Task.WhenAny(UserConfigUpdated.Task, Task.Delay(5000)) != UserConfigUpdated.Task)
					{
						Response.Errors.Add("Server update timed out while awaiting write");
						Logger.LogError("Server update timed out after writing config");
					}
				}

			}
			finally
			{
				UserConfigUpdated = null;
			}

			return Response;

		}
		void OnUserConfigUpdated(ServerSettings Settings, string Name)
		{
			NumUserConfigUpdates++;
			UserConfigUpdated?.SetResult(true);
		}

		private TaskCompletionSource<bool>? UserConfigUpdated;

		/// <summary>
		/// The number of server configuration updates that have been made while the server is running
		/// This is used to detect whether server may need to be restarted
		/// </summary>
		public int NumUserConfigUpdates { get; private set; } = 0;

	}
}
