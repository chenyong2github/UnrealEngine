// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage.Impl
{
	/// <summary>
	/// Implementation of <see cref="IStorageClient"/> which writes directly to the local filesystem for testing. Not intended for production use.
	/// </summary>
	public class FileStorageClient : IStorageClient
	{
		class Ref : IRef
		{
			public NamespaceId NamespaceId { get; set; }
			public BucketId BucketId { get; set; }
			public RefId RefId { get; set; }
			public CbObject Value { get; set; }

			public Ref(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CbObject Value)
			{
				this.NamespaceId = NamespaceId;
				this.BucketId = BucketId;
				this.RefId = RefId;
				this.Value = Value;
			}
		}

		/// <summary>
		/// Base directory for storing files
		/// </summary>
		public DirectoryReference BaseDir { get; }
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="BaseDir"></param>
		/// <param name="Logger"></param>
		public FileStorageClient(DirectoryReference BaseDir, ILogger Logger)
		{
			this.BaseDir = BaseDir;
			this.Logger = Logger;

			DirectoryReference.CreateDirectory(BaseDir);
		}

		FileReference GetBlobFile(NamespaceId NamespaceId, IoHash Hash)
		{
			return FileReference.Combine(BaseDir, NamespaceId.ToString(), $"{Hash}.blob");
		}

		/// <inheritdoc/>
		public Task<Stream> ReadBlobAsync(NamespaceId NamespaceId, IoHash Hash, CancellationToken CancellationToken = default)
		{
			FileReference File = GetBlobFile(NamespaceId, Hash);
			Logger.LogInformation("Reading {File} ({Size} bytes)", File, new FileInfo(File.FullName).Length);
			return Task.FromResult<Stream>(FileReference.Open(File, FileMode.Open, FileAccess.Read, FileShare.Read));
		}

		/// <inheritdoc/>
		public async Task WriteBlobAsync(NamespaceId NamespaceId, IoHash Hash, Stream Stream, CancellationToken CancellationToken = default)
		{
			FileReference File = GetBlobFile(NamespaceId, Hash);
			DirectoryReference.CreateDirectory(File.Directory);

			Logger.LogInformation("Writing {File} ({Size} bytes)", File, Stream.Length);
			using (Stream OutputStream = FileReference.Open(File, FileMode.Create, FileAccess.Write, FileShare.Read))
			{
				await Stream.CopyToAsync(OutputStream);
			}
		}

		/// <inheritdoc/>
		public Task<bool> DeleteRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken = default)
		{
			FileReference File = GetRefFile(NamespaceId, BucketId, RefId);
			if (FileReference.Exists(File))
			{
				FileReference.Delete(File);
				return Task.FromResult(true);
			}
			return Task.FromResult(false);
		}

		/// <inheritdoc/>
		public Task<List<RefId>> FindMissingRefsAsync(NamespaceId NamespaceId, BucketId BucketId, List<RefId> RefIds, CancellationToken CancellationToken = default)
		{
			return Task.FromResult(RefIds.Where(x => !FileReference.Exists(GetRefFile(NamespaceId, BucketId, x))).ToList());
		}

		FileReference GetRefFile(NamespaceId NamespaceId, BucketId BucketId, RefId RefId)
		{
			return FileReference.Combine(BaseDir, NamespaceId.ToString(), BucketId.ToString(), $"{RefId}.ref");
		}

		/// <inheritdoc/>
		public async Task<IRef> GetRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken = default)
		{
			FileReference File = GetRefFile(NamespaceId, BucketId, RefId);
			Logger.LogInformation("Reading {File} ({Size} bytes)", File, new FileInfo(File.FullName).Length);
			byte[] Data = await FileReference.ReadAllBytesAsync(File);
			return new Ref(NamespaceId, BucketId, RefId, new CbObject(Data));
		}

		/// <inheritdoc/>
		public Task<bool> HasRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken = default)
		{
			return Task.FromResult(FileReference.Exists(GetRefFile(NamespaceId, BucketId, RefId)));
		}

		/// <inheritdoc/>
		public Task<List<IoHash>> TryFinalizeRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, IoHash Hash, CancellationToken CancellationToken = default)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public async Task<List<IoHash>> TrySetRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CbObject Value, CancellationToken CancellationToken = default)
		{
			FileReference File = GetRefFile(NamespaceId, BucketId, RefId);
			DirectoryReference.CreateDirectory(File.Directory);

			Logger.LogInformation("Writing {File} ({Size} bytes)", File, Value.GetView().Length);
			await FileReference.WriteAllBytesAsync(File, Value.GetView().ToArray(), CancellationToken);
			return new List<IoHash>();
		}
	}
}
