// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Amazon.Runtime.Internal.Endpoints.StandardLibrary;
using EpicGames.Core;
using EpicGames.Redis;
using Horde.Build.Projects;
using Horde.Build.Server;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.Extensions.Primitives;
using ProtoBuf;
using StackExchange.Redis;

namespace Horde.Build.Configuration
{
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Service which processes runtime configuration data.
	/// </summary>
	public sealed class ConfigService : IOptionsFactory<GlobalConfig>, IOptionsChangeTokenSource<GlobalConfig>, IHostedService, IDisposable
	{
		// Index to all current config files 
		[ProtoContract]
		class ConfigSnapshot
		{
			[ProtoMember(1)]
			public byte[] Global = null!;

			[ProtoMember(2)]
			public Dictionary<ProjectId, byte[]> Projects { get; set; } = new Dictionary<ProjectId, byte[]>();

			[ProtoMember(3)]
			public Dictionary<StreamId, byte[]> Streams { get; set; } = new Dictionary<StreamId, byte[]>();

			[ProtoMember(4)]
			public Dictionary<Uri, string> Dependencies { get; set; } = new Dictionary<Uri, string>();
		}

		// Implements a token for config change notifications
		class ChangeToken : IChangeToken
		{
			class Registration : IDisposable
			{
				public Action<object>? _callback;
				public object? _state;
				public Registration? _next;

				public void Dispose() => _callback = null;
			}

			static readonly Registration s_sentinel = new Registration();

			Registration? _firstRegistration = null;

			public bool ActiveChangeCallbacks => true;
			public bool HasChanged => _firstRegistration != s_sentinel;

			public void TriggerChange()
			{
				for (Registration? firstRegistration = _firstRegistration; firstRegistration != s_sentinel; firstRegistration = _firstRegistration)
				{
					if (Interlocked.CompareExchange(ref _firstRegistration, s_sentinel, firstRegistration) == firstRegistration)
					{
						for (; firstRegistration != null; firstRegistration = firstRegistration._next)
						{
							Action<object>? callback = firstRegistration._callback;
							if (callback != null)
							{
								callback(firstRegistration._state!);
							}
						}
						break;
					}
				}
			}

			public IDisposable RegisterChangeCallback(Action<object> callback, object state)
			{
				Registration registration = new Registration { _callback = callback, _state = state };
				for (; ; )
				{
					Registration? nextRegistration = _firstRegistration;
					registration._next = nextRegistration;

					if (nextRegistration == s_sentinel)
					{
						callback(state);
						break;
					}
					if (Interlocked.CompareExchange(ref _firstRegistration, registration, nextRegistration) == nextRegistration)
					{
						break;
					}
				}
				return registration;
			}
		}

		// The current config
		record class ConfigState(IoHash Hash, GlobalConfig GlobalConfig);

		readonly RedisService _redisService;
		readonly ServerSettings _settings;
		readonly Dictionary<string, IConfigSource> _sources;
		readonly JsonSerializerOptions _jsonOptions;
		readonly RedisKey _snapshotKey = "config";

		readonly ITicker _ticker;
		readonly TimeSpan _tickInterval = TimeSpan.FromSeconds(10.0);

		readonly RedisChannel _updateChannel = "config-update";
		readonly BackgroundTask _updateTask;

		Task<ConfigState> _stateTask;
		ChangeToken? _currentChangeToken;

		/// <inheritdoc/>
		string IOptionsChangeTokenSource<GlobalConfig>.Name => String.Empty;

		/// <summary>
		/// Constructor
		/// </summary>
		public ConfigService(RedisService redisService, IOptions<ServerSettings> settings, IEnumerable<IConfigSource> sources, IClock clock, ILogger<ConfigService> logger)
		{
			_redisService = redisService;
			_settings = settings.Value;
			_sources = sources.ToDictionary(x => x.Scheme, x => x, StringComparer.OrdinalIgnoreCase);

			_jsonOptions = new JsonSerializerOptions();
			Startup.ConfigureJsonSerializer(_jsonOptions);

			_ticker = clock.AddSharedTicker<ConfigService>(_tickInterval, TickSharedAsync, logger);

			_updateTask = new BackgroundTask(WaitForUpdatesAsync);

			_stateTask = Task.Run(() => GetStartupStateAsync());
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_updateTask.Dispose();
			_ticker.Dispose();
		}

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken cancellationToken)
		{
			await _ticker.StartAsync();
			_updateTask.Start();
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken cancellationToken)
		{
			await _updateTask.StopAsync();
			await _ticker.StopAsync();
		}

		/// <inheritdoc/>
		public GlobalConfig Create(string name) => _stateTask.Result.GlobalConfig;

		/// <inheritdoc/>
		public IChangeToken GetChangeToken()
		{
			ChangeToken? newToken = null;
			for (; ; )
			{
				ChangeToken? token = _currentChangeToken;
				if (token != null)
				{
					return token;
				}

				newToken ??= new ChangeToken();

				if (Interlocked.CompareExchange(ref _currentChangeToken, newToken, null) == null)
				{
					return newToken;
				}
			}
		}

		async Task<ConfigState> GetStartupStateAsync()
		{
			ReadOnlyMemory<byte> data = ReadOnlyMemory<byte>.Empty;
			if (!_settings.ForceConfigUpdateOnStartup)
			{
				data = await ReadSnapshotDataAsync();
			}
			if (data.Length == 0)
			{
				data = SerializeSnapshot(await CreateSnapshotAsync(CancellationToken.None));
			}
			return new ConfigState(IoHash.Compute(data.Span), CreateGlobalConfig(data));
		}

		async ValueTask TickSharedAsync(CancellationToken cancellationToken)
		{
			// Read a new copy of the current snapshot in the context of being elected to perform updates. This
			// avoids any latency with receiving notifications on the pub/sub channel about changes.
			ReadOnlyMemory<byte> initialData = await ReadSnapshotDataAsync();

			// Update the snapshot until we're asked to stop
			ConfigSnapshot? snapshot = (initialData.Length > 0) ? DeserializeSnapshot(initialData) : null;
			while (!cancellationToken.IsCancellationRequested)
			{
				if (snapshot == null || await IsOutOfDateAsync(snapshot, cancellationToken))
				{
					snapshot = await CreateSnapshotAsync(cancellationToken);
					await WriteSnapshotAsync(snapshot);
				}
				await Task.Delay(_tickInterval, cancellationToken);
			}
		}

		async Task WaitForUpdatesAsync(CancellationToken cancellationToken)
		{
			AsyncEvent updateEvent = new AsyncEvent();
			await using (RedisSubscription subscription = await _redisService.SubscribeAsync(_updateChannel, _ => updateEvent.Pulse()))
			{
				Task cancellationTask = Task.Delay(-1, cancellationToken);
				while (!cancellationToken.IsCancellationRequested)
				{
					Task updateTask = updateEvent.Task;

					RedisValue value = await _redisService.GetDatabase().StringGetAsync(_snapshotKey);
					if (!value.IsNullOrEmpty)
					{
						await UpdateConfigObjectAsync(value);
					}

					await await Task.WhenAny(updateTask, cancellationTask);
				}
			}
		}

		/// <summary>
		/// Create a new snapshot object
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New config snapshot</returns>
		async Task<ConfigSnapshot> CreateSnapshotAsync(CancellationToken cancellationToken)
		{
			// Get the path to the root config file
			Uri globalConfigUri;
			if (Path.IsPathRooted(_settings.ConfigPath) && !_settings.ConfigPath.StartsWith("//", StringComparison.Ordinal))
			{
				// absolute path to config
				globalConfigUri = new Uri(_settings.ConfigPath);
			}
			else
			{
				// relative (development) or perforce path
				globalConfigUri = ConfigType.CombinePaths(new Uri(FileReference.Combine(Program.AppDir, "_").FullName), _settings.ConfigPath);
			}

			// Read the config files
			ConfigContext context = new ConfigContext(_jsonOptions, _sources);
			try
			{
				ConfigSnapshot snapshot = new ConfigSnapshot();

				GlobalConfig globalConfig = await ConfigType.ReadAsync<GlobalConfig>(globalConfigUri, context, cancellationToken);
				snapshot.Global = JsonSerializer.SerializeToUtf8Bytes(globalConfig, context.JsonOptions);

				foreach (ProjectConfigRef projectConfigRef in globalConfig.Projects)
				{
					context.PropertyPathToFile.Clear();

					ProjectConfig projectConfig = await ConfigType.ReadAsync<ProjectConfig>(new Uri(projectConfigRef.Path), context, cancellationToken);
					snapshot.Projects.Add(projectConfigRef.Id, JsonSerializer.SerializeToUtf8Bytes(projectConfig, context.JsonOptions));

					foreach (StreamConfigRef streamConfigRef in projectConfig.Streams)
					{
						context.PropertyPathToFile.Clear();

						StreamConfig streamConfig = await ConfigType.ReadAsync<StreamConfig>(new Uri(streamConfigRef.Path), context, cancellationToken);
						snapshot.Streams.Add(streamConfigRef.Id, JsonSerializer.SerializeToUtf8Bytes(streamConfig, context.JsonOptions));
					}
				}

				// Save all the dependencies
				foreach ((Uri depUri, IConfigData depData) in context.Files)
				{
					snapshot.Dependencies.Add(depUri, depData.Revision);
				}

				return snapshot;
			}
			catch (Exception ex) when (ex is not ConfigException)
			{
				throw new ConfigException(context, "Internal exception while parsing config files", ex);
			}
		}

		/// <summary>
		/// Checks if the given snapshot is out of date
		/// </summary>
		/// <param name="snapshot">The current config snapshot</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>True if the snapshot is out of date</returns>
		async Task<bool> IsOutOfDateAsync(ConfigSnapshot snapshot, CancellationToken cancellationToken)
		{
			// Group the dependencies by scheme in order to allow the source to batch-query them
			foreach(IGrouping<string, KeyValuePair<Uri, string>> group in snapshot.Dependencies.GroupBy(x => x.Key.Scheme))
			{
				KeyValuePair<Uri, string>[] pairs = group.ToArray();

				IConfigSource? source;
				if (!_sources.TryGetValue(group.Key, out source))
				{
					return true;
				}

				IConfigData[] data = await source.GetAsync(pairs.ConvertAll(x => x.Key), cancellationToken);
				for (int idx = 0; idx < pairs.Length; idx++)
				{
					if (!data[idx].Revision.Equals(pairs[idx].Value, StringComparison.Ordinal))
					{
						return true;
					}
				}
			}
			return false;
		}

		async Task WriteSnapshotAsync(ConfigSnapshot index)
		{
			ReadOnlyMemory<byte> data = SerializeSnapshot(index);
			await _redisService.GetDatabase().StringSetAsync(_snapshotKey, data);
			await _redisService.PublishAsync(_updateChannel, RedisValue.EmptyString);
		}

		async Task<ReadOnlyMemory<byte>> ReadSnapshotDataAsync()
		{
			RedisValue value = await _redisService.GetDatabase().StringGetAsync(_snapshotKey);
			return value.IsNullOrEmpty ? ReadOnlyMemory<byte>.Empty : (ReadOnlyMemory<byte>)value;
		}

		static ReadOnlyMemory<byte> SerializeSnapshot(ConfigSnapshot snapshot)
		{
			using (MemoryStream outputStream = new MemoryStream())
			{
				using GZipStream zipStream = new GZipStream(outputStream, CompressionMode.Compress, false);
				ProtoBuf.Serializer.Serialize(zipStream, snapshot);
				zipStream.Flush();
				return outputStream.ToArray();
			}
		}

		static ConfigSnapshot DeserializeSnapshot(ReadOnlyMemory<byte> data)
		{
			using (ReadOnlyMemoryStream outputStream = new ReadOnlyMemoryStream(data))
			{
				using GZipStream zipStream = new GZipStream(outputStream, CompressionMode.Decompress, false);
				return ProtoBuf.Serializer.Deserialize<ConfigSnapshot>(zipStream);
			}
		}

		async Task UpdateConfigObjectAsync(ReadOnlyMemory<byte> data)
		{
			ConfigState state = await _stateTask;

			// Hash the data and only update if it changes; this prevents any double-updates due to time between initialization and the hosted service starting.
			IoHash hash = IoHash.Compute(data.Span);
			if (hash != state.Hash)
			{
				// Update the config object
				GlobalConfig globalConfig = CreateGlobalConfig(data);
				_stateTask = Task.FromResult(new ConfigState(hash, globalConfig));

				// Notify any watchers of the new config object
				for (; ; )
				{
					ChangeToken? token = _currentChangeToken;
					if (Interlocked.CompareExchange(ref _currentChangeToken, null, token) == token)
					{
						token?.TriggerChange();
						break;
					}
				}
			}
		}

		GlobalConfig CreateGlobalConfig(ReadOnlyMemory<byte> data)
		{
			// Decompress the snapshot object
			ConfigSnapshot snapshot = DeserializeSnapshot(data);

			// Build the new config and store the project and stream configs inside it
			GlobalConfig globalConfig = JsonSerializer.Deserialize<GlobalConfig>(snapshot.Global, _jsonOptions)!;
			foreach (ProjectConfigRef projectConfigRef in globalConfig.Projects)
			{
				ProjectConfig projectConfig = JsonSerializer.Deserialize<ProjectConfig>(snapshot.Projects[projectConfigRef.Id], _jsonOptions)!;
				projectConfigRef.Target = projectConfig;

				foreach (StreamConfigRef streamConfigRef in projectConfig.Streams)
				{
					StreamConfig streamConfig = JsonSerializer.Deserialize<StreamConfig>(snapshot.Streams[streamConfigRef.Id], _jsonOptions)!;
					streamConfigRef.Target = streamConfig;
				}
			}
			return globalConfig;
		}
	}
}
