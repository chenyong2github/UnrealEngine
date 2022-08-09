// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
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
	/// Implementation of <see cref="ITreeStore"/> which packs nodes together into <see cref="Bundle"/> objects, then stores them
	/// in a <see cref="IBlobStore"/>.
	/// </summary>
	public class BundleStore : ITreeStore, IDisposable
	{
		/// <summary>
		/// Information about a node within a bundle.
		/// </summary>
		[DebuggerDisplay("{Hash}")]
		sealed class NodeInfo : ITreeBlobRef
		{
			public readonly BundleStore Owner;
			public readonly IoHash Hash;
			public NodeState State { get; private set; }

			public NodeInfo(BundleStore owner, IoHash hash, NodeState state)
			{
				Owner = owner;
				Hash = hash;
				State = state;
			}

			/// <summary>
			/// Updates the current state. The only allowed transition is to an exported node state.
			/// </summary>
			/// <param name="state">The new state</param>
			public void SetState(ExportedNodeState state)
			{
				State = state;
			}

			/// <inheritdoc/>
			public ValueTask<ITreeBlob> GetTargetAsync(CancellationToken cancellationToken = default) => Owner.GetNodeAsync(this, cancellationToken);
		}

		/// <summary>
		/// Metadata about a compression packet within a bundle
		/// </summary>
		class PacketInfo
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

		/// <summary>
		/// Base class for the state of a node
		/// </summary>
		abstract class NodeState
		{
		}

		/// <summary>
		/// State for data imported from a bundle, but whose location within it is not yet known because the bundle's header has not been read yet.
		/// Can be transitioned to ExportedNodeState.
		/// </summary>
		class ImportedNodeState : NodeState
		{
			public readonly BundleInfo BundleInfo;
			public readonly int ExportIdx;

			public ImportedNodeState(BundleInfo bundleInfo, int exportIdx)
			{
				BundleInfo = bundleInfo;
				ExportIdx = exportIdx;
			}
		}

		/// <summary>
		/// State for data persisted to a bundle. Decoded data is cached outside this structure in the store's MemoryCache.
		/// Cannot be transitioned to any other state.
		/// </summary>
		class ExportedNodeState : ImportedNodeState
		{
			public readonly PacketInfo PacketInfo;
			public readonly int Offset;
			public readonly int Length;
			public readonly IReadOnlyList<NodeInfo> References;

			public ExportedNodeState(BundleInfo bundleInfo, int exportIdx, PacketInfo packetInfo, int offset, int length, IReadOnlyList<NodeInfo> references)
				: base(bundleInfo, exportIdx)
			{
				PacketInfo = packetInfo;
				Offset = offset;
				Length = length;
				References = references;
			}
		}

		/// <summary>
		/// Data for a node which exists in memory, but which does NOT exist in storage yet. Nodes which 
		/// are read in from storage are represented by ExportedNodeState and a MemoryCache. Can transition to an ExportedNodeState only.
		/// </summary>
		class InMemoryNodeState : NodeState, ITreeBlob
		{
			public readonly ReadOnlySequence<byte> Data;
			public readonly IReadOnlyList<NodeInfo> References;

			ReadOnlySequence<byte> ITreeBlob.Data => Data;
			IReadOnlyList<ITreeBlobRef> ITreeBlob.Refs => References;

			public InMemoryNodeState(ReadOnlySequence<byte> data, IReadOnlyList<NodeInfo> references)
			{
				Data = data;
				References = references;
			}
		}

		/// <summary>
		/// Stores a lookup from blob id to bundle info, and from there to the nodes it contains.
		/// </summary>
		class BundleContext
		{
			readonly ConcurrentDictionary<BlobId, BundleInfo> _blobIdToBundle = new ConcurrentDictionary<BlobId, BundleInfo>();

			public BundleInfo FindOrAddBundle(BlobId blobId, int exportCount)
			{
				BundleInfo bundleInfo = _blobIdToBundle.GetOrAdd(blobId, new BundleInfo(this, blobId, exportCount));
				Debug.Assert(bundleInfo.Exports.Length == exportCount);
				return bundleInfo;
			}
		}

		/// <summary>
		/// Information about an imported bundle
		/// </summary>
		[DebuggerDisplay("{BlobId}")]
		class BundleInfo
		{
			public readonly BundleContext Context;
			public readonly BlobId BlobId;
			public readonly NodeInfo?[] Exports;

			public bool Mounted;
			public Task MountTask = Task.CompletedTask;

			public BundleInfo(BundleContext context, BlobId blobId, int exportCount)
			{
				Context = context;
				BlobId = blobId;
				Exports = new NodeInfo?[exportCount];
			}
		}

		class BundleWriteContext : BundleContext, ITreeWriter
		{
			readonly BundleStore _owner;

			public readonly RefName Name;
			public readonly ConcurrentDictionary<IoHash, NodeInfo> HashToNode = new ConcurrentDictionary<IoHash, NodeInfo>();

			public Task WriteTask = Task.CompletedTask;
			public readonly List<NodeInfo> WriteNodes = new List<NodeInfo>();
			public readonly HashSet<NodeInfo> WriteNodesSet = new HashSet<NodeInfo>();
			public long WriteLength;

			public byte[] PacketBuffer = Array.Empty<byte>();

			public BundleWriteContext(BundleStore owner, RefName name)
			{
				_owner = owner;
				Name = name;
			}

			/// <inheritdoc/>
			public Task FlushAsync(ITreeBlob root, CancellationToken cancellationToken = default) => _owner.FlushAsync(this, root, cancellationToken);

			/// <inheritdoc/>
			public Task<ITreeBlobRef> WriteNodeAsync(ReadOnlySequence<byte> data, IReadOnlyList<ITreeBlobRef> refs, CancellationToken cancellationToken = default) => _owner.WriteNodeAsync(this, data, refs, cancellationToken);
		}

		readonly IBlobStore _blobStore;
		readonly IMemoryCache _cache;

		readonly object _lockObject = new object();

		readonly SemaphoreSlim _readSema;

		// Active read tasks at any moment. If a BundleObject is not available in the cache, we start a read and add an entry to this dictionary
		// so that other threads can also await it.
		readonly Dictionary<string, Task<Bundle>> _readTasks = new Dictionary<string, Task<Bundle>>();

		readonly BundleOptions _options;

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
		public ITreeWriter CreateTreeWriter(RefName name) => new BundleWriteContext(this, name);

		/// <inheritdoc/>
		public Task DeleteTreeAsync(RefName name, CancellationToken cancellationToken) => _blobStore.DeleteRefAsync(name, cancellationToken);

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

			// Create a new context for this bundle and its references
			BundleContext context = new BundleContext();

			// Create all the imports for the root bundle
			BundleHeader header = bundle.Header;
			List<NodeInfo> nodes = CreateImports(context, header);

			// Decompress the bundle data and mount all the nodes in memory
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

					NodeInfo node = new NodeInfo(this, export.Hash, new InMemoryNodeState(new ReadOnlySequence<byte>(nodeData), nodeRefs));
					nodes.Add(node);

					nodeOffset += export.Length;
				}

				packetOffset += packet.EncodedLength;
			}

			// Return the last node as the root
			return (InMemoryNodeState)nodes[^1].State;
		}

		/// <summary>
		/// Gets the data for a given node
		/// </summary>
		/// <param name="node">The node to get the data for</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The node data</returns>
		async ValueTask<ITreeBlob> GetNodeAsync(NodeInfo node, CancellationToken cancellationToken)
		{
			if (node.State is InMemoryNodeState inMemoryState)
			{
				return inMemoryState;
			}

			ExportedNodeState exportedState = await GetExportedStateAsync(node, cancellationToken);

			Bundle bundle = await ReadBundleAsync(exportedState.BundleInfo.BlobId, cancellationToken);
			ReadOnlyMemory<byte> packetData = DecodePacket(exportedState.BundleInfo.BlobId, exportedState.PacketInfo, bundle.Payload);
			ReadOnlyMemory<byte> nodeData = packetData.Slice(exportedState.Offset, exportedState.Length);

			return new InMemoryNodeState(new ReadOnlySequence<byte>(nodeData), exportedState.References);
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

		#region Reading bundles

		/// <summary>
		/// Reads a bundle from the given blob id, or retrieves it from the cache
		/// </summary>
		/// <param name="blobId"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		async Task<Bundle> ReadBundleAsync(BlobId blobId, CancellationToken cancellationToken = default)
		{
			string cacheKey = GetBundleCacheKey(blobId);
			if (!_cache.TryGetValue<Bundle>(cacheKey, out Bundle? bundle))
			{
				Task<Bundle>? readTask;
				lock (_lockObject)
				{
					if (!_readTasks.TryGetValue(cacheKey, out readTask))
					{
						readTask = Task.Run(() => ReadBundleInternalAsync(cacheKey, blobId, cancellationToken), cancellationToken);
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

		static string GetBundleCacheKey(BlobId blobId) => $"bundle:{blobId}";

		void AddBundleToCache(string cacheKey, Bundle bundle)
		{
			using (ICacheEntry entry = _cache.CreateEntry(cacheKey))
			{
				entry.SetSize(bundle.Payload.Length);
				entry.SetValue(bundle);
			}
		}

		async Task MountBundleAsync(BundleInfo bundleInfo, CancellationToken cancellationToken)
		{
			if (!bundleInfo.Mounted)
			{
				Task mountTask;
				lock (bundleInfo)
				{
					Task prevMountTask = bundleInfo.MountTask.ContinueWith(x => { }, cancellationToken, TaskContinuationOptions.None, TaskScheduler.Default);
					mountTask = Task.Run(() => MountBundleInternalAsync(prevMountTask, bundleInfo, cancellationToken), cancellationToken);
					bundleInfo.MountTask = mountTask;
				}
				await mountTask;
			}
		}

		async Task MountBundleInternalAsync(Task prevMountTask, BundleInfo bundleInfo, CancellationToken cancellationToken)
		{
			// Wait for the previous mount task to complete. This may succeed or be cancelled.
			await prevMountTask;

			// If it didn't succeed, try again
			if (!bundleInfo.Mounted)
			{
				// Read the bundle data
				Bundle bundle = await ReadBundleAsync(bundleInfo.BlobId, cancellationToken);
				BundleHeader header = bundle.Header;

				// Create all the imported nodes
				List<NodeInfo> nodes = CreateImports(bundleInfo.Context, header);

				// Create the exported nodes, or update the state of any imported nodes to exported
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

						ExportedNodeState state = new ExportedNodeState(bundleInfo, exportIdx, packetInfo, nodeOffset, export.Length, nodeRefs);

						NodeInfo? node = bundleInfo.Exports[exportIdx];
						if (node == null)
						{
							node = bundleInfo.Exports[exportIdx] = new NodeInfo(this, export.Hash, state);
						}
						else
						{
							node.SetState(state);
						}
						nodes.Add(node);

						nodeOffset += export.Length;
					}

					packetOffset += packet.EncodedLength;
				}

				// Mark the bundle as mounted
				bundleInfo.Mounted = true;
			}
		}

		List<NodeInfo> CreateImports(BundleContext context, BundleHeader header)
		{
			List<NodeInfo> indexedNodes = new List<NodeInfo>(header.Imports.Sum(x => x.Exports.Count) + header.Exports.Count);
			foreach (BundleImport import in header.Imports)
			{
				BundleInfo importBundle = context.FindOrAddBundle(import.BlobId, import.ExportCount);
				foreach ((int exportIdx, IoHash exportHash) in import.Exports)
				{
					NodeInfo? node = importBundle.Exports[exportIdx];
					if (node == null)
					{
						node = new NodeInfo(this, exportHash, new ImportedNodeState(importBundle, exportIdx));
						importBundle.Exports[exportIdx] = node;
					}
					indexedNodes.Add(node);
				}
			}
			return indexedNodes;
		}
		/// <summary>
		/// Gets a decoded block from the store
		/// </summary>
		/// <param name="blobId">Information about the bundle</param>
		/// <param name="packetInfo">The decoded block location and size</param>
		/// <param name="payload">The bundle payload</param>
		/// <returns>The decoded data</returns>
		ReadOnlyMemory<byte> DecodePacket(BlobId blobId, PacketInfo packetInfo, ReadOnlyMemory<byte> payload)
		{
			string cacheKey = $"bundle-packet:{blobId}@{packetInfo.Offset}";
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

		#endregion

		#region Writing bundles

		Task<ITreeBlobRef> WriteNodeAsync(BundleWriteContext context, ReadOnlySequence<byte> data, IReadOnlyList<ITreeBlobRef> refs, CancellationToken cancellationToken = default)
		{
			IReadOnlyList<NodeInfo> typedRefs = refs.Select(x => (NodeInfo)x).ToList();

			IoHash hash = ComputeHash(data, typedRefs);

			NodeInfo? node;
			if (!context.HashToNode.TryGetValue(hash, out node))
			{
				InMemoryNodeState state = new InMemoryNodeState(data, typedRefs);
				NodeInfo newNode = new NodeInfo(this, hash, state);

				// Try to add the node again. If we succeed, check if we need to flush the current bundle being built.
				node = context.HashToNode.GetOrAdd(hash, newNode);
				if (node == newNode)
				{
					lock (context)
					{
						WriteInMemoryNodes(context, node);
					}
				}
			}

			return Task.FromResult<ITreeBlobRef>(node);
		}

		void WriteInMemoryNodes(BundleWriteContext context, NodeInfo root)
		{
			if (root.State is InMemoryNodeState inMemoryState)
			{
				if (context.WriteNodesSet.Add(root))
				{
					// Write all the references
					foreach (NodeInfo reference in inMemoryState.References)
					{
						WriteInMemoryNodes(context, reference);
					}

					// If this node pushes the current blob over the max size, queue it to be written now
					long length = inMemoryState.Data.Length;
					if (context.WriteLength + length > _options.MaxBlobSize)
					{
						// Start the write, but don't wait for it to complete. Once we've copied the list of nodes they can be written asynchronously.
						NodeInfo[] writeNodes = context.WriteNodes.ToArray();
						_ = SequenceWriteAsync(context, () => WriteBundleAsync(context, writeNodes, CancellationToken.None), CancellationToken.None);

						context.WriteNodes.Clear();
						context.WriteLength = 0;
					}

					// Add this node to the list of nodes to be written
					context.WriteNodes.Add(root);
					context.WriteLength += length;
				}
			}
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

		async Task WriteBundleAsync(BundleWriteContext context, NodeInfo[] nodes, CancellationToken cancellationToken)
		{
			// Create the bundle
			Bundle bundle = CreateBundle(context, nodes);
			BundleHeader header = bundle.Header;

			// Write it to storage
			BlobId blobId = await _blobStore.WriteBundleBlobAsync(context.Name, bundle, cancellationToken);
			string cacheKey = GetBundleCacheKey(blobId);
			AddBundleToCache(cacheKey, bundle);

			// Create a BundleInfo for it
			BundleInfo bundleInfo = context.FindOrAddBundle(blobId, nodes.Length);
			for (int idx = 0; idx < nodes.Length; idx++)
			{
				bundleInfo.Exports[idx] = nodes[idx];
			}

			// Update the node states to reference the written bundle
			int exportIdx = 0;
			int packetOffset = 0;
			foreach (BundlePacket packet in bundle.Header.Packets)
			{
				PacketInfo packetInfo = new PacketInfo(packetOffset, packet);

				int nodeOffset = 0;
				for (; exportIdx < header.Exports.Count && nodeOffset + bundle.Header.Exports[exportIdx].Length <= packet.DecodedLength; exportIdx++)
				{
					InMemoryNodeState inMemoryState = (InMemoryNodeState)nodes[exportIdx].State;

					BundleExport export = header.Exports[exportIdx];
					nodes[exportIdx].SetState(new ExportedNodeState(bundleInfo, exportIdx, packetInfo, nodeOffset, export.Length, inMemoryState.References));

					nodeOffset += header.Exports[exportIdx].Length;
				}

				packetOffset += packet.EncodedLength;
			}

			// Remove all the newly written nodes from the dirty set
			lock (context)
			{
				context.WriteNodesSet.ExceptWith(nodes);
			}
		}

		async Task FlushAsync(BundleWriteContext context, ITreeBlob root, CancellationToken cancellationToken = default)
		{
			// Create the root node, and add everything it references to the write buffer
			IoHash rootHash = ComputeHash(root.Data, root.Refs.ConvertAll(x => (NodeInfo)x));
			NodeInfo rootNode = new NodeInfo(this, rootHash, (InMemoryNodeState)root);

			// Flush all the nodes to bundles, and snapshot the remaining ref
			Task task;
			lock (context)
			{
				WriteInMemoryNodes(context, rootNode);
				NodeInfo[] writeNodes = context.WriteNodes.ToArray();
				task = SequenceWriteAsync(context, () => FlushInternalAsync(context, writeNodes, cancellationToken), cancellationToken);
			}
			await task;
		}

		async Task FlushInternalAsync(BundleWriteContext context, NodeInfo[] writeNodes, CancellationToken cancellationToken)
		{
			Bundle bundle = CreateBundle(context, writeNodes);
			await _blobStore.WriteBundleRefAsync(context.Name, bundle, cancellationToken);
		}

		void FindNodesToWrite(NodeInfo root, List<NodeInfo> nodes, HashSet<NodeInfo> uniqueNodes)
		{
			if (root.State is InMemoryNodeState inMemoryState)
			{
				if (uniqueNodes.Add(root))
				{
					foreach (NodeInfo reference in inMemoryState.References)
					{
						FindNodesToWrite(reference, nodes, uniqueNodes);
					}
					nodes.Add(root);
				}
			}
		}

		static async Task SequenceWriteAsync(BundleWriteContext context, Func<Task> writeFunc, CancellationToken cancellationToken)
		{
			Task task;
			lock (context)
			{
				Task prevTask = context.WriteTask;

				// Wait for the previous write to complete first, ignoring any exceptions
				Func<Task> wrappedWriteFunc = async () =>
				{
					await prevTask.ContinueWith(x => { }, cancellationToken, TaskContinuationOptions.None, TaskScheduler.Default);
					await writeFunc();
				};
				task = Task.Run(wrappedWriteFunc, cancellationToken);

				context.WriteTask = task;
			}
			await task;
		}

		/// <summary>
		/// Creates a Bundle containing a set of nodes. 
		/// </summary>
		Bundle CreateBundle(BundleWriteContext context, NodeInfo[] nodes)
		{
			// Create a set from the nodes to be written. We use this to determine references that are imported.
			HashSet<NodeInfo> nodeSet = new HashSet<NodeInfo>(nodes);

			// Find all the imported nodes by bundle
			Dictionary<BundleInfo, List<NodeInfo>> bundleToImportedNodes = new Dictionary<BundleInfo, List<NodeInfo>>();
			foreach (NodeInfo node in nodes)
			{
				InMemoryNodeState state = (InMemoryNodeState)node.State;
				foreach (NodeInfo reference in state.References)
				{
					if (nodeSet.Add(reference))
					{
						// Get the persisted node info
						ImportedNodeState importedState = (ImportedNodeState)reference.State;
						BundleInfo bundleInfo = importedState.BundleInfo;

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
			foreach ((BundleInfo bundleInfo, List<NodeInfo> importedNodes) in bundleToImportedNodes)
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
			byte[] outputBuffer = new byte[_options.MaxBlobSize];

			// Create the export list
			List<BundleExport> exports = new List<BundleExport>();
			List<BundlePacket> packets = new List<BundlePacket>();

			// Size of data currently stored in the block buffer
			int blockSize = 0;

			// Compress all the nodes into the output buffer buffer
			int outputOffset = 0;
			foreach (NodeInfo node in nodes)
			{
				InMemoryNodeState nodeState = (InMemoryNodeState)node.State;
				ReadOnlySequence<byte> nodeData = nodeState.Data;

				// If we can't fit this data into the current block, flush the contents of it first
				if (blockSize > 0 && blockSize + nodeData.Length > _options.MinCompressionPacketSize)
				{
					FlushPacket(context.PacketBuffer.AsMemory(0, blockSize), ref outputBuffer, ref outputOffset, packets);
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
					CreateFreeSpace(ref context.PacketBuffer, blockSize, requiredSize);

					foreach (ReadOnlyMemory<byte> nodeSegment in nodeData)
					{
						nodeSegment.CopyTo(context.PacketBuffer.AsMemory(blockSize));
						blockSize += nodeSegment.Length;
					}
				}
				else
				{
					FlushPacket(nodeData.First, ref outputBuffer, ref outputOffset, packets);
				}
			}
			FlushPacket(context.PacketBuffer.AsMemory(0, blockSize), ref outputBuffer, ref outputOffset, packets);

			// Flush the data
			BundleHeader header = new BundleHeader(imports, exports, packets);
			ReadOnlyMemory<byte> payload = outputBuffer.AsMemory(0, packets.Sum(x => x.EncodedLength));
			return new Bundle(header, payload);
		}

		static void FlushPacket(ReadOnlyMemory<byte> inputData, ref byte[] outputBuffer, ref int outputOffset, List<BundlePacket> packets)
		{
			if (inputData.Length > 0)
			{
				int minFreeSpace = LZ4Codec.MaximumOutputSize(inputData.Length);
				CreateFreeSpace(ref outputBuffer, outputOffset, outputOffset + minFreeSpace);

				ReadOnlySpan<byte> inputSpan = inputData.Span;
				Span<byte> outputSpan = outputBuffer.AsSpan(outputOffset);

				int encodedLength = LZ4Codec.Encode(inputSpan, outputSpan);
				packets.Add(new BundlePacket(encodedLength, inputData.Length));

				outputOffset += encodedLength;

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

		#endregion
	}
}
