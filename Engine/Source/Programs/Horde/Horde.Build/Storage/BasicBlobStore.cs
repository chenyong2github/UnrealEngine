// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Horde.Build.Server;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;

namespace Horde.Build.Storage
{
	/// <summary>
	/// Implementation of <see cref="IBlobStore"/> using Mongo for storing refs, and an <see cref="IStorageBackend"/> implementation for bulk data.
	/// </summary>
	public class BasicBlobStore : IBlobStore
	{
		class RefDocument
		{
			[BsonId]
			public RefName Name { get; set; }
			public BlobLocator Value { get; set; }

			public RefDocument()
			{
				Name = RefName.Empty;
				Value = BlobLocator.Empty;
			}

			public RefDocument(RefName name, BlobLocator value)
			{
				Name = name;
				Value = value;
			}
		}

		class CachedRefValue
		{
			public RefName Name { get; }
			public BlobLocator Value { get; }
			public DateTime Time { get; }

			public CachedRefValue(RefName name, BlobLocator value)
			{
				Name = name;
				Value = value;
				Time = DateTime.UtcNow;
			}
		}

		readonly HostId _serverId = HostId.Empty;

		readonly IMongoCollection<RefDocument> _refs;
		readonly IStorageBackend _backend;
		readonly IMemoryCache _cache;

		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="mongoService">The mongo service implementation</param>
		/// <param name="backend">Backend to use for storing data</param>
		/// <param name="cache">Cache for ref values</param>
		/// <param name="logger">Logger for this instance</param>
		public BasicBlobStore(MongoService mongoService, IStorageBackend backend, IMemoryCache cache, ILogger<BasicBlobStore> logger)
		{
			_refs = mongoService.GetCollection<RefDocument>("Refs");
			_backend = backend;
			_cache = cache;
			_logger = logger;
 		}

		#region Blobs

		static string GetBlobPath(BlobId id) => $"{id}.blob";

		/// <inheritdoc/>
		public async Task<Bundle> ReadBundleAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			string path = GetBlobPath(locator.BlobId);

			ReadOnlyMemory<byte>? data = await _backend.ReadBytesAsync(path, cancellationToken);
			if (data == null)
			{
				throw new KeyNotFoundException($"Unable to read data from {path}");
			}

			return new Bundle(new MemoryReader(data.Value));
		}

		/// <inheritdoc/>
		public async Task<BlobLocator> WriteBundleAsync(Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			BlobId id = BlobId.CreateNew(prefix);
			string path = GetBlobPath(id);

			ReadOnlySequence<byte> sequence = bundle.AsSequence();
			using (ReadOnlySequenceStream stream = new ReadOnlySequenceStream(sequence))
			{
				await _backend.WriteAsync(path, stream, cancellationToken);
			}

			return new BlobLocator(_serverId, id);
		}

		#endregion

		#region Refs

		CachedRefValue AddRefToCache(RefName name, BlobLocator value)
		{
			CachedRefValue cacheValue = new CachedRefValue(name, value);
			using (ICacheEntry newEntry = _cache.CreateEntry(name))
			{
				newEntry.Value = cacheValue;
				newEntry.SetSize(name.Text.Length + value.Inner.Length);
				newEntry.SetSlidingExpiration(TimeSpan.FromMinutes(5.0));
			}
			return cacheValue;
		}

		/// <inheritdoc/>
		public async Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default)
		{
			await _refs.DeleteOneAsync(x => x.Name == name, cancellationToken);
			AddRefToCache(name, BlobLocator.Empty);
		}

		/// <inheritdoc/>
		public async Task<BundleRef?> TryReadRefAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = await TryReadRefTargetAsync(name, cacheTime, cancellationToken);
			if (locator.IsValid())
			{
				try
				{
					Bundle? bundle = await ReadBundleAsync(locator, cancellationToken);
					return new BundleRef(locator, bundle);
				}
				catch (Exception ex)
				{
					_logger.LogWarning(ex, "Unable to read blob {BlobId} referenced by ref {RefName}", locator, name);
					return null;
				}
			}
			return null;
		}

		/// <inheritdoc/>
		public async Task<BlobLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			CachedRefValue entry;
			if (!_cache.TryGetValue(name, out entry) || entry.Time < cacheTime)
			{
				RefDocument? refDocument = await _refs.Find(x => x.Name == name).FirstOrDefaultAsync(cancellationToken);
				BlobLocator value = (refDocument == null) ? BlobLocator.Empty : refDocument.Value;
				entry = AddRefToCache(name, value);
			}
			return entry.Value;
		}

		/// <inheritdoc/>
		public async Task<BlobLocator> WriteRefAsync(RefName name, Bundle bundle, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = await WriteBundleAsync(bundle, name.Text, cancellationToken);
			await WriteRefTargetAsync(name, locator, cancellationToken);
			return locator;
		}

		/// <inheritdoc/>
		public async Task WriteRefTargetAsync(RefName name, BlobLocator locator, CancellationToken cancellationToken = default)
		{
			RefDocument refDocument = new RefDocument(name, locator);
			await _refs.ReplaceOneAsync(x => x.Name == name, refDocument, new ReplaceOptions { IsUpsert = true }, cancellationToken);
			AddRefToCache(name, locator);
		}

		#endregion
	}
}
