// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Storage.Backends
{
	/// <summary>
	/// Implementation of <see cref="IStorageClient"/> which writes data to files on disk.
	/// </summary>
	public class FileStorageClient : StorageClientBase
	{
		readonly DirectoryReference _rootDir;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="rootDir">Root directory for storing blobs</param>
		/// <param name="cache">Cache for blob data</param>
		/// <param name="logger">Logger interface</param>
		public FileStorageClient(DirectoryReference rootDir, IMemoryCache cache, ILogger logger)
			: base(cache, logger)
		{
			_rootDir = rootDir;
			_logger = logger;

			DirectoryReference.CreateDirectory(_rootDir);
		}

		FileReference GetRefFile(RefName name) => FileReference.Combine(_rootDir, name.ToString() + ".ref");
		FileReference GetBlobFile(BlobLocator id) => FileReference.Combine(_rootDir, id.Inner.ToString() + ".blob");

		#region Blobs

		/// <inheritdoc/>
		public override Task<Stream> ReadBlobAsync(BlobLocator id, CancellationToken cancellationToken = default)
		{
			FileReference file = GetBlobFile(id);
			_logger.LogInformation("Reading {File}", file);
			return Task.FromResult<Stream>(FileReference.Open(file, FileMode.Open, FileAccess.Read, FileShare.Read));
		}

		/// <inheritdoc/>
		public override async Task<Stream> ReadBlobRangeAsync(BlobLocator id, int offset, int length, CancellationToken cancellationToken = default)
		{
			Stream stream = await ReadBlobAsync(id, cancellationToken);
			stream.Seek(offset, SeekOrigin.Begin);
			return stream;
		}

		/// <inheritdoc/>
		public override async Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			BlobLocator id = BlobLocator.Create(HostId.Empty, prefix);
			FileReference file = GetBlobFile(id);
			DirectoryReference.CreateDirectory(file.Directory);
			_logger.LogInformation("Writing {File}", file);

			using (FileStream fileStream = FileReference.Open(file, FileMode.Create, FileAccess.Write, FileShare.ReadWrite))
			{
				await stream.CopyToAsync(fileStream, cancellationToken);
			}

			return id;
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public override Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default)
		{
			FileReference file = GetRefFile(name);
			FileReference.Delete(file);
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public override async Task<NodeLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			FileReference file = GetRefFile(name);
			if (!FileReference.Exists(file))
			{
				return default;
			}

			_logger.LogInformation("Reading {File}", file);
			string text = await FileReference.ReadAllTextAsync(file, cancellationToken);

			int hashIdx = text.IndexOf('#', StringComparison.Ordinal);
			BlobLocator locator = new BlobLocator(text.Substring(0, hashIdx));
			int exportIdx = Int32.Parse(text.Substring(hashIdx + 1), CultureInfo.InvariantCulture);

			return new NodeLocator(locator, exportIdx);
		}

		/// <inheritdoc/>
		public override async Task WriteRefTargetAsync(RefName name, NodeLocator target, CancellationToken cancellationToken = default)
		{
			FileReference file = GetRefFile(name);
			DirectoryReference.CreateDirectory(file.Directory);
			_logger.LogInformation("Writing {File}", file);
			await FileReference.WriteAllTextAsync(file, $"{target.Blob}#{target.ExportIdx}");
		}

		#endregion
	}
}
