using EpicGames.Core;
using HordeServer.Storage.Impl;
using HordeServer.Storage.Primitives;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;

namespace HordeServer.Storage.Services
{
	/// <summary>
	/// Very basic implementation of <see cref="IStorageService"/>. Backed by a filesystem, and uses timestamps for GC. Data may be evicted at any time.
	/// </summary>
	class SimpleStorageService : IStorageService
	{
		/// <summary>
		/// Path for storing blobs
		/// </summary>
		const string BlobsPath = "SimpleStorage/Blobs";

		/// <summary>
		/// Path for storing refs
		/// </summary>
		const string RefsPath = "SimpleStorage/Refs";

		/// <summary>
		/// The storage backend
		/// </summary>
		IStorageBackend Backend;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Backend">The storage backend</param>
		public SimpleStorageService(IStorageBackend Backend)
		{
			this.Backend = Backend;
		}

		/// <summary>
		/// Gets the for a file with the given hash
		/// </summary>
		/// <param name="BaseDir">Base directory for storage</param>
		/// <param name="Hash">Hash of the file</param>
		/// <returns>Path to the file for this blob</returns>
		static string GetItemPath(string BaseDir, BlobHash Hash)
		{
			return $"{BaseDir}/{StringUtils.FormatHexString(Hash.Span.Slice(0, 1))}/{StringUtils.FormatHexString(Hash.Span.Slice(1, 1))}/{Hash}.dat";
		}

		/// <inheritdoc/>
		public async Task<ReadOnlyMemory<byte>?> TryGetBlobAsync(BlobHash Hash, DateTime Deadline = default)
		{
			string Path = GetItemPath(BlobsPath, Hash);
			return await Backend.ReadAsync(Path);
		}

		/// <inheritdoc/>
		public async Task PutBlobAsync(BlobHash Hash, ReadOnlyMemory<byte> Value)
		{
			BlobHash RealHash = BlobHash.Compute(Value);
			if(Hash != RealHash)
			{
				throw new InvalidDataException($"Hash for blob does not match. Expected {RealHash}, got {Hash}");
			}

			string Path = GetItemPath(BlobsPath, Hash);
			if (!await Backend.TouchAsync(Path))
			{
				await Backend.WriteAsync(Path, Value);
			}
		}

		/// <inheritdoc/>
		public Task<bool> ShouldPutBlobAsync(BlobHash Hash)
		{
			// Always return true to force timestamps to be updated
			return Task.FromResult(true);
		}

		/// <inheritdoc/>
		public async Task SetRefAsync(BlobHash Key, BlobHash? Value)
		{
			string Path = GetItemPath(RefsPath, Key);
			if (Value == null)
			{
				await Backend.DeleteAsync(Path);
			}
			else
			{
				await Backend.WriteAsync(Path, Value.Value.Memory);
			}
		}

		/// <inheritdoc/>
		public async Task TouchRefAsync(BlobHash Key)
		{
			string Path = GetItemPath(RefsPath, Key);
			await Backend.TouchAsync(Path);
		}

		/// <inheritdoc/>
		public async Task<BlobHash?> GetRefAsync(BlobHash Key, TimeSpan MaxDrift = default)
		{
			ReadOnlyMemory<byte>? Memory = await Backend.ReadAsync(GetItemPath(RefsPath, Key));
			if (Memory == null)
			{
				return null;
			}
			else
			{
				return new BlobHash(Memory.Value);
			}
		}
	}
}
