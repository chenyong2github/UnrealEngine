// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace HordeServerTests
{
	[TestClass]
	public class LruCacheTests : IDisposable
	{
		readonly FileReference _indexFile;
		readonly FileReference _dataFile;
		LruCache _cache;

		public LruCacheTests()
		{
			_indexFile = new FileReference("test.idx");
			_dataFile = new FileReference("test.dat");

			_cache = LruCache.CreateNew(_indexFile, _dataFile, 2048, 1024 * 1024);
		}

		public void Dispose()
		{
			_cache.Dispose();
		}

		[TestMethod]
		public void EmptyObject()
		{
			IoHash hash = _cache.Add(Array.Empty<byte>());
			Assert.AreEqual(1, _cache.NumItems);

			using (LruCache.View view = _cache.LockView())
			{
				byte[] data = view.Get(hash).ToArray();
				Assert.AreEqual(0, data.Length);
			}
		}

		[TestMethod]
		public void DuplicateObjects()
		{
			_cache.Add(new byte[] { 1, 2, 3 });
			_cache.Add(new byte[] { 1, 2, 3 });
			_cache.Add(new byte[] { 1, 2, 4 });
			Assert.AreEqual(2, _cache.NumItems);
			Assert.AreEqual(6, _cache.NumBytes);
			Assert.AreEqual(128, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, _cache.NumBytesWithPageSlack);
		}

		static byte[] MakeBlockWithIndex(int size, int index)
		{
			byte[] data = new byte[size];
			BinaryPrimitives.WriteInt32LittleEndian(data, index);
			return data;
		}

		[TestMethod]
		public void LargeObjectPacking()
		{
			_cache.Add(MakeBlockWithIndex(1000, 1));
			Assert.AreEqual(1, _cache.NumItems);
			Assert.AreEqual(1000, _cache.NumBytes);
			Assert.AreEqual(1024, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, _cache.NumBytesWithPageSlack);

			_cache.Add(MakeBlockWithIndex(1000, 2));
			Assert.AreEqual(2, _cache.NumItems);
			Assert.AreEqual(2000, _cache.NumBytes);
			Assert.AreEqual(2048, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, _cache.NumBytesWithPageSlack);

			_cache.Add(MakeBlockWithIndex(1000, 3));
			Assert.AreEqual(3, _cache.NumItems);
			Assert.AreEqual(3000, _cache.NumBytes);
			Assert.AreEqual(3072, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, _cache.NumBytesWithPageSlack);

			_cache.Add(MakeBlockWithIndex(1000, 4));
			Assert.AreEqual(4, _cache.NumItems);
			Assert.AreEqual(4000, _cache.NumBytes);
			Assert.AreEqual(4096, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, _cache.NumBytesWithPageSlack);

			_cache.Add(MakeBlockWithIndex(1000, 5));
			Assert.AreEqual(5, _cache.NumItems);
			Assert.AreEqual(5000, _cache.NumBytes);
			Assert.AreEqual(5120, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(8192, _cache.NumBytesWithPageSlack);
		}

		[TestMethod]
		public void SmallObjectPacking()
		{
			_cache.Add(MakeBlockWithIndex(64, 1));
			Assert.AreEqual(1, _cache.NumItems);
			Assert.AreEqual(64, _cache.NumBytes);
			Assert.AreEqual(64, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, _cache.NumBytesWithPageSlack);

			_cache.Add(MakeBlockWithIndex(64, 2));
			Assert.AreEqual(2, _cache.NumItems);
			Assert.AreEqual(128, _cache.NumBytes);
			Assert.AreEqual(128, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, _cache.NumBytesWithPageSlack);

			_cache.Add(MakeBlockWithIndex(64, 3));
			Assert.AreEqual(3, _cache.NumItems);
			Assert.AreEqual(192, _cache.NumBytes);
			Assert.AreEqual(192, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, _cache.NumBytesWithPageSlack);
		}

		[TestMethod]
		public void FillItems()
		{
			for (int idx = 0; idx < 2048; idx++)
			{
				byte[] data = MakeBlockWithIndex(64, idx);
				_cache.Add(data);
			}
			Assert.AreEqual(2048, _cache.NumItems);

			_cache.Add(new byte[] { 1, 2, 3 });
			Assert.AreEqual(2048, _cache.NumItems);
		}

		[TestMethod]
		public void FillCapacity()
		{
			for (int idx = 0; idx < 1024; idx++)
			{
				byte[] data = MakeBlockWithIndex(1024, idx);
				_cache.Add(data);
			}
			Assert.AreEqual(1024, _cache.NumItems);

			_cache.Add(new byte[] { 1, 2, 3 });
			Assert.AreEqual(1024, _cache.NumItems);
		}

		[TestMethod]
		public void HugeObjects()
		{
			byte[] data = new byte[4096 + 4096 + 2000];

			Random random = new Random(0);
			random.NextBytes(data);

			IoHash hash = _cache.Add(data);
			Assert.AreEqual(1, _cache.NumItems);
			Assert.AreEqual(data.Length, _cache.NumBytes);
			Assert.AreEqual(4096 + 4096 + 2048, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096 + 4096 + 4096, _cache.NumBytesWithPageSlack);

			using LruCache.View view = _cache.LockView();
			ReadOnlyMemory<byte> lookupData = view.Get(hash);
			Assert.IsTrue(data.AsSpan().SequenceEqual(lookupData.Span));
		}

		[TestMethod]
		public void HugeObjects2()
		{
			const int NumItems = 5;
			for (int idx = 0; idx < NumItems; idx++)
			{
				byte[] data = MakeBlockWithIndex(2000 + (idx * 2048), idx);
				_cache.Add(data);
			}

			Assert.AreEqual(NumItems, _cache.NumItems);

			using (LruCache.View view = _cache.LockView())
			{
				for (int idx = 0; idx < NumItems; idx++)
				{
					byte[] data = MakeBlockWithIndex(2000 + (idx * 2048), idx);
					IoHash hash = IoHash.Compute(data);
					ReadOnlyMemory<byte> otherData = view.Get(hash);
					Assert.IsTrue(data.AsSpan().SequenceEqual(otherData.Span));
				}
			}
		}

		[TestMethod]
		public async Task SmallObjectSerialization()
		{
			for (int idx = 0; idx < 1000; idx++)
			{
				byte[] data = MakeBlockWithIndex(4 + idx, idx);
				_cache.Add(data);
			}

			CheckSmallObjects();

			Assert.AreEqual(1000, _cache.NumItems);
			long numBytes = _cache.NumBytes;
			long numBytesWithBlockSlack = _cache.NumBytesWithBlockSlack;
			long numBytesWithPageSlack = _cache.NumBytesWithPageSlack;

			await _cache.SaveAsync();
			_cache.Dispose();

			_cache = await LruCache.OpenAsync(_indexFile, _dataFile);

			Assert.AreEqual(1000, _cache.NumItems);
			Assert.AreEqual(numBytes, _cache.NumBytes);
			Assert.AreEqual(numBytesWithBlockSlack, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(numBytesWithPageSlack, _cache.NumBytesWithPageSlack);

			CheckSmallObjects();
		}

		void CheckSmallObjects()
		{
			using (LruCache.View view = _cache.LockView())
			{
				for (int idx = 0; idx < 1000; idx++)
				{
					byte[] data = MakeBlockWithIndex(4 + idx, idx);
					IoHash hash = IoHash.Compute(data);
					ReadOnlyMemory<byte> otherData = view.Get(hash);
					Assert.IsTrue(data.AsSpan().SequenceEqual(otherData.Span));
				}
			}
		}

		[TestMethod]
		public async Task LargeObjectSerialization()
		{
			const int NumItems = 10;
			for (int idx = 0; idx < NumItems; idx++)
			{
				byte[] data = MakeBlockWithIndex(2000 + (idx * 2048), idx);
				_cache.Add(data);
			}

			CheckLargeObjects(NumItems);

			Assert.AreEqual(NumItems, _cache.NumItems);
			long numBytes = _cache.NumBytes;
			long numBytesWithBlockSlack = _cache.NumBytesWithBlockSlack;
			long numBytesWithPageSlack = _cache.NumBytesWithPageSlack;

			await _cache.SaveAsync();
			_cache.Dispose();

			_cache = await LruCache.OpenAsync(_indexFile, _dataFile);

			Assert.AreEqual(NumItems, _cache.NumItems);
			Assert.AreEqual(numBytes, _cache.NumBytes);
			Assert.AreEqual(numBytesWithBlockSlack, _cache.NumBytesWithBlockSlack);
			Assert.AreEqual(numBytesWithPageSlack, _cache.NumBytesWithPageSlack);

			CheckLargeObjects(NumItems);
		}

		void CheckLargeObjects(int numItems)
		{
			using (LruCache.View view = _cache.LockView())
			{
				for (int idx = 0; idx < numItems; idx++)
				{
					byte[] data = MakeBlockWithIndex(2000 + (idx * 2048), idx);
					IoHash hash = IoHash.Compute(data);
					ReadOnlyMemory<byte> otherData = view.Get(hash);
					Assert.IsTrue(data.AsSpan().SequenceEqual(otherData.Span));
				}
			}
		}

		[TestMethod]
		public async Task ExpireItems()
		{
			for (int idx = 0; idx < 1000; idx++)
			{
				byte[] data = MakeBlockWithIndex(4 + idx, idx);
				_cache.Add(data);
			}

			long trimSize = _cache.NumBytesWithBlockSlack;
			_cache.NextGeneration();

			List<IoHash> testHashes = new List<IoHash>();
			for (int idx = 1000; idx < 2000; idx++)
			{
				byte[] data = MakeBlockWithIndex(4 + idx, idx);
				testHashes.Add(_cache.Add(data));
			}

			long desiredSize = _cache.NumBytesWithBlockSlack - trimSize;
			await _cache.TrimAsync(desiredSize);

			Assert.AreEqual(desiredSize, _cache.NumBytesWithBlockSlack);

			using(LruCache.View view = _cache.LockView())
			{
				for (int idx = 0; idx < 100; idx++)
				{
					ReadOnlyMemory<byte> memory = view.Get(testHashes[idx]);
					Assert.IsFalse(memory.IsEmpty);
				}
			}
		}
	}
}
