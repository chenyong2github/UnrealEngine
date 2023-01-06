// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;
using Horde.Build.Utilities;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Build.Configuration
{
	using PerforceConnectionId = StringId<PerforceConnectionSettings>;

	/// <summary>
	/// Accessor for a specific revision of a config file. Provides metadata about the current revision, and allows reading its data.
	/// </summary>
	public interface IConfigData
	{
		/// <summary>
		/// URI of the config file
		/// </summary>
		Uri Uri { get; }

		/// <summary>
		/// String used to identify a specific version of the config data. Used for ordinal comparisons, otherwise opaque.
		/// </summary>
		string Revision { get; }

		/// <summary>
		/// Reads data for the config file
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>UTF-8 encoded data for the config file</returns>
		ValueTask<ReadOnlyMemory<byte>> ReadAsync(CancellationToken cancellationToken);
	}

	/// <summary>
	/// Source for reading config files
	/// </summary>
	public interface IConfigSource
	{
		/// <summary>
		/// URI scheme for this config source
		/// </summary>
		string Scheme { get; }

		/// <summary>
		/// Reads a config file from this source
		/// </summary>
		/// <param name="uris">Locations of the config files to query</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Config file data</returns>
		Task<IConfigData[]> GetAsync(Uri[] uris, CancellationToken cancellationToken);
	}

	/// <summary>
	/// Extension methods for <see cref="IConfigSource"/>
	/// </summary>
	public static class ConfigSource
	{
		/// <summary>
		/// Gets a single config file from a source
		/// </summary>
		/// <param name="source">Source to query</param>
		/// <param name="uri">Location of the config file to query</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Config file data</returns>
		public static async Task<IConfigData> GetAsync(this IConfigSource source, Uri uri, CancellationToken cancellationToken)
		{
			IConfigData[] result = await source.GetAsync(new[] { uri }, cancellationToken);
			return result[0];
		}
	}

	/// <summary>
	/// In-memory config file source
	/// </summary>
	public sealed class InMemoryConfigSource : IConfigSource
	{
		class ConfigFileRevisionImpl : IConfigData
		{
			public Uri Uri { get; }
			public string Revision { get; }
			public ReadOnlyMemory<byte> Data { get; }

			public ConfigFileRevisionImpl(Uri uri, string version, ReadOnlyMemory<byte> data)
			{
				Uri = uri;
				Revision = version;
				Data = data;
			}

			public ValueTask<ReadOnlyMemory<byte>> ReadAsync(CancellationToken cancellationToken) => new ValueTask<ReadOnlyMemory<byte>>(Data);
		}

		readonly Dictionary<Uri, ConfigFileRevisionImpl> _files = new Dictionary<Uri, ConfigFileRevisionImpl>();

		/// <summary>
		/// Name of the scheme for this source
		/// </summary>
		public const string Scheme = "memory";

		/// <inheritdoc/>
		string IConfigSource.Scheme => Scheme;

		/// <summary>
		/// Manually adds a new config file
		/// </summary>
		/// <param name="path">Path to the config file</param>
		/// <param name="data">Config file data</param>
		public void Add(Uri path, ReadOnlyMemory<byte> data)
		{
			_files.Add(path, new ConfigFileRevisionImpl(path, "v1", data));
		}

		/// <inheritdoc/>
		public Task<IConfigData[]> GetAsync(Uri[] uris, CancellationToken cancellationToken)
		{
			IConfigData[] result = new IConfigData[uris.Length];
			for (int idx = 0; idx < uris.Length; idx++)
			{
				ConfigFileRevisionImpl? configFile;
				if (!_files.TryGetValue(uris[idx], out configFile))
				{
					throw new FileNotFoundException($"Config file {uris[idx]} not found.");
				}
				result[idx] = configFile;
			}
			return Task.FromResult(result);
		}
	}

	/// <summary>
	/// Config file source which reads from the filesystem
	/// </summary>
	public sealed class FileConfigSource : IConfigSource
	{
		class ConfigFileImpl : IConfigData
		{
			public Uri Uri { get; }
			public string Revision { get; }
			public DateTime LastWriteTimeUtc { get; }
			public ReadOnlyMemory<byte> Data { get; }

			public ConfigFileImpl(Uri uri, DateTime lastWriteTimeUtc, ReadOnlyMemory<byte> data)
			{
				Uri = uri;
				Revision = $"timestamp={lastWriteTimeUtc.Ticks}";
				LastWriteTimeUtc = lastWriteTimeUtc;
				Data = data;
			}

			public ValueTask<ReadOnlyMemory<byte>> ReadAsync(CancellationToken cancellationToken) => new ValueTask<ReadOnlyMemory<byte>>(Data);
		}

		/// <summary>
		/// Name of the scheme for this source
		/// </summary>
		public const string Scheme = "file";

		/// <inheritdoc/>
		string IConfigSource.Scheme => Scheme;

		readonly DirectoryReference _baseDir;
		readonly ConcurrentDictionary<FileReference, ConfigFileImpl> _files = new ConcurrentDictionary<FileReference, ConfigFileImpl>();

		/// <summary>
		/// Constructor
		/// </summary>
		public FileConfigSource()
			: this(DirectoryReference.GetCurrentDirectory())
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="baseDir">Base directory for resolving relative paths</param>
		public FileConfigSource(DirectoryReference baseDir)
		{
			_baseDir = baseDir;
		}

		/// <inheritdoc/>
		public async Task<IConfigData[]> GetAsync(Uri[] uris, CancellationToken cancellationToken)
		{
			IConfigData[] files = new IConfigData[uris.Length];
			for (int idx = 0; idx < uris.Length; idx++)
			{
				Uri uri = uris[idx];
				FileReference localPath = FileReference.Combine(_baseDir, uri.LocalPath);

				ConfigFileImpl? file;
				for (; ; )
				{
					if (_files.TryGetValue(localPath, out file))
					{
						if (FileReference.GetLastWriteTimeUtc(localPath) == file.LastWriteTimeUtc)
						{
							break;
						}
						else
						{
							_files.TryRemove(new KeyValuePair<FileReference, ConfigFileImpl>(localPath, file));
						}
					}

					using (FileStream stream = FileReference.Open(localPath, FileMode.Open, FileAccess.Read, FileShare.Read))
					{
						using MemoryStream memoryStream = new MemoryStream();
						await stream.CopyToAsync(memoryStream, cancellationToken);
						DateTime lastWriteTime = FileReference.GetLastWriteTimeUtc(localPath);
						file = new ConfigFileImpl(uri, lastWriteTime, memoryStream.ToArray());
					}

					if (_files.TryAdd(localPath, file))
					{
						break;
					}
				}

				files[idx] = file;
			}
			return files;
		}
	}

	/// <summary>
	/// Perforce cluster config file source
	/// </summary>
	public sealed class PerforceConfigSource : IConfigSource
	{
		class ConfigFileImpl : IConfigData
		{
			public Uri Uri { get; }
			public int Change { get; }
			public string Revision { get; }

			readonly PerforceConfigSource _owner;

			public ConfigFileImpl(Uri uri, int change, PerforceConfigSource owner)
			{
				Uri = uri;
				Change = change;
				Revision = $"{change}";
				_owner = owner;
			}

			public ValueTask<ReadOnlyMemory<byte>> ReadAsync(CancellationToken cancellationToken) => _owner.ReadAsync(Uri, Change, cancellationToken);
		}

		/// <summary>
		/// Name of the scheme for this source
		/// </summary>
		public const string Scheme = "perforce";

		/// <inheritdoc/>
		string IConfigSource.Scheme => Scheme;

		readonly IOptionsMonitor<ServerSettings> _settings;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="settings">Global settings</param>
		/// <param name="logger">Logger instance</param>
		public PerforceConfigSource(IOptionsMonitor<ServerSettings> settings, ILogger<PerforceConfigSource> logger)
		{
			_settings = settings;
			_logger = logger;
		}

		/// <inheritdoc/>
		public async Task<IConfigData[]> GetAsync(Uri[] uris, CancellationToken cancellationToken)
		{
			Dictionary<Uri, IConfigData> results = new Dictionary<Uri, IConfigData>();
			foreach (IGrouping<string, Uri> group in uris.GroupBy(x => x.Host))
			{
				using (IPerforceConnection perforce = await ConnectAsync(group.Key))
				{
					FileSpecList fileSpec = group.Select(x => x.AbsolutePath).Distinct(StringComparer.OrdinalIgnoreCase).ToList();

					List<FStatRecord> records = await perforce.FStatAsync(FStatOptions.ShortenOutput, fileSpec, cancellationToken).ToListAsync(cancellationToken);
					records.RemoveAll(x => x.HeadAction == FileAction.Delete || x.HeadAction == FileAction.MoveDelete);

					Dictionary<string, FStatRecord> absolutePathToRecord = records.ToDictionary(x => x.DepotFile ?? String.Empty, x => x, StringComparer.OrdinalIgnoreCase);
					foreach (Uri uri in group)
					{
						FStatRecord? record;
						if (!absolutePathToRecord.TryGetValue(uri.AbsolutePath, out record))
						{
							throw new FileNotFoundException($"Unable to read {uri}. No matching files found.");
						}
						results[uri] = new ConfigFileImpl(uri, record.HeadChange, this);
					}
				}
			}
			return uris.ConvertAll(x => results[x]);
		}

		async ValueTask<ReadOnlyMemory<byte>> ReadAsync(Uri uri, int change, CancellationToken cancellationToken)
		{
			using (IPerforceConnection perforce = await ConnectAsync(uri.Host))
			{
				PerforceResponse<PrintRecord<byte[]>> response = await perforce.TryPrintDataAsync($"{uri.AbsolutePath}@{change}", cancellationToken);
				response.EnsureSuccess();
				return response.Data.Contents!;
			}
		}

		async Task<IPerforceConnection> ConnectAsync(string host)
		{
			ServerSettings settings = _settings.CurrentValue;

			PerforceConnectionId connectionId = new PerforceConnectionId();
			if (!String.IsNullOrEmpty(host))
			{
				connectionId = new PerforceConnectionId(host);
			}

			PerforceConnectionSettings? connectionSettings = settings.Perforce.FirstOrDefault(x => x.Id == connectionId);
			if (connectionSettings == null)
			{
				if (connectionId == PerforceConnectionSettings.Default)
				{
					connectionSettings = new PerforceConnectionSettings();
				}
				else
				{
					throw new InvalidOperationException($"No Perforce connection settings defined for '{connectionId}'.");
				}
			}

			return await PerforceConnection.CreateAsync(connectionSettings.ToPerforceSettings(), _logger);
		}
	}
}
