// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Commands.Bundles
{
	abstract class BundleCommandBase : Command
	{
		class FileBlobStore : StorageClientBase
		{
			readonly DirectoryReference _rootDir;
			readonly ILogger _logger;

			public FileBlobStore(DirectoryReference rootDir, IMemoryCache cache, ILogger logger)
				: base(cache, logger)
			{
				_rootDir = rootDir;
				_logger = logger;

				DirectoryReference.CreateDirectory(_rootDir);
			}

			FileReference GetRefFile(RefName name) => FileReference.Combine(_rootDir, name.ToString() + ".ref");
			FileReference GetBlobFile(BlobLocator id) => FileReference.Combine(_rootDir, id.Inner.ToString() + ".blob");

			#region Blobs

			public override Task<Stream> ReadBlobAsync(BlobLocator id, CancellationToken cancellationToken = default)
			{
				FileReference file = GetBlobFile(id);
				_logger.LogInformation("Reading {File}", file);
				return Task.FromResult<Stream>(FileReference.Open(file, FileMode.Open, FileAccess.Read, FileShare.Read));
			}

			public override async Task<Stream> ReadBlobRangeAsync(BlobLocator id, int offset, int length, CancellationToken cancellationToken = default)
			{
				Stream stream = await ReadBlobAsync(id, cancellationToken);
				stream.Seek(offset, SeekOrigin.Begin);
				return stream;
			}

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

			public override Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default)
			{
				throw new NotImplementedException();
			}

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

			public override async Task WriteRefTargetAsync(RefName name, NodeLocator target, CancellationToken cancellationToken = default)
			{
				FileReference file = GetRefFile(name);
				DirectoryReference.CreateDirectory(file.Directory);
				_logger.LogInformation("Writing {File}", file);
				await FileReference.WriteAllTextAsync(file, $"{target.Blob}#{target.ExportIdx}");
			}

			#endregion
		}

		public static RefName DefaultRefName { get; } = new RefName("default-ref");

		[CommandLine("-StorageDir=", Description = "Overrides the default storage server with a local directory")]
		public DirectoryReference StorageDir { get; set; } = DirectoryReference.Combine(Program.AppDir, "bundles");

		protected IStorageClient CreateStorageClient(IMemoryCache cache, ILogger logger)
		{
			return new FileBlobStore(StorageDir, cache, logger);
		}
	}

	[Command("bundle", "create", "Creates a bundle from a folder on the local hard drive")]
	class CreateCommand : BundleCommandBase
	{
		[CommandLine("-Ref=")]
		public RefName RefName { get; set; } = DefaultRefName;

		[CommandLine("-InputDir=", Required = true)]
		public DirectoryReference InputDir { get; set; } = null!;

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			using IMemoryCache cache = new MemoryCache(new MemoryCacheOptions());
			IStorageClient store = CreateStorageClient(cache, logger);

			TreeWriter writer = new TreeWriter(store, prefix: RefName.Text);

			DirectoryNode node = new DirectoryNode(DirectoryFlags.None);

			Stopwatch timer = Stopwatch.StartNew();

			ChunkingOptions options = new ChunkingOptions();
			await node.CopyFromDirectoryAsync(InputDir.ToDirectoryInfo(), options, writer, logger, CancellationToken.None);

			await writer.WriteRefAsync(RefName, node);

			logger.LogInformation("Time: {Time}", timer.Elapsed.TotalSeconds);
			return 0;
		}
	}
}
