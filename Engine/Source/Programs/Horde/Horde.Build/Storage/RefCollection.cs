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

			[BsonElement("l")]
			public BlobLocator Locator { get; set; }

			[BsonElement("e")]
			public int ExportIdx { get; set; }

			public RefDocument()
			{
				Name = RefName.Empty;
				Locator = BlobLocator.Empty;
			}

			public RefDocument(NamespaceId namespaceId, RefName name, RefTarget target)
			{
				NamespaceId = namespaceId;
				Name = name;
				Locator = target.Locator;
				ExportIdx = target.ExportIdx;
			}
		}

		class CachedRefValue
		{
			public RefName Name { get; }
			public RefTarget? Target { get; }
			public DateTime Time { get; }

			public CachedRefValue(RefName name, RefTarget? target)
			{
				Name = name;
				Target = target;
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

		CachedRefValue AddRefToCache(NamespaceId namespaceId, RefName name, RefTarget? target)
		{
			CachedRefValue cacheValue = new CachedRefValue(name, target);
			using (ICacheEntry newEntry = _cache.CreateEntry(new RefKey(namespaceId, name)))
			{
				newEntry.Value = cacheValue;
				newEntry.SetSize(name.Text.Length);
				newEntry.SetSlidingExpiration(TimeSpan.FromMinutes(5.0));
			}
			return cacheValue;
		}

		/// <inheritdoc/>
		public async Task DeleteRefAsync(NamespaceId namespaceId, RefName name, CancellationToken cancellationToken = default)
		{
			await _refs.DeleteOneAsync(x => x.NamespaceId == namespaceId && x.Name == name, cancellationToken);
			AddRefToCache(namespaceId, name, null);
		}

		/// <inheritdoc/>
		public async Task<RefTarget?> TryReadRefTargetAsync(NamespaceId namespaceId, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			CachedRefValue entry;
			if (!_cache.TryGetValue(name, out entry) || entry.Time < cacheTime)
			{
				RefDocument? refDocument = await _refs.Find(x => x.NamespaceId == namespaceId && x.Name == name).FirstOrDefaultAsync(cancellationToken);

				RefTarget? target;
				if (refDocument == null)
				{
					target = null;
				}
				else
				{
					target = new RefTarget(refDocument.Locator, refDocument.ExportIdx);
				}

				entry = AddRefToCache(namespaceId, name, target);
			}
			return entry.Target;
		}

		/// <inheritdoc/>
		public async Task WriteRefTargetAsync(NamespaceId namespaceId, RefName name, RefTarget target, CancellationToken cancellationToken = default)
		{
			RefDocument refDocument = new RefDocument(namespaceId, name, target);
			await _refs.ReplaceOneAsync(x => x.NamespaceId == namespaceId && x.Name == name, refDocument, new ReplaceOptions { IsUpsert = true }, cancellationToken);
			AddRefToCache(namespaceId, name, target);
		}
	}
}
