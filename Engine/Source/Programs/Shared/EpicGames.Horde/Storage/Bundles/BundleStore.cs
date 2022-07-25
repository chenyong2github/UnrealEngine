// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using K4os.Compression.LZ4;
using Microsoft.Extensions.Caching.Memory;

namespace EpicGames.Horde.Storage.Bundles
{
	/// <summary>
	/// Options for configuring a bundle serializer
	/// </summary>
	public class BundleOptions
	{
		/// <summary>
		/// Maximum payload size fo a blob
		/// </summary>
		public int MaxBlobSize { get; set; } = 10 * 1024 * 1024;

		/// <summary>
		/// Maximum payload size for the root blob
		/// </summary>
		public int MaxInlineBlobSize { get; set; } = 1024 * 64;

		/// <summary>
		/// Minimum size of a block to be compressed
		/// </summary>
		public int MinCompressionPacketSize { get; set; } = 16 * 1024;

		/// <summary>
		/// Number of nodes to retain in the working set after performing a partial flush
		/// </summary>
		public int TrimIgnoreCount { get; set; } = 150;

		/// <summary>
		/// Number of reads from storage to allow concurrently. This has an impact on memory usage as well as network throughput.
		/// </summary>
		public int MaxConcurrentReads { get; set; } = 5;
	}

	/// <summary>
	/// Statistics for the state of a bundle
	/// </summary>
	public class BundleStats
	{
		/// <summary>
		/// Number of blobs that have been written
		/// </summary>
		public int NewBlobCount { get; set; }

		/// <summary>
		/// Total size of the blobs that have been written
		/// </summary>
		public long NewBlobBytes { get; set; }

		/// <summary>
		/// Number of refs that have been written
		/// </summary>
		public int NewRefCount { get; set; }

		/// <summary>
		/// Total size of the refs that have been written
		/// </summary>
		public long NewRefBytes { get; set; }

		/// <summary>
		/// Number of nodes in the tree that have been written
		/// </summary>
		public int NewNodeCount { get; set; }

		/// <summary>
		/// Total size of the nodes that have been serialized
		/// </summary>
		public long NewNodeBytes { get; set; }
	}

	/// <summary>
	/// Manages a collection of bundles.
	/// </summary>
	public class BundleStore : ITreeStore, ITreeBlobWriter, IDisposable
	{
		/// <summary>
		/// Information about a node within a bundle.
		/// </summary>
		[DebuggerDisplay("{Hash}")]
		sealed class NodeInfo : ITreeBlob
		{
			public readonly BundleStore Owner;
			public IoHash Hash { get; }
			public NodeState State => _state;

			NodeState _state;

			public NodeInfo(BundleStore owner, IoHash hash, NodeState state)
			{
				Owner = owner;
				Hash = hash;
				_state = state;
			}

			public bool TrySetState(NodeState prevState, NodeState nextState)
			{
				return Interlocked.CompareExchange(ref _state, nextState, prevState) == prevState;
			}

			/// <inheritdoc/>
			public ValueTask<ReadOnlySequence<byte>> GetDataAsync(CancellationToken cancellationToken) => Owner.GetDataAsync(this, cancellationToken);

			/// <inheritdoc/>
			public async ValueTask<IReadOnlyList<ITreeBlob>> GetReferencesAsync(CancellationToken cancellationToken) => await Owner.GetReferencesAsync(this, cancellationToken);
		}

		sealed class PacketInfo
		{
			public readonly int Offset;
			public readonly int DecodedLength;
			public readonly int EncodedLength;

			public PacketInfo(int offset, BundlePacket packet)
			{
				Offset = offset;
				DecodedLength = packet.DecodedLength;
				EncodedLength = packet.EncodedLength;
			}
		}

		abstract class NodeState
		{
		}

		// State for data imported from a bundle, but whose location within it is not yet known because the bundle's header has not been read yet.
		// Can be transitioned to ExportedNodeState.
		class ImportedNodeState : NodeState
		{
			public readonly BlobInfo BundleInfo;
			public readonly int ExportIdx;

			public ImportedNodeState(BlobInfo bundleInfo, int exportIdx)
			{
				BundleInfo = bundleInfo;
				ExportIdx = exportIdx;
			}
		}

		// State for data persisted to a bundle. Decoded data is cached outside this structure in the store's MemoryCache.
		// Cannot be transitioned to any other state.
		class ExportedNodeState : ImportedNodeState
		{
			public readonly PacketInfo PacketInfo;
			public readonly int Offset;
			public readonly int Length;
			public readonly IReadOnlyList<NodeInfo> References;

			public ExportedNodeState(BlobInfo bundleInfo, int exportIdx, PacketInfo packetInfo, int offset, int length, IReadOnlyList<NodeInfo> references)
				: base(bundleInfo, exportIdx)
			{
				PacketInfo = packetInfo;
				Offset = offset;
				Length = length;
				References = references;
			}
		}

		// Data for a node which exists in memory, but which does NOT exist in storage yet. Nodes which 
		// are read in from storage are represented by ExportedNodeState and a MemoryCache. Can transition to an ExportedNodeState only.
		class InMemoryNodeState : NodeState
		{
			public readonly ReadOnlySequence<byte> Data;
			public readonly IReadOnlyList<NodeInfo> References;

			public InMemoryNodeState(ReadOnlySequence<byte> data, IReadOnlyList<NodeInfo> references)
			{
				Data = data;
				References = references;
			}
		}

		/// <summary>
		/// Information about a blob
		/// </summary>
		[DebuggerDisplay("{BlobId}")]
		class BlobInfo
		{
			public readonly BlobId BlobId;
			public readonly NodeInfo?[] Exports;
			public Task<NodeInfo>? _mountTask;

			public BlobInfo(BlobId blobId, int exportCount)
				: this(blobId, new NodeInfo?[exportCount])
			{
			}

			public BlobInfo(BlobId blobId, NodeInfo?[] exports)
			{
				BlobId = blobId;
				Exports = exports;
			}
		}

		readonly IBlobStore _blobStore;
		readonly ConcurrentDictionary<IoHash, NodeInfo> _hashToNode = new ConcurrentDictionary<IoHash, NodeInfo>();
		readonly ConcurrentDictionary<BlobId, BlobInfo> _blobIdToBundleInfo = new ConcurrentDictionary<BlobId, BlobInfo>();
		readonly IMemoryCache _cache;

		readonly object _lockObject = new object();

		readonly SemaphoreSlim _readSema;

		// Active read tasks at any moment. If a BundleObject is not available in the cache, we start a read and add an entry to this dictionary
		// so that other threads can also await it.
		readonly Dictionary<string, Task<Bundle>> _readTasks = new Dictionary<string, Task<Bundle>>();

		Task _writeTask = Task.CompletedTask;

		readonly BundleOptions _options;

		/// <summary>
		/// Tracks statistics for the size of written data
		/// </summary>
		public BundleStats Stats { get; private set; } = new BundleStats();

		BundleStats _nextStats = new BundleStats();
		byte[] _blockBuffer = Array.Empty<byte>();
		byte[] _encodedBuffer = Array.Empty<byte>();

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleStore(IBlobStore blobStore, BundleOptions options, IMemoryCache cache)
		{
			_blobStore = blobStore;
			_options = options;
			_cache = cache;
			_readSema = new SemaphoreSlim(options.MaxConcurrentReads);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		/// <summary>
		/// Dispose of this objects resources
		/// </summary>
		/// <param name="disposing"></param>
		protected virtual void Dispose(bool disposing)
		{
			_readSema.Dispose();
		}

		/// <inheritdoc/>
		public Task<bool> HasTreeAsync(RefName name, CancellationToken cancellationToken) => _blobStore.HasRefAsync(name, cancellationToken);

		/// <inheritdoc/>
		public async Task<ITreeBlob?> TryReadTreeAsync(RefName name, CancellationToken cancellationToken = default)
		{
			Bundle? bundle = await _blobStore.TryReadBundleAsync(name, cancellationToken);
			if (bundle == null)
			{
				return null;
			}
			return MountInMemoryBundle(bundle);
		}

		/// <inheritdoc/>
		public async Task<T?> TryReadTreeAsync<T>(RefName name, CancellationToken cancellationToken = default) where T : TreeNode
		{
			ITreeBlob? node = await TryReadTreeAsync(name, cancellationToken);
			if (node == null)
			{
				return null;
			}
			return await TreeNode.DeserializeAsync<T>(node, cancellationToken);
		}

		/// <inheritdoc/>
		public async Task WriteTreeAsync(RefName name, ITreeBlob root, bool flush = true, CancellationToken cancellationToken = default)
		{
			await SequenceWrite(() => WriteTreeInternalAsync(name, root, flush, cancellationToken), cancellationToken);
		}

		async Task SequenceWrite(Func<Task> writeFunc, CancellationToken cancellationToken)
		{
			Task task;
			lock (_lockObject)
			{
				Task prevTask = _writeTask;

				// Wait for the previous write to complete first, ignoring any exceptions
				Func<Task> wrappedWriteFunc = async () =>
				{
					await prevTask.ContinueWith(x => { }, cancellationToken, TaskContinuationOptions.None, TaskScheduler.Default);
					await writeFunc();
				};
				task = Task.Run(wrappedWriteFunc, cancellationToken);

				_writeTask = task;
			}
			await task;
		}

		async Task WriteTreeInternalAsync(RefName name, ITreeBlob root, bool flush = true, CancellationToken cancellationToken = default)
		{
			// Find all the referenced in-memory nodes from the root
			List<NodeInfo> nodes = new List<NodeInfo>();
			FindNodesToWrite((NodeInfo)root, nodes, new HashSet<NodeInfo>(), flush);

			// Group the nodes into bundles
			long bundleSize = 0;
			List<NodeInfo> bundleNodes = new List<NodeInfo>();

			foreach (NodeInfo node in nodes)
			{
				// Check if we need to flush the current bundle
				long nodeSize = ((InMemoryNodeState)node.State).Data.Length;
				if (bundleNodes.Count > 0 && bundleSize + nodeSize > _options.MaxBlobSize)
				{
					Bundle bundle = CreateBundle(bundleNodes);
					BlobId blobId = await _blobStore.WriteBundleBlobAsync(name, bundle, cancellationToken);

					BlobInfo bundleInfo = new BlobInfo(blobId, bundleNodes.ToArray());
					MountExportedBundle(bundleInfo, bundle);

					bundleSize = 0;
					bundleNodes.Clear();
				}

				// Add this node to the list
				bundleSize += nodeSize;
				bundleNodes.Add(node);
			}

			// If we're doing a full flush, also write whatever is left
			if (flush)
			{
				// Write the main ref
				Bundle rootBundle = CreateBundle(bundleNodes);
				await _blobStore.WriteBundleRefAsync(name, rootBundle, cancellationToken);

				// Copy the stats over
				Stats = _nextStats;
				_nextStats = new BundleStats();
			}
		}

		/// <inheritdoc/>
		public async Task WriteTreeAsync<T>(RefName name, T root, bool flush = true, CancellationToken cancellationToken = default) where T : TreeNode
		{
			ITreeBlob rootBlob = await TreeNode.SerializeAsync(root, this, cancellationToken);
			await WriteTreeAsync(name, rootBlob, flush, cancellationToken);
		}

		/// <inheritdoc/>
		public Task DeleteTreeAsync(RefName name, CancellationToken cancellationToken) => _blobStore.DeleteRefAsync(name, cancellationToken);

		/// <summary>
		/// Gets the data for a given node
		/// </summary>
		/// <param name="node">The node to get the data for</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The node data</returns>
		async ValueTask<ReadOnlySequence<byte>> GetDataAsync(NodeInfo node, CancellationToken cancellationToken)
		{
			if (node.State is InMemoryNodeState inMemoryState)
			{
				return inMemoryState.Data;
			}

			ExportedNodeState exportedState = await GetExportedStateAsync(node, cancellationToken);

			Bundle bundle = await ReadBundleAsync(exportedState.BundleInfo, cancellationToken);
			ReadOnlyMemory<byte> packetData = DecodePacket(exportedState.BundleInfo, exportedState.PacketInfo, bundle.Payload);
			ReadOnlyMemory<byte> nodeData = packetData.Slice(exportedState.Offset, exportedState.Length);

			return new ReadOnlySequence<byte>(nodeData);
		}

		/// <summary>
		/// Gets the references from a given node
		/// </summary>
		/// <param name="node">The node to get the data for</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The node data</returns>
		async ValueTask<IReadOnlyList<NodeInfo>> GetReferencesAsync(NodeInfo node, CancellationToken cancellationToken)
		{
			if (node.State is InMemoryNodeState inMemoryState)
			{
				return inMemoryState.References;
			}

			ExportedNodeState exportedState = await GetExportedStateAsync(node, cancellationToken);
			return exportedState.References;
		}

		async ValueTask<ExportedNodeState> GetExportedStateAsync(NodeInfo node, CancellationToken cancellationToken)
		{
			ExportedNodeState? exportedState = node.State as ExportedNodeState;
			if (exportedState == null)
			{
				ImportedNodeState importedState = (ImportedNodeState)node.State;
				await MountBundleAsync(importedState.BundleInfo, cancellationToken);
				exportedState = (ExportedNodeState)node.State;
			}
			return exportedState;
		}

		/// <inheritdoc/>
		public Task<ITreeBlob> WriteBlobAsync(ReadOnlySequence<byte> data, IReadOnlyList<ITreeBlob> references, CancellationToken cancellationToken)
		{
			List<NodeInfo> typedReferences = references.ConvertAll(x => (NodeInfo)x);
			if (typedReferences.Any(x => x.Owner != this))
			{
				throw new InvalidDataException("Referenced node does not belong to the current tree.");
			}

			IoHash hash = ComputeHash(data, typedReferences);

			InMemoryNodeState state = new InMemoryNodeState(data, typedReferences);
			NodeInfo node = FindOrAddNode(hash, state);

			_nextStats.NewNodeCount++;
			_nextStats.NewNodeBytes += data.Length;

			return Task.FromResult<ITreeBlob>(node);
		}

		NodeInfo FindOrAddNode(IoHash hash, NodeState state)
		{
			NodeInfo? node;
			if (!_hashToNode.TryGetValue(hash, out node))
			{
				node = _hashToNode.GetOrAdd(hash, new NodeInfo(this, hash, state));
			}

			if (state is ExportedNodeState)
			{
				// Transition from any non-exported state
				for (; ; )
				{
					NodeState prevState = node.State;
					if (prevState is ExportedNodeState)
					{
						break;
					}
					else if (node.TrySetState(prevState, state))
					{
						break;
					}
				}
			}
			else if (state is ImportedNodeState)
			{
				// Transition from in memory state
				for (; ; )
				{
					NodeState prevState = node.State;
					if (!(prevState is InMemoryNodeState))
					{
						break;
					}
					else if (node.TrySetState(prevState, state))
					{
						break;
					}
				}
			}

			return node;
		}

		static IoHash ComputeHash(ReadOnlySequence<byte> data, IReadOnlyList<NodeInfo> references)
		{
			byte[] buffer = new byte[IoHash.NumBytes * (references.Count + 1)];

			Span<byte> span = buffer;
			for (int idx = 0; idx < references.Count; idx++)
			{
				references[idx].Hash.CopyTo(span);
				span = span[IoHash.NumBytes..];
			}
			IoHash.Compute(data).CopyTo(span);

			return IoHash.Compute(buffer);
		}

		void FindNodesToWrite(NodeInfo root, List<NodeInfo> nodes, HashSet<NodeInfo> uniqueNodes, bool flush)
		{
			if (root.State is InMemoryNodeState inMemoryState)
			{
				if (uniqueNodes.Add(root))
				{
					int count = inMemoryState.References.Count;
					for (int idx = 0; idx < count; idx++)
					{
						FindNodesToWrite(inMemoryState.References[idx], nodes, uniqueNodes, flush || idx < count - 1);
					}
					if (flush)
					{
						nodes.Add(root);
					}
				}
			}
		}

		async Task<NodeInfo> MountBundleAsync(BlobInfo bundle, CancellationToken cancellationToken)
		{
			Task<NodeInfo>? mountTask = bundle._mountTask;
			if (mountTask == null)
			{
				lock (bundle)
				{
					bundle._mountTask ??= Task.Run(() => MountBundleInternalAsync(bundle, cancellationToken));
					mountTask = bundle._mountTask;
				}
			}
			return await mountTask;
		}

		async Task<NodeInfo> MountBundleInternalAsync(BlobInfo bundleInfo, CancellationToken cancellationToken)
		{
			Bundle bundle = await ReadBundleAsync(bundleInfo, cancellationToken);
			return MountExportedBundle(bundleInfo, bundle);
		}

		List<NodeInfo> RegisterImports(BundleHeader header)
		{
			// Create all the nodes as imports first, so we can fix up references later.
			List<NodeInfo> nodes = new List<NodeInfo>(header.Imports.Sum(x => x.Exports.Count) + header.Exports.Count);
			foreach (BundleImport import in header.Imports)
			{
				BlobInfo importBundle = _blobIdToBundleInfo.GetOrAdd(import.BlobId, id => new BlobInfo(id, import.ExportCount));
				foreach ((int exportIdx, IoHash exportHash) in import.Exports)
				{
					NodeInfo? export = importBundle.Exports[exportIdx];
					if (export == null)
					{
						export = FindOrAddNode(exportHash, new ImportedNodeState(importBundle, exportIdx));
						importBundle.Exports[exportIdx] = export;
					}
					nodes.Add(export);
				}
			}
			return nodes;
		}

		NodeInfo MountInMemoryBundle(Bundle bundle)
		{
			BundleHeader header = bundle.Header;
			List<NodeInfo> nodes = RegisterImports(header);

			int exportIdx = 0;
			int packetOffset = 0;
			foreach (BundlePacket packet in header.Packets)
			{
				ReadOnlyMemory<byte> encodedPacket = bundle.Payload.Slice(packetOffset, packet.EncodedLength);

				byte[] decodedPacket = new byte[packet.DecodedLength];
				LZ4Codec.Decode(encodedPacket.Span, decodedPacket);

				int nodeOffset = 0;
				for (; exportIdx < header.Exports.Count && nodeOffset + header.Exports[exportIdx].Length <= packet.DecodedLength; exportIdx++)
				{
					BundleExport export = header.Exports[exportIdx];

					ReadOnlyMemory<byte> nodeData = decodedPacket.AsMemory(nodeOffset, export.Length);
					List<NodeInfo> nodeRefs = export.References.ConvertAll(x => nodes[x]);

					InMemoryNodeState nodeState = new InMemoryNodeState(new ReadOnlySequence<byte>(nodeData), nodeRefs);
					NodeInfo node = FindOrAddNode(export.Hash, nodeState);
					nodes.Add(node);

					nodeOffset += header.Exports[exportIdx].Length;
				}

				packetOffset += packet.EncodedLength;
			}

			return nodes[^1];
		}

		NodeInfo MountExportedBundle(BlobInfo bundleInfo, Bundle bundle)
		{
			BundleHeader header = bundle.Header;
			List<NodeInfo> nodes = RegisterImports(header);

			int exportIdx = 0;
			int packetOffset = 0;
			foreach (BundlePacket packet in header.Packets)
			{
				PacketInfo packetInfo = new PacketInfo(packetOffset, packet);

				int nodeOffset = 0;
				for (; exportIdx < header.Exports.Count && nodeOffset + header.Exports[exportIdx].Length <= packet.DecodedLength; exportIdx++)
				{
					BundleExport export = header.Exports[exportIdx];

					List<NodeInfo> nodeRefs = export.References.ConvertAll(x => nodes[x]);
					ExportedNodeState exportedState = new ExportedNodeState(bundleInfo, exportIdx, packetInfo, nodeOffset, export.Length, nodeRefs);

					NodeInfo node = FindOrAddNode(export.Hash, exportedState);
					bundleInfo.Exports[exportIdx] = node;
					nodes.Add(node);

					nodeOffset += header.Exports[exportIdx].Length;
				}

				packetOffset += packet.EncodedLength;
			}

			return nodes[^1];
		}

		async Task<Bundle> ReadBundleAsync(BlobInfo bundleInfo, CancellationToken cancellationToken)
		{
			string cacheKey = $"bundle:{bundleInfo.BlobId}";
			if (!_cache.TryGetValue<Bundle>(cacheKey, out Bundle? bundle))
			{
				Task<Bundle>? readTask;
				lock (_lockObject)
				{
					if (!_readTasks.TryGetValue(cacheKey, out readTask))
					{
						readTask = Task.Run(() => ReadBundleInternalAsync(cacheKey, bundleInfo.BlobId, cancellationToken));
						_readTasks.Add(cacheKey, readTask);
					}
				}
				bundle = await readTask;
			}
			return bundle;
		}

		async Task<Bundle> ReadBundleInternalAsync(string cacheKey, BlobId blobId, CancellationToken cancellationToken)
		{
			// Perform another (sequenced) check whether an object has been added to the cache, to counteract the race between a read task being added and a task completing.
			lock (_lockObject)
			{
				if (_cache.TryGetValue<Bundle>(cacheKey, out Bundle? cachedObject))
				{
					return cachedObject;
				}
			}

			// Wait for the read semaphore to avoid triggering too many operations at once.
			await _readSema.WaitAsync(cancellationToken);

			// Read the data from storage
			Bundle bundle;
			try
			{
				bundle = await _blobStore.ReadBundleAsync(blobId, cancellationToken);
			}
			finally
			{
				_readSema.Release();
			}

			// Add the object to the cache
			using (ICacheEntry entry = _cache.CreateEntry(cacheKey))
			{
				entry.SetSize(bundle.Payload.Length);
				entry.SetValue(bundle);
			}

			// Remove this object from the list of read tasks
			lock (_lockObject)
			{
				_readTasks.Remove(cacheKey);
			}
			return bundle;
		}

		/// <summary>
		/// Gets a decoded block from the store
		/// </summary>
		/// <param name="bundleInfo">Information about the bundle</param>
		/// <param name="packetInfo">The decoded block location and size</param>
		/// <param name="payload">The bundle payload</param>
		/// <returns>The decoded data</returns>
		ReadOnlyMemory<byte> DecodePacket(BlobInfo bundleInfo, PacketInfo packetInfo, ReadOnlyMemory<byte> payload)
		{
			string cacheKey = $"bundle-packet:{bundleInfo.BlobId}@{packetInfo.Offset}";
			return _cache.GetOrCreate<ReadOnlyMemory<byte>>(cacheKey, entry =>
			{
				ReadOnlyMemory<byte> decodedPacket = DecodePacketUncached(packetInfo, payload);
				entry.SetSize(packetInfo.DecodedLength);
				return decodedPacket;
			});
		}

		static ReadOnlyMemory<byte> DecodePacketUncached(PacketInfo packetInfo, ReadOnlyMemory<byte> payload)
		{
			ReadOnlyMemory<byte> encodedPacket = payload.Slice(packetInfo.Offset, packetInfo.EncodedLength);

			byte[] decodedPacket = new byte[packetInfo.DecodedLength];
			LZ4Codec.Decode(encodedPacket.Span, decodedPacket);

			return decodedPacket;
		}

		async Task FindLiveNodes(NodeInfo node, Dictionary<BlobInfo, HashSet<NodeInfo>> bundleToLiveNodes, HashSet<NodeInfo> nodes, CancellationToken cancellationToken)
		{
			if (nodes.Add(node))
			{
				if (node.State is ImportedNodeState importedState)
				{
					HashSet<NodeInfo>? liveNodes;
					if (!bundleToLiveNodes.TryGetValue(importedState.BundleInfo, out liveNodes))
					{
						liveNodes = new HashSet<NodeInfo>();
						bundleToLiveNodes.Add(importedState.BundleInfo, liveNodes);
					}
					liveNodes.Add(node);
				}

				foreach (NodeInfo reference in await GetReferencesAsync(node, cancellationToken))
				{
					await FindLiveNodes(reference, bundleToLiveNodes, nodes, cancellationToken);
				}
			}
		}

		/// <summary>
		/// Creates a Bundle containing a set of nodes. 
		///
		/// WARNING: The <see cref="Bundle.Payload"/> member will be set to the value of <see cref="_encodedBuffer"/> on return, which will be
		/// reused on a subsequent call. If the object needs to be persisted across the lifetime of other objects, this field must be duplicated.
		/// </summary>
		Bundle CreateBundle(IReadOnlyList<NodeInfo> nodes)
		{
			// Create a set from the nodes to be written. We use this to determine references that are imported.
			HashSet<NodeInfo> nodeSet = new HashSet<NodeInfo>(nodes);

			// Find all the imported nodes by bundle
			Dictionary<BlobInfo, List<NodeInfo>> bundleToImportedNodes = new Dictionary<BlobInfo, List<NodeInfo>>();
			foreach (NodeInfo node in nodes)
			{
				InMemoryNodeState state = (InMemoryNodeState)node.State;
				foreach (NodeInfo reference in state.References)
				{
					if (nodeSet.Add(reference))
					{
						// Get the persisted node info
						ImportedNodeState importedState = (ImportedNodeState)reference.State;
						BlobInfo bundleInfo = importedState.BundleInfo;

						// Get the list of nodes within it
						List<NodeInfo>? importedNodes;
						if (!bundleToImportedNodes.TryGetValue(bundleInfo, out importedNodes))
						{
							importedNodes = new List<NodeInfo>();
							bundleToImportedNodes.Add(bundleInfo, importedNodes);
						}
						importedNodes.Add(reference);
					}
				}
			}

			// Create the import list
			List<BundleImport> imports = new List<BundleImport>();

			// Add all the imports and assign them identifiers
			Dictionary<NodeInfo, int> nodeToIndex = new Dictionary<NodeInfo, int>();
			foreach ((BlobInfo bundleInfo, List<NodeInfo> importedNodes) in bundleToImportedNodes)
			{
				(int, IoHash)[] exportInfos = new (int, IoHash)[importedNodes.Count];
				for (int idx = 0; idx < importedNodes.Count; idx++)
				{
					NodeInfo importedNode = importedNodes[idx];
					nodeToIndex.Add(importedNode, nodeToIndex.Count);

					ImportedNodeState importedNodeState = (ImportedNodeState)importedNode.State;
					exportInfos[idx] = (importedNodeState.ExportIdx, importedNode.Hash);
				}
				imports.Add(new BundleImport(bundleInfo.BlobId, bundleInfo.Exports.Length, exportInfos));
			}

			// Preallocate data in the encoded buffer to reduce fragmentation if we have to resize
			CreateFreeSpace(ref _encodedBuffer, 0, _options.MaxBlobSize);

			// Create the export list
			List<BundleExport> exports = new List<BundleExport>();
			List<BundlePacket> packets = new List<BundlePacket>();

			// Size of data currently stored in the block buffer
			int blockSize = 0;

			// Compress all the nodes into the encoded buffer
			int packetOffset = 0;
			foreach (NodeInfo node in nodes)
			{
				InMemoryNodeState nodeState = (InMemoryNodeState)node.State;
				ReadOnlySequence<byte> nodeData = nodeState.Data;

				// If we can't fit this data into the current block, flush the contents of it first
				if (blockSize > 0 && blockSize + nodeData.Length > _options.MinCompressionPacketSize)
				{
					FlushPacket(_blockBuffer.AsMemory(0, blockSize), ref packetOffset, packets);
					blockSize = 0;
				}

				// Create the export for this node
				int[] references = nodeState.References.Select(x => nodeToIndex[x]).ToArray();
				BundleExport export = new BundleExport(node.Hash, (int)nodeData.Length, references);
				exports.Add(export);
				nodeToIndex[node] = nodeToIndex.Count;

				// Write out the new block
				if (nodeData.Length < _options.MinCompressionPacketSize || !nodeData.IsSingleSegment)
				{
					int requiredSize = Math.Max(blockSize + (int)nodeData.Length, (int)(_options.MaxBlobSize * 1.2));
					CreateFreeSpace(ref _blockBuffer, blockSize, requiredSize);

					foreach (ReadOnlyMemory<byte> nodeSegment in nodeData)
					{
						nodeSegment.CopyTo(_blockBuffer.AsMemory(blockSize));
						blockSize += nodeSegment.Length;
					}
				}
				else
				{
					FlushPacket(nodeData.First, ref packetOffset, packets);
				}
			}
			FlushPacket(_blockBuffer.AsMemory(0, blockSize), ref packetOffset, packets);

			// Flush the data
			BundleHeader header = new BundleHeader(imports, exports, packets);
			ReadOnlyMemory<byte> payload = _encodedBuffer.AsMemory(0, packets.Sum(x => x.EncodedLength));
			return new Bundle(header, payload);
		}

		void FlushPacket(ReadOnlyMemory<byte> inputData, ref int packetOffset, List<BundlePacket> packets)
		{
			if (inputData.Length > 0)
			{
				int minFreeSpace = LZ4Codec.MaximumOutputSize(inputData.Length);
				CreateFreeSpace(ref _encodedBuffer, packetOffset, packetOffset + minFreeSpace);

				ReadOnlySpan<byte> inputSpan = inputData.Span;
				Span<byte> outputSpan = _encodedBuffer.AsSpan(packetOffset);

				int encodedLength = LZ4Codec.Encode(inputSpan, outputSpan);
				packets.Add(new BundlePacket(encodedLength, inputData.Length));

				packetOffset += encodedLength;

				Debug.Assert(encodedLength >= 0);
			}
		}

		static void CreateFreeSpace(ref byte[] buffer, int usedSize, int requiredSize)
		{
			if (requiredSize > buffer.Length)
			{
				byte[] newBuffer = new byte[requiredSize];
				buffer.AsSpan(0, usedSize).CopyTo(newBuffer);
				buffer = newBuffer;
			}
		}
	}
}
