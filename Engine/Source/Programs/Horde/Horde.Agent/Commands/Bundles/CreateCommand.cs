// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Commands.Bundles
{
	abstract class BundleCommandBase : Command
	{
		class FileBlobStore : IBlobStore
		{
			readonly DirectoryReference _rootDir;
			readonly ILogger _logger;

			public FileBlobStore(DirectoryReference rootDir, ILogger logger)
			{
				_rootDir = rootDir;
				_logger = logger;

				DirectoryReference.CreateDirectory(_rootDir);
			}

			FileReference GetRefFile(RefName name) => FileReference.Combine(_rootDir, name.ToString() + ".ref");
			FileReference GetBlobFile(BlobLocator id) => FileReference.Combine(_rootDir, id.Inner.ToString() + ".blob");

			static async Task WriteAsync(FileReference file, ReadOnlySequence<byte> sequence, CancellationToken cancellationToken)
			{
				using (FileStream stream = FileReference.Open(file, FileMode.Create, FileAccess.Write, FileShare.ReadWrite))
				{
					foreach (ReadOnlyMemory<byte> memory in sequence)
					{
						await stream.WriteAsync(memory, cancellationToken);
					}
				}
			}

			#region Blobs

			public async Task<Bundle> ReadBundleAsync(BlobLocator id, CancellationToken cancellationToken = default)
			{
				FileReference file = GetBlobFile(id);
				_logger.LogInformation("Reading {File}", file);
				byte[] bytes = await FileReference.ReadAllBytesAsync(file, cancellationToken);
				return new Bundle(new MemoryReader(bytes));
			}

			public async Task<BlobLocator> WriteBundleAsync(Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default)
			{
				BlobLocator id = BlobLocator.Create(HostId.Empty, prefix);
				FileReference file = GetBlobFile(id);
				DirectoryReference.CreateDirectory(file.Directory);
				_logger.LogInformation("Writing {File}", file);
				await WriteAsync(file, bundle.AsSequence(), cancellationToken);
				return id;
			}

			#endregion

			#region Refs

			public Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default)
			{
				throw new NotImplementedException();
			}

			public async Task<BundleRef?> TryReadRefAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
			{
				BlobLocator locator = await TryReadRefTargetAsync(name, cacheTime, cancellationToken);
				if (!locator.IsValid())
				{
					return null;
				}

				Bundle bundle = await ReadBundleAsync(locator, cancellationToken);
				return new BundleRef(locator, bundle);
			}

			public async Task<BlobLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
			{
				FileReference file = GetRefFile(name);
				if(!FileReference.Exists(file))
				{
					return BlobLocator.Empty;
				}

				_logger.LogInformation("Reading {File}", file);
				string text = await FileReference.ReadAllTextAsync(file, cancellationToken);
				return new BlobLocator(text.Trim());
			}

			public async Task<BlobLocator> WriteRefAsync(RefName name, Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default)
			{
				BlobLocator locator = await WriteBundleAsync(bundle, name.Text, cancellationToken);
				await WriteRefTargetAsync(name, locator, cancellationToken);
				return locator;
			}

			public async Task WriteRefTargetAsync(RefName name, BlobLocator id, CancellationToken cancellationToken = default)
			{
				FileReference file = GetRefFile(name);
				DirectoryReference.CreateDirectory(file.Directory);
				_logger.LogInformation("Writing {File}", file);
				await FileReference.WriteAllTextAsync(file, id.ToString());
			}

			#endregion
		}

		public static RefName DefaultRefName { get; } = new RefName("default-ref");

		[CommandLine("-StorageDir=", Description = "Overrides the default storage server with a local directory")]
		public DirectoryReference StorageDir { get; set; } = DirectoryReference.Combine(Program.AppDir, "bundles");

		IBlobStore? _blobStore;

		protected IBlobStore CreateBlobStore(ILogger logger)
		{
			_blobStore ??= new FileBlobStore(StorageDir, logger);
			return _blobStore;
		}

		protected ITreeStore CreateTreeStore(ILogger logger)
		{
			IBlobStore blobStore = CreateBlobStore(logger);
			return new TreeStore(blobStore, new TreeOptions());
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
			using ITreeStore store = CreateTreeStore(logger);
			ITreeWriter writer = store.CreateTreeWriter(RefName.Text);

			DirectoryNode node = new DirectoryNode(DirectoryFlags.None);
			await node.CopyFromDirectoryAsync(InputDir.ToDirectoryInfo(), new ChunkingOptions(), writer, logger, CancellationToken.None);

			await writer.WriteRefAsync(RefName, node);
			return 0;
		}
	}
}
