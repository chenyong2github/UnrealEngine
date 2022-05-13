// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using K4os.Compression.LZ4;
using Microsoft.Extensions.Caching.Memory;
using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Bundles
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
		/// Proportion of used data under which to trigger a re-pack
		/// </summary>
		public float RepackRatio { get; set; } = 0.6f;

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
	/// Base class for manipulating bundles
	/// </summary>
	public class Bundle : IDisposable
	{
		/// <summary>
		/// Information about a node within a bundle.
		/// </summary>
		[DebuggerDisplay("{Hash}")]
		sealed class NodeInfo
		{
			public IoHash Hash { get; }
			public int Rank { get; }
			public int Length { get; }

			public NodeState State
			{
				get => _state;
				set => _state = value;
			}

			private NodeState _state;

			public NodeInfo(IoHash hash, int rank, int length, NodeState state)
			{
				if (length <= 0)
				{
					throw new ArgumentException("Node length must be greater than zero", nameof(length));
				}

				Hash = hash;
				Rank = rank;
				Length = length;
				_state = state;
			}

			public void TrySetExport(BlobInfo blob, BundleExport export, IReadOnlyList<NodeInfo> references)
			{
				NodeState prevState = State;
				if (!prevState.IsStandalone())
				{
					NodeState nextState = NodeState.Exported(blob, export, references);
					while (!prevState.IsStandalone() && !TryUpdate(prevState, nextState))
					{
						prevState = State;
					}
				}
			}

			public void SetStandalone(ReadOnlySequence<byte> data, IReadOnlyList<NodeInfo> references)
			{
				State = NodeState.Standalone(data, references);
			}

			private bool TryUpdate(NodeState prevState, NodeState nextState)
			{
				return Interlocked.CompareExchange(ref _state, nextState, prevState) == prevState;
			}
		}

		/// <summary>
		/// State of each node. Each instance of this class is immutable, in order to allow atomic state transitions on each <see cref="NodeInfo"/> object.
		/// 
		/// Nodes in memory may be in the following states.
		/// 
		/// * Imported
		///   Node is known to be in located a particular hashed blob in storage, but we haven't yet read the blob index to figure out exactly where.
		///   
		/// * Exported
		///   Node is known to be located within a particular blob in storage, and the <see cref="NodeState.Export"/> member identifies specifically where.
		///   
		/// * Standalone
		///   Node exists in memory, but has not been written to storage yet.
		/// 
		/// </summary>
		class NodeState
		{
			public BlobInfo? Blob { get; }
			public BundleExport? Export { get; }
			public ReadOnlySequence<byte> Data { get; }
			public IReadOnlyList<NodeInfo>? References { get; }

			private NodeState(BlobInfo? blob, BundleExport? export, ReadOnlySequence<byte> data, IReadOnlyList<NodeInfo>? references)
			{
				Blob = blob;
				Export = export;
				Data = data;
				References = references;
			}

			public bool IsImport() => References == null;
			public bool IsExport() => Export != null;
			public bool IsStandalone() => Blob == null;

			public static NodeState Imported(BlobInfo blob)
			{
				return new NodeState(blob, null, ReadOnlySequence<byte>.Empty, null);
			}

			public static NodeState Exported(BlobInfo blob, BundleExport export, IReadOnlyList<NodeInfo> references)
			{
				return new NodeState(blob, export, ReadOnlySequence<byte>.Empty, references);
			}

			public static NodeState Standalone(ReadOnlySequence<byte> data, IReadOnlyList<NodeInfo> references)
			{
				return new NodeState(null, null, data, references);
			}
		}

		/// <summary>
		/// Information about a blob
		/// </summary>
		[DebuggerDisplay("{Hash}")]
		class BlobInfo
		{
			public IoHash Hash { get; }
			public int TotalCost { get; }

			public bool Mounted { get; set; }
			public List<NodeInfo>? LiveNodes { get; set; }

			public BlobInfo(IoHash hash, int totalCost)
			{
				Hash = hash;
				TotalCost = totalCost;
			}
		}

		readonly IStorageClient _storageClient;
		readonly NamespaceId _namespaceId;

		/// <summary>
		/// Reference to the root node in the bundle
		/// </summary>
		public BundleNode Root { get; private set; }

		readonly ConcurrentDictionary<IoHash, NodeInfo> _hashToNode = new ConcurrentDictionary<IoHash, NodeInfo>();
		readonly ConcurrentDictionary<IoHash, BlobInfo> _hashToBlob = new ConcurrentDictionary<IoHash, BlobInfo>();
		readonly IMemoryCache _cache;

		readonly CbWriter _sharedWriter = new CbWriter();
		byte[] _sharedBlobBuffer = Array.Empty<byte>();

		readonly object _lockObject = new object();

		readonly SemaphoreSlim _readSema;

		// Active read tasks at any moment. If a BundleObject is not available in the cache, we start a read and add an entry to this dictionary
		// so that other threads can also await it.
		readonly Dictionary<IoHash, Task<BundleObject>> _readTasks = new Dictionary<IoHash, Task<BundleObject>>();

		/// <summary>
		/// Options for the bundle
		/// </summary>
		public BundleOptions Options { get; }

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
		protected Bundle(IStorageClient storageClient, NamespaceId namespaceId, BundleNode root, BundleOptions options, IMemoryCache cache)
		{
			_storageClient = storageClient;
			_namespaceId = namespaceId;
			Root = root;
			Options = options;
			_cache = cache;
			_readSema = new SemaphoreSlim(options.MaxConcurrentReads);
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="storageClient">Client interface for the storage backend</param>
		/// <param name="namespaceId">Namespace for storing blobs in the storage system</param>
		/// <param name="rootObject">Object containing the root node for this bundle</param>
		/// <param name="rootNode">Reference to the root node for the bundle</param>
		/// <param name="options">Options for controlling how things are serialized</param>
		/// <param name="cache">Cache for storing decompressed objects, serialized blobs.</param>
		protected Bundle(IStorageClient storageClient, NamespaceId namespaceId, BundleObject rootObject, BundleNode rootNode, BundleOptions options, IMemoryCache cache)
			: this(storageClient, namespaceId, rootNode, options, cache)
		{
			RegisterRootObject(rootObject);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_readSema.Dispose();
		}

		/// <summary>
		/// Creates a new typed bundle with a default-constructed root node
		/// </summary>
		/// <typeparam name="T">The node type</typeparam>
		/// <param name="storageClient">Client interface for the storage backend</param>
		/// <param name="namespaceId">Namespace for storing blobs in the storage system</param>
		/// <param name="options">Options for controlling how things are serialized</param>
		/// <param name="cache">Cache for storing decompressed objects, serialized blobs.</param>
		/// <returns>New bundle instance</returns>
		public static Bundle<T> Create<T>(IStorageClient storageClient, NamespaceId namespaceId, BundleOptions options, IMemoryCache cache) where T : BundleNode, new()
		{
			return Create(storageClient, namespaceId, new T(), options, cache);
		}

		/// <summary>
		/// Creates a new typed bundle with a specific root node
		/// </summary>
		/// <typeparam name="T">The node type</typeparam>
		/// <param name="storageClient">Client interface for the storage backend</param>
		/// <param name="namespaceId">Namespace for storing blobs in the storage system</param>
		/// <param name="root">Reference to the root node for the bundle</param>
		/// <param name="options">Options for controlling how things are serialized</param>
		/// <param name="cache">Cache for storing decompressed objects, serialized blobs.</param>
		/// <returns>New bundle instance</returns>
		public static Bundle<T> Create<T>(IStorageClient storageClient, NamespaceId namespaceId, T root, BundleOptions options, IMemoryCache cache) where T : BundleNode
		{
			return new Bundle<T>(storageClient, namespaceId, root, options, cache);
		}

		/// <summary>
		/// Reads a bundle from storage
		/// </summary>
		public static async ValueTask<Bundle<T>> ReadAsync<T>(IStorageClient storageClient, NamespaceId namespaceId, BucketId bucketId, RefId refId, BundleOptions options, IMemoryCache cache) where T : BundleNode
		{
			Bundle<T> newBundle = new Bundle<T>(storageClient, namespaceId, null!, options, cache);

			BundleRoot rootObject = await storageClient.GetRefAsync<BundleRoot>(namespaceId, bucketId, refId);
			newBundle.RegisterRootObject(rootObject.Object);

			IoHash rootNodeHash = rootObject.Object.Exports[^1].Hash;
			ReadOnlySequence<byte> rootNodeData = await newBundle.GetDataAsync(rootNodeHash);
			((Bundle)newBundle).Root = BundleNode.Deserialize<T>(rootNodeData);

			return newBundle;
		}

		/// <summary>
		/// Flush a subset of nodes in the working set.
		/// </summary>
		private void SerializeWorkingSet(int ignoreCount)
		{
			// Find all the nodes that need to be flushed, in order
			List<BundleNodeRef> nodeRefs = new List<BundleNodeRef>();
			LinkWorkingSet(Root, nodeRefs);

			// Find the working set of mutable nodes to retain in memory
			HashSet<BundleNodeRef> workingSet = new HashSet<BundleNodeRef>(nodeRefs.Where(x => !x.Node!.IsReadOnly()).OrderByDescending(x => x._lastModifiedTime).Take(ignoreCount));
			nodeRefs.RemoveAll(x => workingSet.Contains(x));
			nodeRefs.SortBy(x => x._lastModifiedTime);

			// Write them all to storage
			foreach (BundleNodeRef nodeRef in nodeRefs)
			{
				BundleNode? node = nodeRef.Node;
				Debug.Assert(node != null);

				NodeInfo nodeInfo = WriteNode(node);
				nodeRef.MarkAsClean(nodeInfo.Hash);
			}
		}

		/// <summary>
		/// Traverse a tree of <see cref="BundleNodeRef"/> objects and update the incoming reference to each node
		/// </summary>
		/// <param name="node">The reference to update</param>
		/// <param name="nodeRefs">List to accumulate the list of all modified references</param>
		private void LinkWorkingSet(BundleNode node, List<BundleNodeRef> nodeRefs)
		{
			foreach (BundleNodeRef childRef in node.GetReferences())
			{
				if (childRef.Node != null && childRef._lastModifiedTime != 0)
				{
					LinkWorkingSet(childRef.Node, nodeRefs);
					if (node.IncomingRef != null)
					{
						node.IncomingRef._lastModifiedTime = Math.Max(node.IncomingRef._lastModifiedTime, childRef._lastModifiedTime + 1);
					}
					nodeRefs.Add(childRef);
				}
			}
		}

		/// <summary>
		/// Perform an incremental flush of the tree
		/// </summary>
		public Task TrimAsync() => TrimAsync(Options.TrimIgnoreCount);

		/// <summary>
		/// Perform an incremental flush of the tree
		/// </summary>
		public async Task TrimAsync(int ignoreCount)
		{
			// Serialize nodes currently in the working set
			SerializeWorkingSet(ignoreCount);

			// Find all the nodes that can be written, in order
			List<NodeInfo> writeNodes = new List<NodeInfo>();
			FindNodesToWrite(Root, writeNodes, new HashSet<NodeInfo>());

			// Write them all to storage
			int minNodeIdx = 0;
			int nextBlobCost = 0;
			for (int idx = 0; idx < writeNodes.Count; idx++)
			{
				NodeInfo node = writeNodes[idx];
				if (idx > minNodeIdx && nextBlobCost + node.Length > Options.MaxBlobSize)
				{
					await WriteObjectAsync(writeNodes.Slice(minNodeIdx, idx - minNodeIdx));
					minNodeIdx = idx;
					nextBlobCost = 0;
				}
				nextBlobCost += node.Length;
			}
		}

		void FindNodesToWrite(BundleNode node, List<NodeInfo> writeNodes, HashSet<NodeInfo> writeNodesSet)
		{
			foreach (BundleNodeRef childRef in node.GetReferences())
			{
				if (childRef.Node != null)
				{
					FindNodesToWrite(childRef.Node, writeNodes, writeNodesSet);
				}
				else
				{
					FindNodeInfosToWrite(_hashToNode[childRef.Hash], writeNodes, writeNodesSet);
				}
			}
		}

		void FindNodeInfosToWrite(NodeInfo node, List<NodeInfo> writeNodes, HashSet<NodeInfo> writeNodesSet)
		{
			if (node.State.Blob == null && writeNodesSet.Add(node))
			{
				foreach (NodeInfo childNode in node.State.References!)
				{
					FindNodeInfosToWrite(childNode, writeNodes, writeNodesSet);
				}
				writeNodes.Add(node);
			}
		}

		/// <summary>
		/// Gets the node referenced by a <see cref="BundleNodeRef{T}"/>, reading it from storage and deserializing it if necessary.
		/// </summary>
		/// <typeparam name="T">Type of node to return</typeparam>
		/// <param name="nodeRef">The reference to get</param>
		/// <returns>The deserialized node</returns>
		public async ValueTask<T> GetAsync<T>(BundleNodeRef<T> nodeRef) where T : BundleNode
		{
			if (!nodeRef.MakeStrongRef())
			{
				ReadOnlySequence<byte> data = await GetDataAsync(nodeRef.Hash);
				nodeRef.Node = BundleNode.Deserialize<T>(data);
				nodeRef.Node.IncomingRef = nodeRef;
			}
			return nodeRef.Node!;
		}

		/// <summary>
		/// Gets data for the node with the given hash
		/// </summary>
		/// <param name="hash">Hash of the node to return data for</param>
		/// <returns>The node data</returns>
		internal ValueTask<ReadOnlySequence<byte>> GetDataAsync(IoHash hash)
		{
			return GetDataAsync(_hashToNode[hash]);
		}

		/// <summary>
		/// Gets the data for a given node
		/// </summary>
		/// <param name="node">The node to get the data for</param>
		/// <returns>The node data</returns>
		async ValueTask<ReadOnlySequence<byte>> GetDataAsync(NodeInfo node)
		{
			NodeState nodeState = node.State;
			if (nodeState.Blob == null)
			{
				return nodeState.Data;
			}
			else
			{
				if (!nodeState.IsExport())
				{
					await MountBlobAsync(nodeState.Blob);
					nodeState = node.State;
				}

				Debug.Assert(nodeState.Blob != null);
				Debug.Assert(nodeState.Export != null);

				BundleObject bundleObject = await GetObjectAsync(nodeState.Blob.Hash);
				ReadOnlyMemory<byte> packetData = DecodePacket(nodeState.Blob.Hash, bundleObject.Data, nodeState.Export.Packet);
				ReadOnlyMemory<byte> nodeData = packetData.Slice(nodeState.Export.Offset, nodeState.Export.Length);

				return new ReadOnlySequence<byte>(nodeData);
			}
		}

		NodeInfo WriteNode(BundleNode node)
		{
			ReadOnlySequence<byte> data = node.Serialize();

			List<IoHash> references = new List<IoHash>();
			foreach (BundleNodeRef reference in node.GetReferences())
			{
				Debug.Assert(reference.Hash != IoHash.Zero);
				references.Add(reference.Hash);
			}

			return WriteNode(data, references);
		}

		/// <summary>
		/// Write a node's data
		/// </summary>
		/// <param name="data">The node data</param>
		/// <param name="references">Hashes for referenced nodes</param>
		/// <returns>Hash of the data</returns>
		NodeInfo WriteNode(ReadOnlySequence<byte> data, IEnumerable<IoHash> references)
		{
			IoHash hash = IoHash.Compute(data);
			NodeInfo[] nodeReferences = references.Select(x => _hashToNode[x]).Distinct().OrderBy(x => x.Hash).ToArray();

			NodeState state = NodeState.Standalone(data, nodeReferences);

			NodeInfo? node;
			if (_hashToNode.TryGetValue(hash, out node))
			{
				node.State = state;
			}
			else
			{
				int rank = 0;
				foreach (NodeInfo reference in nodeReferences)
				{
					rank = Math.Max(rank, reference.Rank + 1);
				}

				node = new NodeInfo(hash, rank, (int)data.Length, state);
				_hashToNode[hash] = node;

				_nextStats.NewNodeCount++;
				_nextStats.NewNodeBytes += data.Length;
			}
			return node;
		}

		async ValueTask MountBlobAsync(BlobInfo blob)
		{
			if(!blob.Mounted)
			{
				BundleObject bundleObject = await GetObjectAsync(blob.Hash);
				lock (_lockObject)
				{
					if (!blob.Mounted)
					{
						RegisterBlobObject(bundleObject, blob);
					}
				}
			}
		}

		/// <summary>
		/// Reads an object data from the store
		/// </summary>
		/// <param name="hash">Hash of the object</param>
		/// <returns>The parsed object</returns>
		async ValueTask<BundleObject> GetObjectAsync(IoHash hash)
		{
			string cacheKey = $"object:{hash}";

			BundleObject? bundleObject;
			if (!_cache.TryGetValue<BundleObject>(cacheKey, out bundleObject))
			{
				Task<BundleObject>? readTask;
				lock (_lockObject)
				{
					if (!_readTasks.TryGetValue(hash, out readTask))
					{
						readTask = Task.Run(() => ReadObjectAsync(cacheKey, hash));
						_readTasks.Add(hash, readTask);
					}
				}
				bundleObject = await readTask;
			}
			return bundleObject;
		}

		async Task<BundleObject> ReadObjectAsync(string cacheKey, IoHash hash)
		{
			// Perform another (sequenced) check whether an object has been added to the cache, to counteract the race between a read task being added and a task completing.
			lock (_lockObject)
			{
				if (_cache.TryGetValue<BundleObject>(cacheKey, out BundleObject? cachedObject))
				{
					return cachedObject;
				}
			}

			// Wait for the read semaphore to avoid triggering too many operations at once.
			await _readSema.WaitAsync();

			// Read the data from storage
			ReadOnlyMemory<byte> data;
			try
			{
				data = await _storageClient.ReadBlobToMemoryAsync(_namespaceId, hash);
			}
			finally
			{
				_readSema.Release();
			}

			// Add the object to the cache
			BundleObject bundleObject = CbSerializer.Deserialize<BundleObject>(data);
			using (ICacheEntry entry = _cache.CreateEntry(cacheKey))
			{
				entry.SetSize(data.Length);
				entry.SetValue(bundleObject);
			}

			// Remove this object from the list of read tasks
			lock (_lockObject)
			{
				_readTasks.Remove(hash);
			}
			return bundleObject;
		}

		/// <summary>
		/// Gets a decoded block from the store
		/// </summary>
		/// <param name="blobHash">Key for the blob</param>
		/// <param name="data">Raw blob data</param>
		/// <param name="packet">The decoded block location and size</param>
		/// <returns>The decoded data</returns>
		ReadOnlyMemory<byte> DecodePacket(IoHash blobHash, ReadOnlyMemory<byte> data, BundleCompressionPacket packet)
		{
			string cacheKey = $"decode:{blobHash}/{packet.Offset}";
			return _cache.GetOrCreate<ReadOnlyMemory<byte>>(cacheKey, entry =>
			{
				ReadOnlyMemory<byte> encodedPacket = data.Slice(packet.Offset, packet.EncodedLength);

				byte[] decodedPacket = new byte[packet.DecodedLength];
				LZ4Codec.Decode(encodedPacket.Span, decodedPacket);

				entry.SetSize(packet.DecodedLength);
				return decodedPacket;
			});
		}

		/// <summary>
		/// Find a node or create a new one
		/// </summary>
		NodeInfo FindOrAddNodeFromImport(BlobInfo blob, BundleImport import)
		{
			NodeInfo? node;
			if (!_hashToNode.TryGetValue(import.Hash, out node))
			{
				NodeInfo newNode = new NodeInfo(import.Hash, import.Rank, import.Length, NodeState.Imported(blob));
				node = _hashToNode.GetOrAdd(newNode.Hash, newNode);
			}
			return node;
		}

		/// <summary>
		/// Creates nodes for a given root object. Data for each node will be decompressed and copied from the object.
		/// </summary>
		void RegisterRootObject(BundleObject rootObject)
		{
			// Create all the export nodes without fixing up their dependencies yet
			List<NodeInfo> nodes = new List<NodeInfo>();
			for (int idx = 0; idx < rootObject.Exports.Count;)
			{
				BundleCompressionPacket packet = rootObject.Exports[idx].Packet;

				byte[] decodedData = new byte[packet.DecodedLength];
				LZ4Codec.Decode(rootObject.Data.Slice(packet.Offset, packet.EncodedLength).Span, decodedData);

				for (; idx < rootObject.Exports.Count && rootObject.Exports[idx].Packet == packet; idx++)
				{
					BundleExport export = rootObject.Exports[idx];

					ReadOnlyMemory<byte> nodeData = decodedData.AsMemory(export.Offset, export.Length);
					if (export.Length != decodedData.Length)
					{
						nodeData = nodeData.ToArray();
					}

					NodeState state = NodeState.Standalone(new ReadOnlySequence<byte>(nodeData), Array.Empty<NodeInfo>());

					NodeInfo? node;
					for (; ; )
					{
						if (_hashToNode.TryGetValue(export.Hash, out node))
						{
							node.State = state;
							break;
						}

						NodeInfo newNode = new NodeInfo(export.Hash, export.Rank, export.Length, state);
						if (_hashToNode.TryAdd(export.Hash, newNode))
						{
							node = newNode;
							break;
						}
					}
					nodes.Add(node);
				}
			}

			// Create all the imports
			RegisterImports(rootObject.ImportObjects, nodes);

			// Fixup all the references
			for (int idx = 0; idx < rootObject.Exports.Count; idx++)
			{
				NodeInfo node = nodes[idx];
				node.State = NodeState.Standalone(node.State.Data, rootObject.Exports[idx].References.Select(x => nodes[x]).ToArray());
			}
		}

		/// <summary>
		/// Creates nodes for a given blob object.
		/// </summary>
		void RegisterBlobObject(BundleObject blobObject, BlobInfo blobInfo)
		{
			// Create the exports as imports first, since we can't resolve the reference list.
			List<NodeInfo> nodes = new List<NodeInfo>();
			foreach (BundleExport export in blobObject.Exports)
			{
				NodeInfo node = FindOrAddNodeFromImport(blobInfo, export);
				nodes.Add(node);
			}

			// Register the regular imports
			RegisterImports(blobObject.ImportObjects, nodes);

			// Go back and resolve all the exports 
			for (int idx = 0; idx < blobObject.Exports.Count; idx++)
			{
				BundleExport export = blobObject.Exports[idx];
				nodes[idx].TrySetExport(blobInfo, export, export.References.Select(x => nodes[x]).ToArray());
			}

			// Flag the blob as being mounted
			blobInfo.Mounted = true;
		}

		void RegisterImports(List<BundleImportObject> importObjects, List<NodeInfo> nodes)
		{
			foreach (BundleImportObject importObject in importObjects)
			{
				BlobInfo importBlob = _hashToBlob.GetOrAdd(importObject.Object.Hash, new BlobInfo(importObject.Object.Hash, importObject.TotalCost));
				foreach (BundleImport import in importObject.Imports)
				{
					NodeInfo node = FindOrAddNodeFromImport(importBlob, import);
					nodes.Add(node);
				}
			}
		}

		/// <summary>
		/// Persist the bundle to storage
		/// </summary>
		/// <param name="bucketId"></param>
		/// <param name="refId"></param>
		/// <param name="metadata">Metadata for the root object</param>
		/// <param name="compact"></param>
		public virtual async Task WriteAsync(BucketId bucketId, RefId refId, CbObject metadata, bool compact)
		{
			// Completely flush the entire working set into NodeInfo objects
			SerializeWorkingSet(0);

			// Serialize the root node
			NodeInfo rootNode = WriteNode(Root);

			// If we're compacting the output, see if there are any other blocks we should merge
			if (compact)
			{
				// Find the live set of objects
				List<NodeInfo> liveNodes = new List<NodeInfo>();
				await FindLiveNodesAsync(rootNode, liveNodes, new HashSet<NodeInfo>());

				// Find the live set of blobs, and the cost of nodes within them
				List<BlobInfo> liveBlobs = new List<BlobInfo>();
				foreach (NodeInfo liveNode in liveNodes)
				{
					BlobInfo? liveBlob = liveNode.State.Blob;
					if (liveBlob != null)
					{
						if (liveBlob.LiveNodes == null)
						{
							liveBlob.LiveNodes = new List<NodeInfo>();
							liveBlobs.Add(liveBlob);
						}
						liveBlob.LiveNodes.Add(liveNode);
					}
				}

				// Figure out which blobs need to be repacked
				foreach (BlobInfo liveBlob in liveBlobs)
				{
					int liveCost = liveBlob.LiveNodes.Sum(x => x.Length);
					int minLiveCost = (int)(liveBlob.TotalCost * Options.RepackRatio);

					if (liveCost < minLiveCost)
					{
						foreach (NodeInfo node in liveBlob.LiveNodes!)
						{
							ReadOnlySequence<byte> data = await GetDataAsync(node);
							node.SetStandalone(data, node.State.References!);
						}
					}
				}

				// Walk backwards through the list of live nodes and make anything that references a repacked node also standalone
				for (int idx = liveNodes.Count - 1; idx >= 0; idx--)
				{
					NodeInfo liveNode = liveNodes[idx];
					if (liveNode.State.References != null && liveNode.State.References.Any(x => x.State.Blob == null))
					{
						ReadOnlySequence<byte> data = await GetDataAsync(liveNode);
						liveNode.SetStandalone(data, liveNode.State.References!);
					}
				}

				// Clear the reference list on each blob
				foreach (BlobInfo liveBlob in liveBlobs)
				{
					liveBlob.LiveNodes = null;
				}
			}

			// Find all the modified nodes
			HashSet<NodeInfo> newNodes = new HashSet<NodeInfo>();
			FindModifiedLiveSet(rootNode, newNodes);

			// Write all the new blobs. Sort by rank and go from end to start, updating the list as we go so that we can figure out imports correctly.
			NodeInfo[] writeNodes = newNodes.Where(x => x.State.Blob == null).OrderBy(x => x.Rank).ThenBy(x => x.Hash).ToArray();

			int minIdx = 0;
			int nextBlobCost = writeNodes[0].Length;

			for (int maxIdx = 1; maxIdx < writeNodes.Length; maxIdx++)
			{
				nextBlobCost += writeNodes[maxIdx].Length;
				if (nextBlobCost > Options.MaxBlobSize)
				{
					await WriteObjectAsync(writeNodes.AsSegment(minIdx, maxIdx - minIdx));
					minIdx = maxIdx;
					nextBlobCost = writeNodes[maxIdx].Length;
				}
			}

			if (nextBlobCost > Options.MaxInlineBlobSize && minIdx + 1 < writeNodes.Length)
			{
				await WriteObjectAsync(writeNodes.AsSegment(minIdx, writeNodes.Length - 1 - minIdx));
				minIdx = writeNodes.Length - 1;
			}

			// Write the final ref
			await WriteRefAsync(writeNodes.AsSegment(minIdx), bucketId, refId, metadata);

			// Copy the stats over
			Stats = _nextStats;
			_nextStats = new BundleStats();
		}

		private void FindModifiedLiveSet(NodeInfo node, HashSet<NodeInfo> nodes)
		{
			if (nodes.Add(node) && node.Rank > 0)
			{
				foreach (NodeInfo reference in node.State.References!)
				{
					if (reference.State.Blob == null)
					{
						FindModifiedLiveSet(reference, nodes);
					}
				}
			}
		}

		async Task FindLiveNodesAsync(NodeInfo node, List<NodeInfo> liveNodes, HashSet<NodeInfo> liveNodeSet)
		{
			if (liveNodeSet.Add(node))
			{
				liveNodes.Add(node);
				if (node.Rank > 0)
				{
					if (node.State.Blob != null)
					{
						await MountBlobAsync(node.State.Blob);
					}
					foreach (NodeInfo reference in node.State.References!)
					{
						await FindLiveNodesAsync(reference, liveNodes, liveNodeSet);
					}
				}
			}
		}

		async Task WriteRefAsync(IReadOnlyList<NodeInfo> nodes, BucketId bucketId, RefId refId, CbObject metadata)
		{
			foreach (NodeInfo node in nodes)
			{
				ReadOnlySequence<byte> nodeData = await GetDataAsync(node);
				node.SetStandalone(nodeData, node.State.References!);
			}

			BundleRoot rootRef = new BundleRoot();
			rootRef.Metadata = metadata;
			rootRef.Object = await CreateObjectAsync(nodes);

			ReadOnlyMemory<byte> rootRefData = EncodeObject(rootRef);
			_nextStats.NewRefCount++;
			_nextStats.NewRefBytes += rootRefData.Length;

			await _storageClient.SetRefAsync(_namespaceId, bucketId, refId, new CbField(rootRefData));
		}

		ReadOnlyMemory<byte> EncodeObject<T>(T data)
		{
			_sharedWriter.Clear();
			CbSerializer.Serialize<T>(_sharedWriter, data);

			int size = _sharedWriter.GetSize();
			CreateFreeSpace(ref _sharedBlobBuffer, 0, Math.Max(size, Options.MaxBlobSize));

			_sharedWriter.CopyTo(_sharedBlobBuffer.AsSpan(0, size));
			return _sharedBlobBuffer.AsMemory(0, size);
		}

		async Task WriteObjectAsync(IReadOnlyList<NodeInfo> nodes)
		{
			BundleObject writeObject = await CreateObjectAsync(nodes);

			ReadOnlyMemory<byte> writeData = EncodeObject(writeObject);
			_nextStats.NewBlobCount++;
			_nextStats.NewBlobBytes += writeData.Length;

			IoHash hash = await _storageClient.WriteBlobFromMemoryAsync(_namespaceId, writeData);

			BlobInfo blob = new BlobInfo(hash, writeObject.Exports.Sum(x => x.Length));
			for (int idx = 0; idx < writeObject.Exports.Count; idx++)
			{
				NodeInfo node = nodes[idx];
				node.State = NodeState.Exported(blob, writeObject.Exports[idx], node.State.References!);
			}

			blob.Mounted = true;
		}

		/// <summary>
		/// Creates a BundleObject instance. 
		///
		/// WARNING: The <see cref="BundleObject.Data"/> member will be set to the value of <see cref="_encodedBuffer"/> on return, which will be
		/// reused on a subsequent call. If the object needs to be persisted across the lifetime of other objects, this field must be duplicated.
		/// </summary>
		async Task<BundleObject> CreateObjectAsync(IReadOnlyList<NodeInfo> nodes)
		{
			// Preallocate data in the encoded buffer to reduce fragmentation if we have to resize
			CreateFreeSpace(ref _encodedBuffer, 0, Options.MaxBlobSize);

			// Find node indices for all the export
			Dictionary<NodeInfo, int> nodeToIndex = new Dictionary<NodeInfo, int>();
			foreach (NodeInfo node in nodes)
			{
				nodeToIndex[node] = nodeToIndex.Count;
			}

			// Create the new object
			BundleObject newObject = new BundleObject();

			// Find all the imported nodes
			HashSet<NodeInfo> importedNodes = new HashSet<NodeInfo>();
			foreach (NodeInfo node in nodes)
			{
				importedNodes.UnionWith(node.State.References!);
			}
			importedNodes.ExceptWith(nodes);

			// Find all the imports
			foreach (IGrouping<BlobInfo, NodeInfo> importGroup in importedNodes.GroupBy(x => x.State.Blob!))
			{
				BlobInfo blob = importGroup.Key;

				NodeInfo[] importNodes = importGroup.ToArray();
				foreach (NodeInfo importNode in importNodes)
				{
					nodeToIndex[importNode] = nodeToIndex.Count;
				}

				List<BundleImport> imports = importGroup.Select(x => new BundleImport(x.Hash, x.Rank, x.Length)).ToList();
				BundleImportObject importObject = new BundleImportObject(new CbObjectAttachment(blob.Hash), blob.TotalCost, imports);
				newObject.ImportObjects.Add(importObject);
			}

			// Size of data currently stored in the block buffer
			int blockSize = 0;

			// Compress all the nodes into the encoded buffer
			BundleCompressionPacket packet = new BundleCompressionPacket(0);
			foreach (NodeInfo node in nodes)
			{
				ReadOnlySequence<byte> nodeData = await GetDataAsync(node);

				// If we can't fit this data into the current block, flush the contents of it first
				if (blockSize > 0 && blockSize + nodeData.Length > Options.MinCompressionPacketSize)
				{
					FlushPacket(_blockBuffer.AsMemory(0, blockSize), packet);
					packet = new BundleCompressionPacket(packet.Offset + packet.EncodedLength);
					blockSize = 0;
				}

				// Create the export for this node
				int[] references = node.State.References.Select(x => nodeToIndex[x]).ToArray();
				BundleExport export = new BundleExport(node.Hash, node.Rank, packet, blockSize, node.Length, references);
				newObject.Exports.Add(export);

				// Write out the new block
				int offset = packet.EncodedLength;
				if (nodeData.Length < Options.MinCompressionPacketSize || !nodeData.IsSingleSegment)
				{
					int requiredSize = Math.Max(blockSize + (int)nodeData.Length, (int)(Options.MaxBlobSize * 1.2));
					CreateFreeSpace(ref _blockBuffer, blockSize, requiredSize);
					foreach (ReadOnlyMemory<byte> nodeSegment in nodeData)
					{
						nodeSegment.CopyTo(_blockBuffer.AsMemory(blockSize));
						blockSize += nodeSegment.Length;
					}
				}
				else
				{
					FlushPacket(nodeData.First, packet);
					packet = new BundleCompressionPacket(packet.Offset + packet.EncodedLength);
				}
			}
			FlushPacket(_blockBuffer.AsMemory(0, blockSize), packet);

			// Flush the data
			newObject.Data = _encodedBuffer.AsMemory(0, packet.Offset + packet.EncodedLength);
			return newObject;
		}

		void FlushPacket(ReadOnlyMemory<byte> inputData, BundleCompressionPacket packet)
		{
			if (inputData.Length > 0)
			{
				int minFreeSpace = LZ4Codec.MaximumOutputSize(inputData.Length);
				CreateFreeSpace(ref _encodedBuffer, packet.Offset, packet.Offset + minFreeSpace);

				ReadOnlySpan<byte> inputSpan = inputData.Span;
				Span<byte> outputSpan = _encodedBuffer.AsSpan(packet.Offset);

				packet.DecodedLength = inputData.Length;
				packet.EncodedLength = LZ4Codec.Encode(inputSpan, outputSpan);

				Debug.Assert(packet.EncodedLength >= 0);
			}
		}

		void CreateFreeSpace(ref byte[] buffer, int usedSize, int requiredSize)
		{
			if (requiredSize > buffer.Length)
			{
				byte[] newBuffer = new byte[requiredSize];
				buffer.AsSpan(0, usedSize).CopyTo(newBuffer);
				buffer = newBuffer;
			}
		}
	}

	/// <summary>
	/// Derived version of <see cref="Bundle"/> supporting a concrete root node type
	/// </summary>
	/// <typeparam name="T">The root node type</typeparam>
	public class Bundle<T> : Bundle where T : BundleNode
	{
		/// <summary>
		/// Node at the root of this tree
		/// </summary>
		public new T Root => (T)base.Root;

		/// <summary>
		/// Constructor
		/// </summary>
		internal Bundle(IStorageClient storageClient, NamespaceId namespaceId, T root, BundleOptions options, IMemoryCache cache)
			: base(storageClient, namespaceId, root, options, cache)
		{
		}
	}
}
