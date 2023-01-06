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
using Horde.Build.Perforce;

namespace Horde.Build.Configuration
{
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
		/// <param name="uri">Location of the config file</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Config file data</returns>
		Task<IConfigData> GetAsync(Uri uri, CancellationToken cancellationToken);
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
		public Task<IConfigData> GetAsync(Uri uri, CancellationToken cancellationToken)
		{
			ConfigFileRevisionImpl? configFile;
			if (!_files.TryGetValue(uri, out configFile))
			{
				throw new FileNotFoundException($"Config file {uri} not found.");
			}
			return Task.FromResult<IConfigData>(configFile);
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
		public async Task<IConfigData> GetAsync(Uri uri, CancellationToken cancellationToken)
		{
			FileReference localPath = FileReference.Combine(_baseDir, uri.LocalPath);

			ConfigFileImpl? file;
			for(; ;)
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

			return file;
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
		public const string Scheme = "p4-cluster";

		/// <inheritdoc/>
		string IConfigSource.Scheme => Scheme;

		readonly IPerforceService _perforceService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="perforceService">Perforce service instance</param>
		public PerforceConfigSource(IPerforceService perforceService)
		{
			_perforceService = perforceService;
		}

		/// <inheritdoc/>
		public async Task<IConfigData> GetAsync(Uri uri, CancellationToken cancellationToken)
		{
			using (IPerforceConnection perforce = await _perforceService.ConnectAsync(uri.Host, null, cancellationToken))
			{
				List<FStatRecord> files = await perforce.FStatAsync(FStatOptions.ShortenOutput, uri.AbsolutePath, cancellationToken).ToListAsync(cancellationToken);
				files.RemoveAll(x => x.HeadAction == FileAction.Delete || x.HeadAction == FileAction.MoveDelete);

				if (files.Count == 0)
				{
					throw new FileNotFoundException($"Unable to read {uri}. No matching files found.");
				}

				return new ConfigFileImpl(uri, files[0].HeadChange, this);
			}
		}

		async ValueTask<ReadOnlyMemory<byte>> ReadAsync(Uri uri, int change, CancellationToken cancellationToken)
		{
			using (IPerforceConnection perforce = await _perforceService.ConnectAsync(uri.Host, null, cancellationToken))
			{
				PerforceResponse<PrintRecord<byte[]>> response = await perforce.TryPrintDataAsync($"{uri.AbsolutePath}@{change}", cancellationToken);
				response.EnsureSuccess();
				return response.Data.Contents!;
			}
		}
	}
}
