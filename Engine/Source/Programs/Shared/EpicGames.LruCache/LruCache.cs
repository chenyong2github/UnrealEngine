// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

[assembly: InternalsVisibleTo("Horde.Build.Tests")]

namespace HordeCommon
{
	/// <summary>
	/// A thread-safe LRU cache of hashed objects, backed by a file on disk. Reads are typically lock free. Writes require a lock but complete in constant time.
	/// The cache is transactional, and will not lose data if the process is terminated. Entries are kept in the cache until explicitly trimmed by calling
	/// <see cref="LruCache.TrimAsync(Int64)"/>.
	/// </summary>
	public class LruCache : IDisposable
	{
		const int MinBlockSizeLog2 = 6; // 64 bytes
		const int MaxBlockSize = 1 << MaxBlockSizeLog2;
		const int MaxBlockSizeLog2 = 12; // 4096 bytes
		const int PageSize = 1 << PageSizeLog2;
		const int PageSizeLog2 = MaxBlockSizeLog2;

		public const int MaxObjectSize = MaxBlockSize;

		const int CurrentVersion = 0;

		/// <summary>
		/// A page of data allocated to store objects
		/// </summary>
		class Page
		{
			public Memory<byte> Data { get; }

			public byte _blockSizeLog2;
			public int _pageCount; // For a large item, this is the total number of full pages in the list. The full item size is LargeItemPageCount * 4kb + Item.LastBlockLength
			public ulong _freeBitMap;
			public int _nextPageIdx;

			public int BlockSize => 1 << _blockSizeLog2;
			public ulong EmptyBitMap => ~0UL >> (64 - (1 << (PageSizeLog2 - _blockSizeLog2))); // Note: left-shift only uses bottom 5 bits, cannot use (1UL << (PageSize >> BlockSizeLog2)) -1
				
			public Page(Memory<byte> data)
			{
				Data = data;
			}

			public void Reset(int blockSizeLog2, int pageCount, int nextPage)
			{
				_blockSizeLog2 = (byte)blockSizeLog2;
				_pageCount = pageCount;
				_freeBitMap = EmptyBitMap;
				_nextPageIdx = nextPage;
			}
		}

		/// <summary>
		/// Information about an allocated item
		/// </summary>
		readonly struct Item
		{
			const long LargeItemFlag = 1L << 63;
			const long ValidFlag = 1L << 62;

			const int FirstPageOffset = 0;
			const int FirstPageLength = 32;
			const int GenerationOffset = FirstPageOffset + FirstPageLength;
			const int GenerationLength = 8;
			const int TailIndexOffset = GenerationOffset + GenerationLength;
			const int TailIndexLength = 6;
			const int TailSizeOffset = TailIndexOffset + TailIndexLength;
			const int TailSizeLength = 13;

			public readonly long Data;

			public int FirstPageIdx => (int)(Data >> FirstPageOffset);
			public byte Generation => (byte)((Data >> GenerationOffset) & ((1 << GenerationLength) - 1));
			public int TailIndex => (int)(Data >> TailIndexOffset) & ((1 << TailIndexLength) - 1);
			public int TailSize => (int)(Data >> TailSizeOffset) & ((1 << TailSizeLength) - 1);
			public bool IsLargeItem => (Data & LargeItemFlag) != 0;
			public bool IsValid => (Data & ValidFlag) != 0;

			public Item(long data)
			{
				Data = data;
			}

			public Item(int firstPage, byte generation, int tailIndex, int tailSize, bool largeItem)
			{
				Data = ((long)firstPage << FirstPageOffset) | ((long)generation << GenerationOffset) | ((long)tailIndex << TailIndexOffset) | ((long)tailSize << TailSizeOffset) | ValidFlag;
				if (largeItem)
				{
					Data |= LargeItemFlag;
				}
			}
		}

		/// <summary>
		/// A scoped view of an LRU cache. Prevents data from being moved while a client is still using it.
		/// </summary>
		public class View : IDisposable
		{
			internal readonly LruCache Outer;

			internal View(LruCache outer)
			{
				Outer = outer;
				lock (outer._lockObject)
				{
					outer._numReaders++;
				}
			}

			public ReadOnlyMemory<byte> Get(IoHash hash) => Outer.Get(hash);

			public void Dispose()
			{
				lock (Outer._lockObject)
				{
					Outer._numReaders--;
				}
			}
		}

		/// <summary>
		/// Open-addressed hashtable used for looking up items by hash. Uses Robin-Hood hashing for performance.
		/// </summary>
		class HashTable
		{
			const ulong Multiplier = 11400714819323198485ul; // fibonnaci hashing

			private readonly byte[] _hashes;
			private readonly long[] _items;
			public int NumItems { get; private set; }
			public int MaxItems => 1 << _maxItemsLog2;
			private readonly int _maxItemsLog2;

			public HashTable(int maxItems)
			{
				_maxItemsLog2 = BitOperations.Log2((uint)maxItems);
				maxItems = 1 << _maxItemsLog2;

				_hashes = new byte[maxItems * IoHash.NumBytes];
				_items = new long[maxItems];
			}

			Memory<byte> GetHashMemory(int slot) => _hashes.AsMemory(slot * IoHash.NumBytes, IoHash.NumBytes);

			int GetTargetSlot(IoHash hash) => (int)(((ulong)hash.GetHashCode() * Multiplier) >> (64 - _maxItemsLog2));

			public bool Add(IoHash hash, Item item)
			{
				if (NumItems < MaxItems)
				{
					NumItems++;
					int targetSlot = GetTargetSlot(hash);
					Add(hash, item.Data, targetSlot, targetSlot);
					return true;
				}
				return false;
			}

			private void Add(IoHash hash, long itemData, int slot, int targetSlot)
			{
				for(; ;)
				{
					// Check if this slot is empty
					IoHash compareHash = GetHash(slot);
					if(compareHash == IoHash.Zero)
					{
						break;
					}

					// If not, get the probe sequence length of the item currently in this slot
					int compareTargetSlot = GetTargetSlot(compareHash);

					// If this is a better fit, replace it. Update subsequent slots first to ensure consistent read ordering.
					int nextSlot = (slot + 1) & (MaxItems - 1);
					if (Displace(slot, targetSlot, compareTargetSlot))
					{
						Add(compareHash, _items[slot], nextSlot, compareTargetSlot);
						break;
					}
					slot = nextSlot;
				}

				hash.CopyTo(GetHashMemory(slot).Span);
				_items[slot] = itemData;
			}

			public bool Displace(int slot, int targetSlot, int compareTargetSlot)
			{
				int probeLen = (targetSlot + MaxItems - slot) & (MaxItems - 1);
				int compareProbeLen = (compareTargetSlot + MaxItems - slot) & (MaxItems - 1);
				return probeLen > compareProbeLen;
			}

			public IoHash GetHash(int slot) => new IoHash(GetHashMemory(slot).Span);
			public Item GetItem(int slot) => new Item(_items[slot]);

			public int Find(IoHash hash)
			{
				int targetSlot = GetTargetSlot(hash);
				for (int slot = targetSlot;;slot = (slot + 1) & (MaxItems - 1))
				{
					IoHash compareHash = GetHash(slot);
					if (compareHash == IoHash.Zero)
					{
						break;
					}
					if (compareHash == hash)
					{
						return slot;
					}

					int compareTargetSlot = GetTargetSlot(compareHash);
					if (Displace(slot, targetSlot, compareTargetSlot))
					{
						break;
					}
				}
				return -1;
			}

			public bool TryUpdate(int slot, Item oldItem, Item newItem)
			{
				return Interlocked.CompareExchange(ref _items[slot], newItem.Data, oldItem.Data) == oldItem.Data;
			}
		}

		readonly object _lockObject = new object();
		int _numReaders = 0;

		readonly FileReference _indexFile;

		readonly MemoryMappedFile _dataFile;
		readonly MemoryMappedViewAccessor _dataFileViewAccessor;
		readonly MemoryMappedFileView _dataFileView;

		const int InvalidIdx = -1;

		int _numFreePages;
		int _freePageIdx;
		readonly int[] _pageIdxWithFreeBlock = new int[MaxBlockSizeLog2 + 1];

		readonly Page[] _pages;
		HashTable _lookup;

		int _generation;
		readonly long[] _generationSize;

		readonly long _maxBytesPerGeneration;

		public int NumItems => _lookup.NumItems;
		public int MaxItems => _lookup.MaxItems;

		public long NumBytes { get; private set; }
		public long NumBytesWithBlockSlack { get; private set; }
		public long NumBytesWithPageSlack => (_pages.Length - _numFreePages) << PageSizeLog2;

		public long MaxSize { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="indexFileRef">Path to the index file</param>
		/// <param name="dataFileRef">Path to the cache file</param>
		/// <param name="maxItems">Maximum number of items that can be stored in the cache</param>
		/// <param name="maxSize">Size of the cache file. This will be preallocated.</param>
		private LruCache(FileReference indexFileRef, FileReference dataFileRef, int maxItems, long maxSize)
		{
			maxSize &= ~(PageSize - 1);

			_indexFile = indexFileRef;
			_generation = 0;
			_generationSize = new long[256];
			_lookup = new HashTable((int)(maxItems * 1.4));
			MaxSize = maxSize;

			_dataFile = MemoryMappedFile.CreateFromFile(dataFileRef.FullName, FileMode.OpenOrCreate, null, maxSize);
			_dataFileViewAccessor = _dataFile.CreateViewAccessor(0, maxSize);
			_dataFileView = new MemoryMappedFileView(_dataFileViewAccessor);

			_maxBytesPerGeneration = maxSize / 256;

			int numPages = (int)(maxSize >> PageSizeLog2);
			_pages = new Page[numPages];

			const int MaxChunkSize = Int32.MaxValue & ~(PageSize - 1);
			const int MaxPagesPerChunk = MaxChunkSize >> PageSizeLog2;

			long chunkOffset = 0;
			for (int baseIdx = 0; baseIdx < numPages; )
			{
				int numPagesInBlock = Math.Min(numPages - baseIdx, MaxPagesPerChunk);
				int chunkSize = numPagesInBlock << PageSizeLog2;

				Memory<byte> chunkData = _dataFileView.GetMemory(chunkOffset, chunkSize);
				for (int idx = 0; idx < numPagesInBlock; idx++)
				{
					Memory<byte> data = chunkData.Slice(idx << MaxBlockSizeLog2, MaxBlockSize);
					_pages[baseIdx + idx] = new Page(data);
				}

				baseIdx += numPagesInBlock;
				chunkOffset += chunkSize;
			}

			_freePageIdx = 0;
			for (int idx = 1; idx < _pages.Length; idx++)
			{
				_pages[idx - 1]._nextPageIdx = idx;
			}

			for (int idx = 0; idx < _pageIdxWithFreeBlock.Length; idx++)
			{
				_pageIdxWithFreeBlock[idx] = InvalidIdx;
			}

			_numFreePages = _pages.Length;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_dataFileView.Dispose();
			_dataFileViewAccessor.Dispose();
			_dataFile.Dispose();
		}

		/// <summary>
		/// Creates a new cache
		/// </summary>
		/// <param name="indexFile"></param>
		/// <param name="dataFile"></param>
		/// <param name="maxItems"></param>
		/// <param name="maxSize"></param>
		/// <returns></returns>
		public static LruCache CreateNew(FileReference indexFile, FileReference dataFile, int maxItems, long maxSize)
		{
			DirectoryReference.CreateDirectory(indexFile.Directory);
			DirectoryReference.CreateDirectory(dataFile.Directory);

			FileReference.Delete(indexFile);
			FileReference.Delete(GetTransactionFile(indexFile));
			FileReference.Delete(dataFile);

			return new LruCache(indexFile, dataFile, maxItems, maxSize);
		}

		/// <summary>
		/// Opens an existing cache and modify its settings
		/// </summary>
		/// <param name="indexFile"></param>
		/// <param name="dataFile"></param>
		/// <param name="maxItems"></param>
		/// <param name="maxSize"></param>
		/// <returns></returns>
		public static async Task<LruCache?> OpenAndModifyAsync(FileReference indexFile, FileReference dataFile, int maxItems, long maxSize)
		{
			LruCache? cache = await OpenAsync(indexFile, dataFile);
			if (cache != null)
			{
				if (cache.MaxItems != maxItems || cache.MaxSize != maxSize)
				{
					FileReference newIndexFile = new FileReference(indexFile.FullName + ".new");
					FileReference newDataFile = new FileReference(dataFile.FullName + ".new");

					using (LruCache newCache = LruCache.CreateNew(newIndexFile, newDataFile, maxItems, maxSize))
					{
						for (int idx = 0; idx < cache._lookup.MaxItems; idx++)
						{
							Item item = newCache._lookup.GetItem(idx);
							if (item.IsValid)
							{
								IoHash hash = cache._lookup.GetHash(idx);
								newCache.Add(hash, newCache.Get(hash));
							}
						}
					}

					FileReference oldIndexFile = new FileReference(indexFile.FullName + ".old");
					FileReference oldDataFile = new FileReference(dataFile.FullName + ".old");

					FileReference.Move(indexFile, oldIndexFile, true);
					FileReference.Move(dataFile, oldDataFile, true);

					FileReference.Move(newIndexFile, indexFile, true);
					FileReference.Move(newDataFile, dataFile, true);

					FileReference.Delete(oldIndexFile);
					FileReference.Delete(oldDataFile);

					cache.Dispose();
					cache = await OpenAsync(indexFile, dataFile);
				}
			}
			return cache;
		}

		/// <summary>
		/// Loads the current cache index from disk
		/// </summary>
		public static async Task<LruCache> OpenAsync(FileReference indexFile, FileReference dataFile)
		{
			LruCache? cache = await TryOpenAsync(indexFile, dataFile);
			if (cache == null)
			{
				throw new FileNotFoundException($"Unable to open {indexFile}");
			}
			return cache;
		}

		/// <summary>
		/// Loads the current cache index from disk
		/// </summary>
		public static async Task<LruCache?> TryOpenAsync(FileReference indexFile, FileReference dataFile)
		{
			// Check if the index exists. If not, check if a save of it was interrupted.
			if (!FileReference.Exists(indexFile))
			{
				FileReference transactionIndexFile = new FileReference(indexFile.FullName + ".tr");
				if (!FileReference.Exists(transactionIndexFile))
				{
					return null;
				}
				FileReference.Move(transactionIndexFile, indexFile, true);
			}

			byte[] data = await FileReference.ReadAllBytesAsync(indexFile);
			MemoryReader reader = new MemoryReader(data);

			byte version = reader.ReadUInt8();
			if (version > CurrentVersion)
			{
				throw new InvalidDataException($"Unable to load lru cache index of version {version}");
			}

			int maxItems = reader.ReadInt32();
			long maxSize = reader.ReadInt64();

			LruCache cache = new LruCache(indexFile, dataFile, maxItems, maxSize);
			cache._generation = reader.ReadUInt8();

			foreach (Page page in cache._pages)
			{
				page._pageCount = InvalidIdx;
			}

			int itemCount = reader.ReadInt32();
			for (int idx = 0; idx < itemCount; idx++)
			{
				IoHash hash = reader.ReadIoHash();
				Item item = new Item(reader.ReadInt64());

				int pageIdx = item.FirstPageIdx;

				Page page = cache._pages[pageIdx];
				if (item.IsLargeItem)
				{
					int pageCount = 0;

					Page firstPage = page;
					for (; ; )
					{
						int nextPageIdx = reader.ReadInt32();
						page.Reset(MaxBlockSizeLog2, 0, nextPageIdx & 0x7fffffff);
						page._freeBitMap = 0;
						page = cache._pages[page._nextPageIdx];

						pageCount++;

						if (nextPageIdx >= 0)
						{
							break;
						}
					}

					cache.NumBytes += pageCount << PageSizeLog2;
					cache.NumBytesWithBlockSlack += pageCount << PageSizeLog2;

					page = firstPage;
					for (; pageCount > 0; pageCount--)
					{
						page._pageCount = pageCount;
						page = cache._pages[page._nextPageIdx];
					}
				}

				if (page._pageCount == InvalidIdx)
				{
					int blockSizeLog2 = GetBlockSizeLog2(item.TailSize);
					page.Reset(blockSizeLog2, 0, InvalidIdx);
				}

				page._freeBitMap ^= 1UL << item.TailIndex;

				cache.NumBytes += item.TailSize;
				cache.NumBytesWithBlockSlack += page.BlockSize;

				cache._lookup.Add(hash, item);
			}

			// Fix up the lists of free and partially-free pages
			cache._numFreePages = 0;
			cache._freePageIdx = InvalidIdx;
			for (int pageIdx = cache._pages.Length - 1; pageIdx >= 0; pageIdx--)
			{
				Page page = cache._pages[pageIdx];
				if (page._pageCount == InvalidIdx)
				{
					page.Reset(0, 0, cache._freePageIdx);
					page._nextPageIdx = cache._freePageIdx;
					cache._freePageIdx = pageIdx;
					cache._numFreePages++;
				}
				else if (page._freeBitMap != 0)
				{
					page._nextPageIdx = cache._pageIdxWithFreeBlock[page._blockSizeLog2];
					cache._freePageIdx = pageIdx;
				}
			}
			return cache;
		}

		/// <summary>
		/// Saves the cache index
		/// </summary>
		public async Task SaveAsync()
		{
			byte[] indexData;
			lock (_lockObject)
			{
				int indexSize = sizeof(byte) + sizeof(int) + sizeof(long) + sizeof(byte) + sizeof(int);
				for (int slot = 0; slot < _lookup.MaxItems; slot++)
				{
					Item item = _lookup.GetItem(slot);
					if (item.IsValid)
					{
						indexSize += IoHash.NumBytes + sizeof(long);
						if (item.IsLargeItem)
						{
							indexSize += _pages[item.FirstPageIdx]._pageCount * sizeof(int);
						}
					}
				}

				indexData = new byte[indexSize];

				MemoryWriter writer = new MemoryWriter(indexData);
				writer.WriteUInt8(CurrentVersion);
				writer.WriteInt32(MaxItems);
				writer.WriteInt64(MaxSize);

				writer.WriteUInt8((byte)_generation);

				writer.WriteInt32(_lookup.NumItems);
				for (int slot = 0; slot < _lookup.MaxItems; slot++)
				{
					Item item = _lookup.GetItem(slot);
					if (item.IsValid)
					{
						writer.WriteIoHash(_lookup.GetHash(slot));
						writer.WriteInt64(item.Data);

						if (item.IsLargeItem)
						{
							Page page = _pages[item.FirstPageIdx];
							while (page._pageCount > 1)
							{
								writer.WriteUInt32((uint)page._nextPageIdx | 0x80000000U);
								page = _pages[page._nextPageIdx];
							}
							writer.WriteInt32(page._nextPageIdx);
						}
					}
				}
				writer.CheckOffset(indexSize);
			}

			// Write the new file to disk
			FileReference transactionFile = GetTransactionFile(_indexFile);
			await FileReference.WriteAllBytesAsync(transactionFile, indexData);
			FileReference.Move(transactionFile, _indexFile, true);
		}

		/// <summary>
		/// Update the generation counter. Useful for testing, where we can specify an explicit point to garbage collect to.
		/// </summary>
		public void NextGeneration()
		{
			lock (_lockObject)
			{
				_generation = (_generation + 1) & 0xff;
			}
		}

		/// <summary>
		/// Adds an item to the cache
		/// </summary>
		/// <param name="data"></param>
		/// <returns></returns>
		public IoHash Add(ReadOnlyMemory<byte> data)
		{
			IoHash hash = IoHash.Compute(data.Span);
			Add(hash, data);
			return hash;
		}

		static int GetBlockSizeLog2(int itemSize)
		{
			int itemSizeLog2 = Math.Max(BitOperations.Log2((uint)itemSize), MinBlockSizeLog2);
			if (itemSize > (1 << itemSizeLog2))
			{
				itemSizeLog2++;
			}
			return itemSizeLog2;
		}

		/// <summary>
		/// Adds an item into the cache
		/// </summary>
		/// <param name="hash">Has of the item</param>
		/// <param name="data">The data to add</param>
		public void Add(IoHash hash, ReadOnlyMemory<byte> data)
		{
			lock (_lockObject)
			{
				// Make sure we can add the hash
				if (_lookup.NumItems >= _lookup.MaxItems)
				{
					return;
				}
				if (_lookup.Find(hash) != -1)
				{
					return;
				}

				// Make sure there are enough full pages to store the data. The last page is always treated as a 'tail' block, even if it's exactly 4kb long.
				int numFullPages = Math.Max(data.Length - 1, 0) >> PageSizeLog2;

				// Get the size of the 'tail' page
				int tailSize = data.Length - (numFullPages << PageSizeLog2);
				int tailSizeLog2 = GetBlockSizeLog2(tailSize);

				// Get the tail page. This may be null if there are no pages free of the given size.
				int tailPageIdx = _pageIdxWithFreeBlock[tailSizeLog2];

				// Make sure there are enough pages available
				int numRequiredPages = numFullPages;
				if (tailPageIdx == InvalidIdx)
				{
					numRequiredPages++;
				}
				if (_numFreePages < numRequiredPages)
				{
					return;
				}

				// Allocate the tail page if necessary
				if (tailPageIdx == -1)
				{
					tailPageIdx = _freePageIdx;
					_freePageIdx = _pages[tailPageIdx]._nextPageIdx;

					_pageIdxWithFreeBlock[tailSizeLog2] = tailPageIdx;
					_pages[tailPageIdx].Reset(tailSizeLog2, 0, -1);
				}

				// Take an item from the first free span
				Page tailPage = _pages[tailPageIdx];
				int tailIndex = BitOperations.TrailingZeroCount(tailPage._freeBitMap) & 63;
				tailPage._freeBitMap ^= 1UL << tailIndex;

				// If the page is full, remove it from the free list
				if (tailPage._freeBitMap == 0UL)
				{
					_pageIdxWithFreeBlock[tailSizeLog2] = tailPage._nextPageIdx;
				}

				// Copy the tail data to the cache
				int tailOffset = tailIndex << tailSizeLog2;
				data.Slice(data.Length - tailSize).CopyTo(tailPage.Data.Slice(tailOffset));

				// Add all the other full pages in reverse order
				int firstPageIdx = tailPageIdx;
				for (int idx = numFullPages - 1; idx >= 0; idx--)
				{
					int pageIdx = _freePageIdx;

					Page page = _pages[pageIdx];
					_freePageIdx = page._nextPageIdx;

					page.Reset(PageSizeLog2, numFullPages - idx, firstPageIdx);
					page._freeBitMap = 0;

					data.Slice(idx << PageSizeLog2, PageSize).CopyTo(page.Data);
					firstPageIdx = pageIdx;
				}

				// Create the new item
				Item item = new Item(firstPageIdx, (byte)_generation, tailIndex, tailSize, numFullPages > 0);
				_lookup.Add(hash, item);

				// Update the allocation stats
				NumBytes += data.Length;
				NumBytesWithBlockSlack += (numFullPages << PageSizeLog2) + (1 << tailSizeLog2);
				_numFreePages -= numRequiredPages;

				// Update the generation
				long newGenerationSize = Interlocked.Add(ref _generationSize[_generation], (numFullPages << PageSizeLog2) + (1 << tailSizeLog2));
				if (newGenerationSize > _maxBytesPerGeneration)
				{
					_generation++;
				}
			}
		}

		/// <summary>
		/// Inserts a page into a linked list, sorted by index
		/// </summary>
		/// <param name="firstPageIdx"></param>
		/// <param name="pageIdx"></param>
		void AddPageToList(ref int firstPageIdx, int pageIdx)
		{
			Page page = _pages[pageIdx];
			if (firstPageIdx == InvalidIdx || pageIdx < firstPageIdx)
			{
				page._nextPageIdx = firstPageIdx;
				firstPageIdx = pageIdx;
			}
			else
			{
				Page prevPage = _pages[firstPageIdx];
				while (prevPage._nextPageIdx != InvalidIdx && pageIdx < prevPage._nextPageIdx)
				{
					prevPage = _pages[prevPage._nextPageIdx];
				}

				page._nextPageIdx = prevPage._nextPageIdx;
				prevPage._nextPageIdx = pageIdx;
			}
		}

		/// <summary>
		/// Removes a page from a linked list
		/// </summary>
		/// <param name="FirstPage"></param>
		/// <param name="Page"></param>
		void RemovePageFromList(ref int firstPageIdx, int pageIdx)
		{
			Page page = _pages[pageIdx];
			if (firstPageIdx == pageIdx)
			{
				firstPageIdx = page._nextPageIdx;
			}
			else if(firstPageIdx != InvalidIdx)
			{
				Page prevPage = _pages[firstPageIdx];
				for (; ;)
				{
					int nextPageIdx = prevPage._nextPageIdx;
					if (nextPageIdx == InvalidIdx)
					{
						break;
					}
					if (nextPageIdx == pageIdx)
					{
						prevPage._nextPageIdx = page._nextPageIdx;
						break;
					}
					prevPage = _pages[nextPageIdx];
				}
			}
		}

		/// <summary>
		/// Creates a view of the cache which can be used for queries
		/// </summary>
		/// <returns>New view object</returns>
		public View LockView()
		{
			return new View(this);
		}

		/// <summary>
		/// Tries to read an item from the cache. Invoked via <see cref="View.Get(IoHash)"/>.
		/// </summary>
		/// <param name="hash"></param>
		/// <returns></returns>
		private ReadOnlyMemory<byte> Get(IoHash hash)
		{
			int slot = _lookup.Find(hash);
			if (slot == -1)
			{
				return ReadOnlyMemory<byte>.Empty;
			}

			Item item = _lookup.GetItem(slot);
			Page firstPage = _pages[item.FirstPageIdx];

			if (item.IsLargeItem)
			{
				int itemSize = (firstPage._pageCount << PageSizeLog2) + item.TailSize;

				byte[] buffer = new byte[itemSize];

				int offset = 0;

				Page nextPage = firstPage;
				for (int idx = 0; idx < firstPage._pageCount; idx++)
				{
					nextPage.Data.CopyTo(buffer.AsMemory(offset));
					nextPage = _pages[nextPage._nextPageIdx];
					offset += PageSize;
				}
				nextPage!.Data.Slice(item.TailIndex << nextPage._blockSizeLog2, item.TailSize).CopyTo(buffer.AsMemory(offset));

				UpdateGeneration(slot, item, itemSize);
				return buffer;
			}
			else
			{
				UpdateGeneration(slot, item, item.TailSize);
				return firstPage.Data.Slice(item.TailIndex << firstPage._blockSizeLog2, item.TailSize);
			}
		}

		void UpdateGeneration(int slot, Item item, int itemSize)
		{
			for (; ; )
			{
				int generationCopy = _generation;
				if (item.Generation == generationCopy)
				{
					break;
				}

				Item newItem = new Item(item.FirstPageIdx, (byte)generationCopy, item.TailIndex, item.TailSize, item.IsLargeItem);
				if (_lookup.TryUpdate(slot, item, newItem))
				{
					Interlocked.Add(ref _generationSize[item.Generation], -itemSize);

					long newGenerationSize = Interlocked.Add(ref _generationSize[item.Generation], itemSize);
					if (newGenerationSize > _maxBytesPerGeneration)
					{
						Interlocked.CompareExchange(ref _generation, (byte)(generationCopy + 1), generationCopy);
					}

					break;
				}

				item = _lookup.GetItem(slot);
			}
		}

		/// <summary>
		/// Trims the cache to the given size, allowing new objects to be allocated. 
		/// </summary>
		/// <param name="targetSize"></param>
		/// <returns></returns>
		public async ValueTask<bool> TrimAsync(long targetSize)
		{
			List<Item> freeItems = new List<Item>();
			lock (_lockObject)
			{
				// Check that nobody is currently reading from the cache
				if (_numReaders > 0)
				{
					return false;
				}

				// Figure out how many generations we're going to keep
				long newTotalSize = 0;
				int keepGenerations = 0;
				while (keepGenerations < 256 && newTotalSize < targetSize)
				{
					newTotalSize += _generationSize[(int)(byte)(_generation - keepGenerations)];
					keepGenerations++;
				}

				// Create a new list of read-only items
				HashTable newItems = new HashTable(_lookup.MaxItems);
				for (int slot = 0; slot < _lookup.MaxItems; slot++)
				{
					Item item = _lookup.GetItem(slot);
					if (item.IsValid)
					{
						int age = (byte)(_generation - item.Generation);
						if (age < keepGenerations)
						{
							newItems.Add(_lookup.GetHash(slot), item);
						}
						else
						{
							freeItems.Add(item);
						}
					}
				}
				_lookup = newItems;
			}

			// Save the new state. This will ensure consistency when we restart.
			await SaveAsync();

			// Release any pages that are no longer needed
			lock (_lockObject)
			{
				foreach (Item item in freeItems)
				{
					int pageIdx = item.FirstPageIdx;
					Page page = _pages[pageIdx];

					// Free any full pages
					int pageCount = page._pageCount;
					while (page._pageCount > 0)
					{
						AddPageToList(ref _freePageIdx, pageIdx);
						pageIdx = page._nextPageIdx;
						page = _pages[page._nextPageIdx];
					}

					// Update the stats
					NumBytes -= (pageCount << PageSizeLog2) + item.TailSize;
					NumBytesWithBlockSlack -= (pageCount << PageSizeLog2) + (1 << page._blockSizeLog2);

					// If this is the first free item on the tail page, add it to the free list
					if (page._freeBitMap == 0)
					{
						AddPageToList(ref _pageIdxWithFreeBlock[page._blockSizeLog2], pageIdx);
					}

					// Mark this block as free
					page._freeBitMap |= 1UL << item.TailIndex;

					// If this page is completely empty, add it to the free page list
					if (page._freeBitMap == page.EmptyBitMap)
					{
						RemovePageFromList(ref _pageIdxWithFreeBlock[page._blockSizeLog2], pageIdx);
						AddPageToList(ref _freePageIdx, pageIdx);
						_numFreePages++;
					}
				}
			}
			return true;
		}

		static FileReference GetTransactionFile(FileReference indexFile)
		{
			return new FileReference(indexFile.FullName + ".tr");
		}
	}
}
