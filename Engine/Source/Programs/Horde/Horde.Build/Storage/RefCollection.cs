// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using Horde.Build.Server;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;
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
			[BsonIgnoreIfDefault]
			public ObjectId Id { get; set; }

			[BsonElement("ns")]
			public NamespaceId NamespaceId { get; set; }

			[BsonElement("k")]
			public RefName Name { get; set; }

			[BsonElement("b")]
			public BlobLocator Blob { get; set; }

			[BsonElement("e")]
			public int ExportIdx { get; set; }

			public RefDocument()
			{
				Name = RefName.Empty;
				Blob = BlobLocator.Empty;
			}

			public RefDocument(NamespaceId namespaceId, RefName name, NodeLocator target)
			{
				NamespaceId = namespaceId;
				Name = name;
				Blob = target.Blob;
				ExportIdx = target.ExportIdx;
			}
		}

		class CachedRefValue
		{
			public RefName Name { get; }
			public NodeLocator Target { get; }
			public DateTime Time { get; }

			public CachedRefValue(RefName name, NodeLocator target)
			{
				Name = name;
				Target = target;
				Time = DateTime.UtcNow;
			}
		}

		readonly IMongoCollection<RefDocument> _refs;
		readonly IMemoryCache _cache;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="mongoService">The mongo service implementation</param>
		/// <param name="cache">Cache for ref values</param>
		/// <param name="logger">Logger for output</param>
		public RefCollection(MongoService mongoService, IMemoryCache cache, ILogger<RefCollection> logger)
		{
			_refs = mongoService.GetCollection<RefDocument>("Refs", keys => keys.Ascending(x => x.NamespaceId).Ascending(x => x.Name), unique: true);
			_cache = cache;
			_logger = logger;
		}

		CachedRefValue AddRefToCache(NamespaceId namespaceId, RefName name, NodeLocator target)
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
			AddRefToCache(namespaceId, name, default);
		}

		/// <inheritdoc/>
		public async Task<NodeLocator> TryReadRefTargetAsync(NamespaceId namespaceId, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			CachedRefValue entry;
			if (!_cache.TryGetValue(name, out entry) || entry.Time < cacheTime)
			{
				RefDocument? refDocument = await _refs.Find(x => x.NamespaceId == namespaceId && x.Name == name).FirstOrDefaultAsync(cancellationToken);

				NodeLocator target;
				if (refDocument == null)
				{
					target = default;
				}
				else
				{
					target = new NodeLocator(refDocument.Blob, refDocument.ExportIdx);
				}

				entry = AddRefToCache(namespaceId, name, target);
			}
			return entry.Target;
		}

		/// <inheritdoc/>
		public async Task WriteRefTargetAsync(NamespaceId namespaceId, RefName name, NodeLocator target, CancellationToken cancellationToken = default)
		{
			RefDocument refDocument = new RefDocument(namespaceId, name, target);
			
			ReplaceOneResult result = await _refs.ReplaceOneAsync(x => x.NamespaceId == namespaceId && x.Name == name, refDocument, new ReplaceOptions { IsUpsert = true }, cancellationToken);
			if (result.ModifiedCount > 0)
			{
				_logger.LogInformation("Updated ref {NamespaceId}:{RefName} to {Target}", namespaceId, name, target);
			}
			else if (result.MatchedCount > 0)
			{
				_logger.LogInformation("Updated ref {NamespaceId}:{RefName} to {Target} (no-op)", namespaceId, name, target);
			}
			else if (result.UpsertedId.IsObjectId)
			{
				_logger.LogInformation("Inserted ref {NamespaceId}:{RefName} to {Target} (id = {NewRefId})", namespaceId, name, target, (ObjectId)result.UpsertedId);
			}
			else
			{
				_logger.LogError("Unable to update ref {NamespaceId}:{RefName} (modified: {Modified}, matched: {Matched}, upsert: {Upsert})", namespaceId, name, result.ModifiedCount, result.MatchedCount, result.UpsertedId);
			}
			
			AddRefToCache(namespaceId, name, target);
		}
	}
}
