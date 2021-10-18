// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Services;
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
		public SimpleStorageService(IStorageBackend<ArtifactCollection> Backend)
		{
			this.Backend = Backend;
		}

		/// <summary>
		/// Gets the for a file with the given hash
		/// </summary>
		/// <param name="BaseDir">Base directory for storage</param>
		/// <param name="Hash">Hash of the file</param>
		/// <returns>Path to the file for this blob</returns>
		static string GetItemPath(string BaseDir, IoHash Hash)
		{
			return $"{BaseDir}/{StringUtils.FormatHexString(Hash.Span.Slice(0, 1))}/{StringUtils.FormatHexString(Hash.Span.Slice(1, 1))}/{Hash}.dat";
		}

		/// <inheritdoc/>
		public async Task<ReadOnlyMemory<byte>?> TryGetBlobAsync(IoHash Hash, DateTime Deadline = default)
		{
			string Path = GetItemPath(BlobsPath, Hash);
			return await Backend.ReadBytesAsync(Path);
		}

		/// <inheritdoc/>
		public async Task PutBlobAsync(IoHash Hash, ReadOnlyMemory<byte> Value)
		{
			IoHash RealHash = IoHash.Compute(Value.Span);
			if(Hash != RealHash)
			{
				throw new InvalidDataException($"Hash for blob does not match. Expected {RealHash}, got {Hash}");
			}

			string Path = GetItemPath(BlobsPath, Hash);
			if (!await Backend.ExistsAsync(Path))
			{
				await Backend.WriteBytesAsync(Path, Value);
			}
		}

		/// <inheritdoc/>
		public Task<bool> ShouldPutBlobAsync(IoHash Hash)
		{
			// Always return true to force timestamps to be updated
			return Task.FromResult(true);
		}

		/// <inheritdoc/>
		public async Task SetRefAsync(IoHash Key, IoHash? Value)
		{
			string Path = GetItemPath(RefsPath, Key);
			if (Value == null)
			{
				await Backend.DeleteAsync(Path);
			}
			else
			{
				await Backend.WriteBytesAsync(Path, Value.Value.Memory);
			}
		}

		/// <inheritdoc/>
		public Task TouchRefAsync(IoHash Key)
		{
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public async Task<IoHash?> GetRefAsync(IoHash Key, TimeSpan MaxDrift = default)
		{
			ReadOnlyMemory<byte>? Memory = await Backend.ReadBytesAsync(GetItemPath(RefsPath, Key));
			if (Memory == null)
			{
				return null;
			}
			else
			{
				return new IoHash(Memory.Value);
			}
		}
	}
}
