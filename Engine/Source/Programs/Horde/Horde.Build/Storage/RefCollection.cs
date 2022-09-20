// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using Horde.Build.Server;
using Microsoft.Extensions.Caching.Memory;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;

namespace Horde.Build.Storage
{
	/// <summary>
	/// Collection of ref name to blob mappings
	/// </summary>
	public class RefCollection : IRefCollection
	{
		class RefKey
		{
			public NamespaceId NamespaceId { get; set; }
			public RefName Name { get; set; }

			public RefKey(NamespaceId namespaceId, RefName name)
			{
				NamespaceId = namespaceId;
				Name = name;
			}

			/// <inheritdoc/>
			public override bool Equals(object? obj) => obj is RefKey refKey && NamespaceId == refKey.NamespaceId && Name == refKey.Name;

			/// <inheritdoc/>
			public override int GetHashCode() => HashCode.Combine(NamespaceId, Name);
		}

		class RefDocument
		{
			public ObjectId Id { get; set; }

			[BsonElement("ns")]
			public NamespaceId NamespaceId { get; set; }

			[BsonElement("k")]
			public RefName Name { get; set; }

			[BsonElement("v")]
			public BlobLocator Value { get; set; }

			public RefDocument()
			{
				Name = RefName.Empty;
				Value = BlobLocator.Empty;
			}

			public RefDocument(NamespaceId namespaceId, RefName name, BlobLocator value)
			{
				NamespaceId = namespaceId;
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

		readonly IMongoCollection<RefDocument> _refs;
		readonly IMemoryCache _cache;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="mongoService">The mongo service implementation</param>
		/// <param name="cache">Cache for ref values</param>
		public RefCollection(MongoService mongoService, IMemoryCache cache)
		{
			_refs = mongoService.GetCollection<RefDocument>("Refs", keys => keys.Ascending(x => x.NamespaceId).Ascending(x => x.Name), unique: true);
			_cache = cache;
		}

		CachedRefValue AddRefToCache(NamespaceId namespaceId, RefName name, BlobLocator value)
		{
			CachedRefValue cacheValue = new CachedRefValue(name, value);
			using (ICacheEntry newEntry = _cache.CreateEntry(new RefKey(namespaceId, name)))
			{
				newEntry.Value = cacheValue;
				newEntry.SetSize(name.Text.Length + value.Inner.Length);
				newEntry.SetSlidingExpiration(TimeSpan.FromMinutes(5.0));
			}
			return cacheValue;
		}

		/// <inheritdoc/>
		public async Task DeleteRefAsync(NamespaceId namespaceId, RefName name, CancellationToken cancellationToken = default)
		{
			await _refs.DeleteOneAsync(x => x.NamespaceId == namespaceId && x.Name == name, cancellationToken);
			AddRefToCache(namespaceId, name, BlobLocator.Empty);
		}

		/// <inheritdoc/>
		public async Task<BlobLocator> TryReadRefTargetAsync(NamespaceId namespaceId, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			CachedRefValue entry;
			if (!_cache.TryGetValue(name, out entry) || entry.Time < cacheTime)
			{
				RefDocument? refDocument = await _refs.Find(x => x.NamespaceId == namespaceId && x.Name == name).FirstOrDefaultAsync(cancellationToken);
				BlobLocator value = (refDocument == null) ? BlobLocator.Empty : refDocument.Value;
				entry = AddRefToCache(namespaceId, name, value);
			}
			return entry.Value;
		}

		/// <inheritdoc/>
		public async Task WriteRefTargetAsync(NamespaceId namespaceId, RefName name, BlobLocator locator, CancellationToken cancellationToken = default)
		{
			RefDocument refDocument = new RefDocument(namespaceId, name, locator);
			await _refs.ReplaceOneAsync(x => x.NamespaceId == namespaceId && x.Name == name, refDocument, new ReplaceOptions { IsUpsert = true }, cancellationToken);
			AddRefToCache(namespaceId, name, locator);
		}
	}
}
