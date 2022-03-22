// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Build.Services;
using Horde.Build.Tools.Impl;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Driver;
using StackExchange.Redis;
using System.Threading.Tasks;

namespace Horde.Build.Tests
{
	[TestClass]
	public class VersionedCollectionTests : DatabaseIntegrationTest
	{
		abstract class DocumentBase : VersionedDocument<string, DocumentV2>
		{
			protected DocumentBase(string id)
				: base(id)
			{
			}
		}

		class DocumentV1 : DocumentBase
		{
			public int ValueV1 { get; set; }

			public DocumentV1(string id, int valueV1)
				: base(id)
			{
				ValueV1 = valueV1;
			}

			public override DocumentV2 UpgradeToLatest() => new DocumentV2(Id, ValueV1.ToString());
		}

		class DocumentV2 : DocumentBase
		{
			public string ValueV2 { get; set; }

			public DocumentV2(string id, string valueV2)
				: base(id)
			{
				ValueV2 = valueV2;
			}

			public override DocumentV2 UpgradeToLatest() => this;
		}


		DatabaseService _databaseService;
		IDatabase _redis;
		RedisKey _baseKey = new RedisKey("versioned/");
		IMongoCollection<VersionedDocument<string, DocumentV2>> _baseCollection;
		VersionedCollection<string, DocumentV2> _collection;

		public VersionedCollectionTests()
		{
			_databaseService = GetDatabaseServiceSingleton();
			_redis = GetRedisDatabase();
			_baseCollection = _databaseService.GetCollection<VersionedDocument<string, DocumentV2>>("versioned");
			_collection = new VersionedCollection<string, DocumentV2>(_baseCollection, _redis, _baseKey);
		}

		[TestMethod]
		public async Task Add()
		{
			DocumentV2? doc = await _collection.GetAsync("hello");
			Assert.IsNull(doc);

			Assert.IsTrue(await _collection.AddAsync(new DocumentV2("hello", "1234")));

			doc = await _collection.GetAsync("hello");
			Assert.IsNotNull(doc);
			Assert.AreEqual(doc!.ValueV2, "1234");
		}

		[TestMethod]
		public async Task AutoUpgrade()
		{
			DocumentV2? doc = await _collection.GetAsync("hello");
			Assert.IsNull(doc);

			await _baseCollection.InsertOneAsync(new DocumentV1("hello", 987));

			doc = await _collection.GetAsync("hello");
			Assert.IsNotNull(doc);
			Assert.AreEqual(doc!.ValueV2, "987");
		}

		[TestMethod]
		public async Task Delete()
		{
			DocumentV2? doc = await _collection.GetAsync("hello");
			Assert.IsNull(doc);

			Assert.IsTrue(await _collection.AddAsync(new DocumentV2("hello", "1234")));

			doc = await _collection.GetAsync("hello");
			Assert.IsNotNull(doc);
			Assert.AreEqual(doc!.ValueV2, "1234");

			Assert.IsTrue(await _collection.DeleteAsync("hello"));

			doc = await _collection.GetAsync("hello");
			Assert.IsNull(doc);
		}

		[TestMethod]
		public async Task FindOrAdd()
		{
			await _baseCollection.InsertOneAsync(new DocumentV1("hello", 555));

			DocumentV2? doc = await _collection.FindOrAddAsync("hello", () => new DocumentV2("hello", "1234"));
			Assert.IsNotNull(doc);
			Assert.AreEqual(doc.ValueV2, "555");

			doc = await _collection.FindOrAddAsync("world", () => new DocumentV2("world", "1234"));
			Assert.IsNotNull(doc);
			Assert.AreEqual(doc.ValueV2, "1234");

			doc = await _collection.FindOrAddAsync("world", () => new DocumentV2("world", "5678"));
			Assert.IsNotNull(doc);
			Assert.AreEqual(doc.ValueV2, "1234");
		}

		[TestMethod]
		public async Task Update()
		{
			await _baseCollection.InsertOneAsync(new DocumentV1("hello", 555));

			DocumentV2? doc = await _collection.GetAsync("hello");
			Assert.IsNotNull(doc);
			Assert.AreEqual(doc!.ValueV2, "555");

			DocumentV2? updatedDoc = await _collection.UpdateAsync(doc!, Builders<DocumentV2>.Update.Set(x => x.ValueV2, "999"));
			Assert.IsNotNull(updatedDoc);
			Assert.AreEqual(updatedDoc!.ValueV2, "999");

			Assert.AreEqual(doc!.ValueV2, "555");

			DocumentV2? doc2 = await _collection.GetAsync("hello");
			Assert.IsNotNull(doc2);
			Assert.AreEqual(doc2!.ValueV2, "999");

			DocumentV2? updatedDoc2 = await _collection.UpdateAsync(doc!, Builders<DocumentV2>.Update.Set(x => x.ValueV2, "1000"));
			Assert.IsNull(updatedDoc2);

			doc2 = await _collection.GetAsync("hello");
			Assert.IsNotNull(doc2);
			Assert.AreEqual(doc2!.ValueV2, "999");

			updatedDoc2 = await _collection.UpdateAsync(updatedDoc, Builders<DocumentV2>.Update.Set(x => x.ValueV2, "1000"));
			Assert.IsNotNull(updatedDoc2);
			Assert.AreEqual(updatedDoc2!.ValueV2, "1000");
		}

		[TestMethod]
		public async Task Replace()
		{
			await _baseCollection.InsertOneAsync(new DocumentV1("hello", 555));

			DocumentV2? doc = await _collection.GetAsync("hello");
			Assert.IsNotNull(doc);
			Assert.AreEqual(doc!.ValueV2, "555");

			Assert.IsTrue(await _collection.ReplaceAsync(doc!, new DocumentV2("hello", "world")));

			DocumentV2? newDoc = await _collection.GetAsync("hello");
			Assert.IsNotNull(newDoc);
			Assert.AreEqual(newDoc!.ValueV2, "world");

			Assert.IsFalse(await _collection.ReplaceAsync(doc!, new DocumentV2("hello", "world2")));

			newDoc = await _collection.GetAsync("hello");
			Assert.IsNotNull(newDoc);
			Assert.AreEqual(newDoc!.ValueV2, "world");
		}
	}
}
