// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using Horde.Build.Server;
using HordeCommon;
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

			[BsonElement("xa"), BsonIgnoreIfDefault]
			public DateTime? ExpiresAtUtc { get; set; }

			[BsonElement("xt"), BsonIgnoreIfDefault]
			public TimeSpan? Lifetime { get; set; }

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

			public bool HasExpired(DateTime utcNow) => ExpiresAtUtc.HasValue && utcNow >= ExpiresAtUtc.Value;

			public bool RequiresTouch(DateTime utcNow) => ExpiresAtUtc.HasValue && Lifetime.HasValue && utcNow >= ExpiresAtUtc.Value - new TimeSpan(Lifetime.Value.Ticks / 4);
		}

		class CachedRefValue
		{
			public RefDocument? Value { get; }
			public DateTime Time { get; }

			public CachedRefValue(RefDocument? value)
			{
				Value = value;
				Time = DateTime.UtcNow;
			}
		}

		readonly IMongoCollection<RefDocument> _refs;
		readonly IClock _clock;
		readonly IMemoryCache _cache;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="mongoService">The mongo service implementation</param>
		/// <param name="clock">Clock for expiring refs</param>
		/// <param name="cache">Cache for ref values</param>
		/// <param name="logger">Logger for output</param>
		public RefCollection(MongoService mongoService, IClock clock, IMemoryCache cache, ILogger<RefCollection> logger)
		{
			_refs = mongoService.GetCollection<RefDocument>("Storage.Refs", keys => keys.Ascending(x => x.NamespaceId).Ascending(x => x.Name), unique: true);
			_clock = clock;
			_cache = cache;
			_logger = logger;
		}

		CachedRefValue AddRefToCache(NamespaceId namespaceId, RefName name, RefDocument? value)
		{
			CachedRefValue cacheValue = new CachedRefValue(value);
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

		/// <summary>
		/// Deletes a ref document that has reached its expiry time
		/// </summary>
		async Task DeleteExpiredRefAsync(RefDocument refDocument, CancellationToken cancellationToken = default)
		{
			await _refs.DeleteOneAsync(x => x.Id == refDocument.Id && x.ExpiresAtUtc == refDocument.ExpiresAtUtc, cancellationToken: cancellationToken);
			AddRefToCache(refDocument.NamespaceId, refDocument.Name, default);
		}

		/// <inheritdoc/>
		public async Task<NodeLocator> TryReadRefTargetAsync(NamespaceId namespaceId, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			CachedRefValue entry;
			if (!_cache.TryGetValue(name, out entry) || entry.Time < cacheTime)
			{
				RefDocument? refDocument = await _refs.Find(x => x.NamespaceId == namespaceId && x.Name == name).FirstOrDefaultAsync(cancellationToken);
				entry = AddRefToCache(namespaceId, name, refDocument);
			}

			if (entry.Value == null)
			{
				return default;
			}

			if (entry.Value.ExpiresAtUtc != null)
			{
				DateTime utcNow = _clock.UtcNow;
				if (entry.Value.HasExpired(utcNow))
				{
					await DeleteExpiredRefAsync(entry.Value, cancellationToken);
					return default;
				}
				if (entry.Value.RequiresTouch(utcNow))
				{
					await _refs.UpdateOneAsync(x => x.Id == entry.Value.Id, Builders<RefDocument>.Update.Set(x => x.ExpiresAtUtc, utcNow + entry.Value.Lifetime!.Value), cancellationToken: cancellationToken);
				}
			}

			return new NodeLocator(entry.Value.Blob, entry.Value.ExportIdx);
		}

		/// <inheritdoc/>
		public async Task WriteRefTargetAsync(NamespaceId namespaceId, RefName name, NodeLocator target, RefOptions? options = null, CancellationToken cancellationToken = default)
		{
			RefDocument refDocument = new RefDocument(namespaceId, name, target);

			if (options != null && options.Lifetime.HasValue)
			{
				refDocument.ExpiresAtUtc = _clock.UtcNow + options.Lifetime.Value;
				if (options.Extend ?? true)
				{
					refDocument.Lifetime = options.Lifetime;
				}
			}

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
			
			AddRefToCache(namespaceId, name, refDocument);
		}

		/// <inheritdoc/>
		public async IAsyncEnumerable<NodeLocator> EnumerateRefs(NamespaceId namespaceId, [EnumeratorCancellation] CancellationToken cancellationToken = default)
		{
			DateTime utcNow = _clock.UtcNow;
			using (IAsyncCursor<RefDocument> cursor = await _refs.Find(x => x.NamespaceId == namespaceId).ToCursorAsync(cancellationToken))
			{
				while (await cursor.MoveNextAsync(cancellationToken))
				{
					foreach (RefDocument refDoc in cursor.Current)
					{
						if (refDoc.HasExpired(utcNow))
						{
							await DeleteExpiredRefAsync(refDoc, cancellationToken);
						}
						else
						{
							yield return new NodeLocator(refDoc.Blob, refDoc.ExportIdx);
						}
					}
				}
			}
		}
	}
}
