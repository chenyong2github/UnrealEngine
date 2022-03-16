// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using K4os.Compression.LZ4;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;
using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
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
				get => StatePrivate;
				set => StatePrivate = value;
			}

			private NodeState StatePrivate;

			public NodeInfo(IoHash Hash, int Rank, int Length, NodeState State)
			{
				if (Length <= 0)
				{
					throw new ArgumentException("Node length must be greater than zero", nameof(Length));
				}

				this.Hash = Hash;
				this.Rank = Rank;
				this.Length = Length;
				this.StatePrivate = State;
			}

			public void TrySetExport(BlobInfo Blob, BundleExport Export, IReadOnlyList<NodeInfo> References)
			{
				NodeState PrevState = State;
				if (!PrevState.IsStandalone())
				{
					NodeState NextState = NodeState.Exported(Blob, Export, References);
					while (!PrevState.IsStandalone() && !TryUpdate(PrevState, NextState))
					{
						PrevState = State;
					}
				}
			}

			public void SetStandalone(ReadOnlySequence<byte> Data, IReadOnlyList<NodeInfo> References)
			{
				State = NodeState.Standalone(Data, References);
			}

			private bool TryUpdate(NodeState PrevState, NodeState NextState)
			{
				return Interlocked.CompareExchange(ref StatePrivate, NextState, PrevState) == PrevState;
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

			private NodeState(BlobInfo? Blob, BundleExport? Export, ReadOnlySequence<byte> Data, IReadOnlyList<NodeInfo>? References)
			{
				this.Blob = Blob;
				this.Export = Export;
				this.Data = Data;
				this.References = References;
			}

			public bool IsImport() => References == null;
			public bool IsExport() => Export != null;
			public bool IsStandalone() => Blob == null;

			public static NodeState Imported(BlobInfo Blob)
			{
				return new NodeState(Blob, null, ReadOnlySequence<byte>.Empty, null);
			}

			public static NodeState Exported(BlobInfo Blob, BundleExport Export, IReadOnlyList<NodeInfo> References)
			{
				return new NodeState(Blob, Export, ReadOnlySequence<byte>.Empty, References);
			}

			public static NodeState Standalone(ReadOnlySequence<byte> Data, IReadOnlyList<NodeInfo> References)
			{
				return new NodeState(null, null, Data, References);
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

			public BlobInfo(IoHash Hash, int TotalCost)
			{
				this.Hash = Hash;
				this.TotalCost = TotalCost;
			}
		}

		readonly IStorageClient StorageClient;
		readonly NamespaceId NamespaceId;

		/// <summary>
		/// Reference to the root node in the bundle
		/// </summary>
		public BundleNode Root { get; private set; }

		ConcurrentDictionary<IoHash, NodeInfo> HashToNode = new ConcurrentDictionary<IoHash, NodeInfo>();
		ConcurrentDictionary<IoHash, BlobInfo> HashToBlob = new ConcurrentDictionary<IoHash, BlobInfo>();
		IMemoryCache Cache;

		CbWriter SharedWriter = new CbWriter();
		byte[] SharedBlobBuffer = Array.Empty<byte>();

		object LockObject = new object();

		SemaphoreSlim ReadSema;

		// Active read tasks at any moment. If a BundleObject is not available in the cache, we start a read and add an entry to this dictionary
		// so that other threads can also await it.
		Dictionary<IoHash, Task<BundleObject>> ReadTasks = new Dictionary<IoHash, Task<BundleObject>>();

		/// <summary>
		/// Options for the bundle
		/// </summary>
		public BundleOptions Options { get; }

		/// <summary>
		/// Tracks statistics for the size of written data
		/// </summary>
		public BundleStats Stats { get; private set; } = new BundleStats();

		BundleStats NextStats = new BundleStats();
		byte[] BlockBuffer = Array.Empty<byte>();
		byte[] EncodedBuffer = Array.Empty<byte>();

		/// <summary>
		/// Constructor
		/// </summary>
		protected Bundle(IStorageClient StorageClient, NamespaceId NamespaceId, BundleNode Root, BundleOptions Options, IMemoryCache Cache)
		{
			this.StorageClient = StorageClient;
			this.NamespaceId = NamespaceId;
			this.Root = Root;
			this.Options = Options;
			this.Cache = Cache;
			this.ReadSema = new SemaphoreSlim(Options.MaxConcurrentReads);
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StorageClient">Client interface for the storage backend</param>
		/// <param name="NamespaceId">Namespace for storing blobs in the storage system</param>
		/// <param name="Object">Object containing the root node for this bundle</param>
		/// <param name="Root">Reference to the root node for the bundle</param>
		/// <param name="Options">Options for controlling how things are serialized</param>
		/// <param name="Cache">Cache for storing decompressed objects, serialized blobs.</param>
		protected Bundle(IStorageClient StorageClient, NamespaceId NamespaceId, BundleObject Object, BundleNode Root, BundleOptions Options, IMemoryCache Cache)
			: this(StorageClient, NamespaceId, Root, Options, Cache)
		{
			RegisterRootObject(Object);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			ReadSema.Dispose();
		}

		/// <summary>
		/// Creates a new typed bundle with a default-constructed root node
		/// </summary>
		/// <typeparam name="T">The node type</typeparam>
		/// <param name="StorageClient">Client interface for the storage backend</param>
		/// <param name="NamespaceId">Namespace for storing blobs in the storage system</param>
		/// <param name="Options">Options for controlling how things are serialized</param>
		/// <param name="Cache">Cache for storing decompressed objects, serialized blobs.</param>
		/// <returns>New bundle instance</returns>
		public static Bundle<T> Create<T>(IStorageClient StorageClient, NamespaceId NamespaceId, BundleOptions Options, IMemoryCache Cache) where T : BundleNode, new()
		{
			return Create(StorageClient, NamespaceId, new T(), Options, Cache);
		}

		/// <summary>
		/// Creates a new typed bundle with a specific root node
		/// </summary>
		/// <typeparam name="T">The node type</typeparam>
		/// <param name="StorageClient">Client interface for the storage backend</param>
		/// <param name="NamespaceId">Namespace for storing blobs in the storage system</param>
		/// <param name="Root">Reference to the root node for the bundle</param>
		/// <param name="Options">Options for controlling how things are serialized</param>
		/// <param name="Cache">Cache for storing decompressed objects, serialized blobs.</param>
		/// <returns>New bundle instance</returns>
		public static Bundle<T> Create<T>(IStorageClient StorageClient, NamespaceId NamespaceId, T Root, BundleOptions Options, IMemoryCache Cache) where T : BundleNode
		{
			return new Bundle<T>(StorageClient, NamespaceId, Root, Options, Cache);
		}

		/// <summary>
		/// Reads a bundle from storage
		/// </summary>
		public static async ValueTask<Bundle<T>> ReadAsync<T>(IStorageClient StorageClient, NamespaceId NamespaceId, BucketId BucketId, RefId RefId, BundleOptions Options, IMemoryCache Cache) where T : BundleNode
		{
			Bundle<T> NewBundle = new Bundle<T>(StorageClient, NamespaceId, null!, Options, Cache);

			BundleRoot RootObject = await StorageClient.GetRefAsync<BundleRoot>(NamespaceId, BucketId, RefId);
			NewBundle.RegisterRootObject(RootObject.Object);

			IoHash RootNodeHash = RootObject.Object.Exports[^1].Hash;
			ReadOnlySequence<byte> RootNodeData = await NewBundle.GetDataAsync(RootNodeHash);
			((Bundle)NewBundle).Root = BundleNode.Deserialize<T>(RootNodeData);

			return NewBundle;
		}

		/// <summary>
		/// Flush a subset of nodes in the working set.
		/// </summary>
		private void SerializeWorkingSet(int IgnoreCount)
		{
			// Find all the nodes that need to be flushed, in order
			List<BundleNodeRef> NodeRefs = new List<BundleNodeRef>();
			LinkWorkingSet(Root, NodeRefs);

			// Find the working set of mutable nodes to retain in memory
			HashSet<BundleNodeRef> WorkingSet = new HashSet<BundleNodeRef>(NodeRefs.Where(x => !x.Node!.IsReadOnly()).OrderByDescending(x => x.LastModifiedTime).Take(IgnoreCount));
			NodeRefs.RemoveAll(x => WorkingSet.Contains(x));
			NodeRefs.SortBy(x => x.LastModifiedTime);

			// Write them all to storage
			foreach (BundleNodeRef NodeRef in NodeRefs)
			{
				BundleNode? Node = NodeRef.Node;
				Debug.Assert(Node != null);

				NodeInfo NodeInfo = WriteNode(Node);
				NodeRef.MarkAsClean(NodeInfo.Hash);
			}
		}

		/// <summary>
		/// Traverse a tree of <see cref="BundleNodeRef"/> objects and update the incoming reference to each node
		/// </summary>
		/// <param name="Node">The reference to update</param>
		/// <param name="NodeRefs">List to accumulate the list of all modified references</param>
		private void LinkWorkingSet(BundleNode Node, List<BundleNodeRef> NodeRefs)
		{
			foreach (BundleNodeRef ChildRef in Node.GetReferences())
			{
				if (ChildRef.Node != null && ChildRef.LastModifiedTime != 0)
				{
					LinkWorkingSet(ChildRef.Node, NodeRefs);
					if (Node.IncomingRef != null)
					{
						Node.IncomingRef.LastModifiedTime = Math.Max(Node.IncomingRef.LastModifiedTime, ChildRef.LastModifiedTime + 1);
					}
					NodeRefs.Add(ChildRef);
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
		public async Task TrimAsync(int IgnoreCount)
		{
			// Serialize nodes currently in the working set
			SerializeWorkingSet(IgnoreCount);

			// Find all the nodes that can be written, in order
			List<NodeInfo> WriteNodes = new List<NodeInfo>();
			FindNodesToWrite(Root, WriteNodes, new HashSet<NodeInfo>());

			// Write them all to storage
			int MinNodeIdx = 0;
			int NextBlobCost = 0;
			for (int Idx = 0; Idx < WriteNodes.Count; Idx++)
			{
				NodeInfo Node = WriteNodes[Idx];
				if (Idx > MinNodeIdx && NextBlobCost + Node.Length > Options.MaxBlobSize)
				{
					await WriteObjectAsync(WriteNodes.Slice(MinNodeIdx, Idx - MinNodeIdx));
					MinNodeIdx = Idx;
					NextBlobCost = 0;
				}
				NextBlobCost += Node.Length;
			}
		}

		void FindNodesToWrite(BundleNode Node, List<NodeInfo> WriteNodes, HashSet<NodeInfo> WriteNodesSet)
		{
			foreach (BundleNodeRef ChildRef in Node.GetReferences())
			{
				if (ChildRef.Node != null)
				{
					FindNodesToWrite(ChildRef.Node, WriteNodes, WriteNodesSet);
				}
				else
				{
					FindNodeInfosToWrite(HashToNode[ChildRef.Hash], WriteNodes, WriteNodesSet);
				}
			}
		}

		void FindNodeInfosToWrite(NodeInfo Node, List<NodeInfo> WriteNodes, HashSet<NodeInfo> WriteNodesSet)
		{
			if (Node.State.Blob == null && WriteNodesSet.Add(Node))
			{
				foreach (NodeInfo ChildNode in Node.State.References!)
				{
					FindNodeInfosToWrite(ChildNode, WriteNodes, WriteNodesSet);
				}
				WriteNodes.Add(Node);
			}
		}

		/// <summary>
		/// Gets the node referenced by a <see cref="BundleNodeRef{T}"/>, reading it from storage and deserializing it if necessary.
		/// </summary>
		/// <typeparam name="T">Type of node to return</typeparam>
		/// <param name="Ref">The reference to get</param>
		/// <returns>The deserialized node</returns>
		public async ValueTask<T> GetAsync<T>(BundleNodeRef<T> Ref) where T : BundleNode
		{
			if (!Ref.MakeStrongRef())
			{
				ReadOnlySequence<byte> Data = await GetDataAsync(Ref.Hash);
				Ref.Node = BundleNode.Deserialize<T>(Data);
				Ref.Node.IncomingRef = Ref;
			}
			return Ref.Node!;
		}

		/// <summary>
		/// Gets data for the node with the given hash
		/// </summary>
		/// <param name="Hash">Hash of the node to return data for</param>
		/// <returns>The node data</returns>
		internal ValueTask<ReadOnlySequence<byte>> GetDataAsync(IoHash Hash)
		{
			return GetDataAsync(HashToNode[Hash]);
		}

		/// <summary>
		/// Gets the data for a given node
		/// </summary>
		/// <param name="Node">The node to get the data for</param>
		/// <returns>The node data</returns>
		async ValueTask<ReadOnlySequence<byte>> GetDataAsync(NodeInfo Node)
		{
			NodeState NodeState = Node.State;
			if (NodeState.Blob == null)
			{
				return NodeState.Data;
			}
			else
			{
				if (!NodeState.IsExport())
				{
					await MountBlobAsync(NodeState.Blob);
					NodeState = Node.State;
				}

				Debug.Assert(NodeState.Blob != null);
				Debug.Assert(NodeState.Export != null);

				BundleObject Object = await GetObjectAsync(NodeState.Blob.Hash);
				ReadOnlyMemory<byte> PacketData = DecodePacket(NodeState.Blob.Hash, Object.Data, NodeState.Export.Packet);
				ReadOnlyMemory<byte> NodeData = PacketData.Slice(NodeState.Export.Offset, NodeState.Export.Length);

				return new ReadOnlySequence<byte>(NodeData);
			}
		}

		NodeInfo WriteNode(BundleNode Node)
		{
			ReadOnlySequence<byte> Data = Node.Serialize();

			List<IoHash> References = new List<IoHash>();
			foreach (BundleNodeRef Reference in Node.GetReferences())
			{
				Debug.Assert(Reference.Hash != IoHash.Zero);
				References.Add(Reference.Hash);
			}

			return WriteNode(Data, References);
		}

		/// <summary>
		/// Write a node's data
		/// </summary>
		/// <param name="Data">The node data</param>
		/// <param name="References">Hashes for referenced nodes</param>
		/// <returns>Hash of the data</returns>
		NodeInfo WriteNode(ReadOnlySequence<byte> Data, IEnumerable<IoHash> References)
		{
			IoHash Hash = IoHash.Compute(Data);
			NodeInfo[] NodeReferences = References.Select(x => HashToNode[x]).Distinct().OrderBy(x => x.Hash).ToArray();

			NodeState State = NodeState.Standalone(Data, NodeReferences);

			NodeInfo? Node;
			if (HashToNode.TryGetValue(Hash, out Node))
			{
				Node.State = State;
			}
			else
			{
				int Rank = 0;
				foreach (NodeInfo Reference in NodeReferences)
				{
					Rank = Math.Max(Rank, Reference.Rank + 1);
				}

				Node = new NodeInfo(Hash, Rank, (int)Data.Length, State);
				HashToNode[Hash] = Node;

				NextStats.NewNodeCount++;
				NextStats.NewNodeBytes += Data.Length;
			}
			return Node;
		}

		async ValueTask MountBlobAsync(BlobInfo Blob)
		{
			if(!Blob.Mounted)
			{
				BundleObject Object = await GetObjectAsync(Blob.Hash);
				lock (LockObject)
				{
					if (!Blob.Mounted)
					{
						RegisterBlobObject(Object, Blob);
					}
				}
			}
		}

		/// <summary>
		/// Reads an object data from the store
		/// </summary>
		/// <param name="Hash">Hash of the object</param>
		/// <returns>The parsed object</returns>
		async ValueTask<BundleObject> GetObjectAsync(IoHash Hash)
		{
			string CacheKey = $"object:{Hash}";

			BundleObject? Object;
			if (!Cache.TryGetValue<BundleObject>(CacheKey, out Object))
			{
				Task<BundleObject>? ReadTask;
				lock (LockObject)
				{
					if (!ReadTasks.TryGetValue(Hash, out ReadTask))
					{
						ReadTask = Task.Run(() => ReadObjectAsync(CacheKey, Hash));
						ReadTasks.Add(Hash, ReadTask);
					}
				}
				Object = await ReadTask;
			}
			return Object;
		}

		async Task<BundleObject> ReadObjectAsync(string CacheKey, IoHash Hash)
		{
			// Perform another (sequenced) check whether an object has been added to the cache, to counteract the race between a read task being added and a task completing.
			lock (LockObject)
			{
				if (Cache.TryGetValue<BundleObject>(CacheKey, out BundleObject? CachedObject))
				{
					return CachedObject;
				}
			}

			// Wait for the read semaphore to avoid triggering too many operations at once.
			await ReadSema.WaitAsync();

			// Read the data from storage
			ReadOnlyMemory<byte> Data;
			try
			{
				Data = await StorageClient.ReadBlobToMemoryAsync(NamespaceId, Hash);
			}
			finally
			{
				ReadSema.Release();
			}

			// Add the object to the cache
			BundleObject Object = CbSerializer.Deserialize<BundleObject>(Data);
			using (ICacheEntry Entry = Cache.CreateEntry(CacheKey))
			{
				Entry.SetSize(Data.Length);
				Entry.SetValue(Object);
			}

			// Remove this object from the list of read tasks
			lock (LockObject)
			{
				ReadTasks.Remove(Hash);
			}
			return Object;
		}

		/// <summary>
		/// Gets a decoded block from the store
		/// </summary>
		/// <param name="BlobHash">Key for the blob</param>
		/// <param name="Data">Raw blob data</param>
		/// <param name="Packet">The decoded block location and size</param>
		/// <returns>The decoded data</returns>
		ReadOnlyMemory<byte> DecodePacket(IoHash BlobHash, ReadOnlyMemory<byte> Data, BundleCompressionPacket Packet)
		{
			string CacheKey = $"decode:{BlobHash}/{Packet.Offset}";
			return Cache.GetOrCreate<ReadOnlyMemory<byte>>(CacheKey, Entry =>
			{
				ReadOnlyMemory<byte> EncodedPacket = Data.Slice(Packet.Offset, Packet.EncodedLength);

				byte[] DecodedPacket = new byte[Packet.DecodedLength];
				LZ4Codec.Decode(EncodedPacket.Span, DecodedPacket);

				Entry.SetSize(Packet.DecodedLength);
				return DecodedPacket;
			});
		}

		/// <summary>
		/// Find a node or create a new one
		/// </summary>
		NodeInfo FindOrAddNodeFromImport(BlobInfo Blob, BundleImport Import)
		{
			NodeInfo? Node;
			if (!HashToNode.TryGetValue(Import.Hash, out Node))
			{
				NodeInfo NewNode = new NodeInfo(Import.Hash, Import.Rank, Import.Length, NodeState.Imported(Blob));
				Node = HashToNode.GetOrAdd(NewNode.Hash, NewNode);
			}
			return Node;
		}

		/// <summary>
		/// Creates nodes for a given root object. Data for each node will be decompressed and copied from the object.
		/// </summary>
		void RegisterRootObject(BundleObject Object)
		{
			// Create all the export nodes without fixing up their dependencies yet
			List<NodeInfo> Nodes = new List<NodeInfo>();
			for (int Idx = 0; Idx < Object.Exports.Count;)
			{
				BundleCompressionPacket Packet = Object.Exports[Idx].Packet;

				byte[] DecodedData = new byte[Packet.DecodedLength];
				LZ4Codec.Decode(Object.Data.Slice(Packet.Offset, Packet.EncodedLength).Span, DecodedData);

				for (; Idx < Object.Exports.Count && Object.Exports[Idx].Packet == Packet; Idx++)
				{
					BundleExport Export = Object.Exports[Idx];

					ReadOnlyMemory<byte> NodeData = DecodedData.AsMemory(Export.Offset, Export.Length);
					if (Export.Length != DecodedData.Length)
					{
						NodeData = NodeData.ToArray();
					}

					NodeState State = NodeState.Standalone(new ReadOnlySequence<byte>(NodeData), Array.Empty<NodeInfo>());

					NodeInfo? Node;
					for (; ; )
					{
						if (HashToNode.TryGetValue(Export.Hash, out Node))
						{
							Node.State = State;
							break;
						}

						NodeInfo NewNode = new NodeInfo(Export.Hash, Export.Rank, Export.Length, State);
						if (HashToNode.TryAdd(Export.Hash, NewNode))
						{
							Node = NewNode;
							break;
						}
					}
					Nodes.Add(Node);
				}
			}

			// Create all the imports
			RegisterImports(Object.ImportObjects, Nodes);

			// Fixup all the references
			for (int Idx = 0; Idx < Object.Exports.Count; Idx++)
			{
				NodeInfo Node = Nodes[Idx];
				Node.State = NodeState.Standalone(Node.State.Data, Object.Exports[Idx].References.Select(x => Nodes[x]).ToArray());
			}
		}

		/// <summary>
		/// Creates nodes for a given blob object.
		/// </summary>
		void RegisterBlobObject(BundleObject Object, BlobInfo Blob)
		{
			// Create the exports as imports first, since we can't resolve the reference list.
			List<NodeInfo> Nodes = new List<NodeInfo>();
			foreach (BundleExport Export in Object.Exports)
			{
				NodeInfo Node = FindOrAddNodeFromImport(Blob, Export);
				Nodes.Add(Node);
			}

			// Register the regular imports
			RegisterImports(Object.ImportObjects, Nodes);

			// Go back and resolve all the exports 
			for (int Idx = 0; Idx < Object.Exports.Count; Idx++)
			{
				BundleExport Export = Object.Exports[Idx];
				Nodes[Idx].TrySetExport(Blob, Export, Export.References.Select(x => Nodes[x]).ToArray());
			}

			// Flag the blob as being mounted
			Blob.Mounted = true;
		}

		void RegisterImports(List<BundleImportObject> ImportObjects, List<NodeInfo> Nodes)
		{
			foreach (BundleImportObject ImportObject in ImportObjects)
			{
				BlobInfo ImportBlob = HashToBlob.GetOrAdd(ImportObject.Object.Hash, new BlobInfo(ImportObject.Object.Hash, ImportObject.TotalCost));
				foreach (BundleImport Import in ImportObject.Imports)
				{
					NodeInfo Node = FindOrAddNodeFromImport(ImportBlob, Import);
					Nodes.Add(Node);
				}
			}
		}

		/// <summary>
		/// Persist the bundle to storage
		/// </summary>
		/// <param name="BucketId"></param>
		/// <param name="RefId"></param>
		/// <param name="Metadata">Metadata for the root object</param>
		/// <param name="Compact"></param>
		public virtual async Task WriteAsync(BucketId BucketId, RefId RefId, CbObject Metadata, bool Compact)
		{
			// Completely flush the entire working set into NodeInfo objects
			SerializeWorkingSet(0);

			// Serialize the root node
			NodeInfo RootNode = WriteNode(Root);

			// If we're compacting the output, see if there are any other blocks we should merge
			if (Compact)
			{
				// Find the live set of objects
				List<NodeInfo> LiveNodes = new List<NodeInfo>();
				await FindLiveNodesAsync(RootNode, LiveNodes, new HashSet<NodeInfo>());

				// Find the live set of blobs, and the cost of nodes within them
				List<BlobInfo> LiveBlobs = new List<BlobInfo>();
				foreach (NodeInfo LiveNode in LiveNodes)
				{
					BlobInfo? LiveBlob = LiveNode.State.Blob;
					if (LiveBlob != null)
					{
						if (LiveBlob.LiveNodes == null)
						{
							LiveBlob.LiveNodes = new List<NodeInfo>();
							LiveBlobs.Add(LiveBlob);
						}
						LiveBlob.LiveNodes.Add(LiveNode);
					}
				}

				// Figure out which blobs need to be repacked
				foreach (BlobInfo LiveBlob in LiveBlobs)
				{
					int LiveCost = LiveBlob.LiveNodes.Sum(x => x.Length);
					int MinLiveCost = (int)(LiveBlob.TotalCost * Options.RepackRatio);

					if (LiveCost < MinLiveCost)
					{
						foreach (NodeInfo Node in LiveBlob.LiveNodes!)
						{
							ReadOnlySequence<byte> Data = await GetDataAsync(Node);
							Node.SetStandalone(Data, Node.State.References!);
						}
					}
				}

				// Walk backwards through the list of live nodes and make anything that references a repacked node also standalone
				for (int Idx = LiveNodes.Count - 1; Idx >= 0; Idx--)
				{
					NodeInfo LiveNode = LiveNodes[Idx];
					if (LiveNode.State.References != null && LiveNode.State.References.Any(x => x.State.Blob == null))
					{
						ReadOnlySequence<byte> Data = await GetDataAsync(LiveNode);
						LiveNode.SetStandalone(Data, LiveNode.State.References!);
					}
				}

				// Clear the reference list on each blob
				foreach (BlobInfo LiveBlob in LiveBlobs)
				{
					LiveBlob.LiveNodes = null;
				}
			}

			// Find all the modified nodes
			HashSet<NodeInfo> NewNodes = new HashSet<NodeInfo>();
			FindModifiedLiveSet(RootNode, NewNodes);

			// Write all the new blobs. Sort by rank and go from end to start, updating the list as we go so that we can figure out imports correctly.
			NodeInfo[] WriteNodes = NewNodes.Where(x => x.State.Blob == null).OrderBy(x => x.Rank).ThenBy(x => x.Hash).ToArray();

			int MinIdx = 0;
			int NextBlobCost = WriteNodes[0].Length;

			for (int MaxIdx = 1; MaxIdx < WriteNodes.Length; MaxIdx++)
			{
				NextBlobCost += WriteNodes[MaxIdx].Length;
				if (NextBlobCost > Options.MaxBlobSize)
				{
					await WriteObjectAsync(WriteNodes.Slice(MinIdx, MaxIdx - MinIdx));
					MinIdx = MaxIdx;
					NextBlobCost = WriteNodes[MaxIdx].Length;
				}
			}

			if (NextBlobCost > Options.MaxInlineBlobSize && MinIdx + 1 < WriteNodes.Length)
			{
				await WriteObjectAsync(WriteNodes.Slice(MinIdx, WriteNodes.Length - 1 - MinIdx));
				MinIdx = WriteNodes.Length - 1;
			}

			// Write the final ref
			await WriteRefAsync(WriteNodes.Slice(MinIdx), BucketId, RefId, Metadata);

			// Copy the stats over
			Stats = NextStats;
			NextStats = new BundleStats();
		}

		private void FindModifiedLiveSet(NodeInfo Node, HashSet<NodeInfo> Nodes)
		{
			if (Nodes.Add(Node) && Node.Rank > 0)
			{
				foreach (NodeInfo Reference in Node.State.References!)
				{
					if (Reference.State.Blob == null)
					{
						FindModifiedLiveSet(Reference, Nodes);
					}
				}
			}
		}

		async Task FindLiveNodesAsync(NodeInfo Node, List<NodeInfo> LiveNodes, HashSet<NodeInfo> LiveNodeSet)
		{
			if (LiveNodeSet.Add(Node))
			{
				LiveNodes.Add(Node);
				if (Node.Rank > 0)
				{
					if (Node.State.Blob != null)
					{
						await MountBlobAsync(Node.State.Blob);
					}
					foreach (NodeInfo Reference in Node.State.References!)
					{
						await FindLiveNodesAsync(Reference, LiveNodes, LiveNodeSet);
					}
				}
			}
		}

		async Task WriteRefAsync(IReadOnlyList<NodeInfo> Nodes, BucketId BucketId, RefId RefId, CbObject Metadata)
		{
			foreach (NodeInfo Node in Nodes)
			{
				ReadOnlySequence<byte> NodeData = await GetDataAsync(Node);
				Node.SetStandalone(NodeData, Node.State.References!);
			}

			BundleRoot Ref = new BundleRoot();
			Ref.Metadata = Metadata;
			Ref.Object = await CreateObjectAsync(Nodes);

			ReadOnlyMemory<byte> RefData = EncodeObject(Ref);
			NextStats.NewRefCount++;
			NextStats.NewRefBytes += RefData.Length;

			await StorageClient.SetRefAsync(NamespaceId, BucketId, RefId, new CbField(RefData));
		}

		ReadOnlyMemory<byte> EncodeObject<T>(T Object)
		{
			SharedWriter.Clear();
			CbSerializer.Serialize<T>(SharedWriter, Object);

			int Size = SharedWriter.GetSize();
			CreateFreeSpace(ref SharedBlobBuffer, 0, Math.Max(Size, Options.MaxBlobSize));

			SharedWriter.CopyTo(SharedBlobBuffer.AsSpan(0, Size));
			return SharedBlobBuffer.AsMemory(0, Size);
		}

		async Task WriteObjectAsync(IReadOnlyList<NodeInfo> Nodes)
		{
			BundleObject Object = await CreateObjectAsync(Nodes);

			ReadOnlyMemory<byte> Data = EncodeObject(Object);
			NextStats.NewBlobCount++;
			NextStats.NewBlobBytes += Data.Length;

			IoHash Hash = await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, Data);

			BlobInfo Blob = new BlobInfo(Hash, Object.Exports.Sum(x => x.Length));
			for (int Idx = 0; Idx < Object.Exports.Count; Idx++)
			{
				NodeInfo Node = Nodes[Idx];
				Node.State = NodeState.Exported(Blob, Object.Exports[Idx], Node.State.References!);
			}

			Blob.Mounted = true;
		}

		/// <summary>
		/// Creates a BundleObject instance. 
		///
		/// WARNING: The <see cref="BundleObject.Data"/> member will be set to the value of <see cref="EncodedBuffer"/> on return, which will be
		/// reused on a subsequent call. If the object needs to be persisted across the lifetime of other objects, this field must be duplicated.
		/// </summary>
		async Task<BundleObject> CreateObjectAsync(IReadOnlyList<NodeInfo> Nodes)
		{
			// Preallocate data in the encoded buffer to reduce fragmentation if we have to resize
			CreateFreeSpace(ref EncodedBuffer, 0, Options.MaxBlobSize);

			// Find node indices for all the export
			Dictionary<NodeInfo, int> NodeToIndex = new Dictionary<NodeInfo, int>();
			foreach (NodeInfo Node in Nodes)
			{
				NodeToIndex[Node] = NodeToIndex.Count;
			}

			// Create the new object
			BundleObject Object = new BundleObject();

			// Find all the imported nodes
			HashSet<NodeInfo> ImportedNodes = new HashSet<NodeInfo>();
			foreach (NodeInfo Node in Nodes)
			{
				ImportedNodes.UnionWith(Node.State.References!);
			}
			ImportedNodes.ExceptWith(Nodes);

			// Find all the imports
			foreach (IGrouping<BlobInfo, NodeInfo> ImportGroup in ImportedNodes.GroupBy(x => x.State.Blob!))
			{
				BlobInfo Blob = ImportGroup.Key;

				NodeInfo[] ImportNodes = ImportGroup.ToArray();
				foreach (NodeInfo ImportNode in ImportNodes)
				{
					NodeToIndex[ImportNode] = NodeToIndex.Count;
				}

				List<BundleImport> Imports = ImportGroup.Select(x => new BundleImport(x.Hash, x.Rank, x.Length)).ToList();
				BundleImportObject ImportObject = new BundleImportObject(new CbObjectAttachment(Blob.Hash), Blob.TotalCost, Imports);
				Object.ImportObjects.Add(ImportObject);
			}

			// Size of data currently stored in the block buffer
			int BlockSize = 0;

			// Compress all the nodes into the encoded buffer
			BundleCompressionPacket Packet = new BundleCompressionPacket(0);
			foreach (NodeInfo Node in Nodes)
			{
				ReadOnlySequence<byte> NodeData = await GetDataAsync(Node);

				// If we can't fit this data into the current block, flush the contents of it first
				if (BlockSize > 0 && BlockSize + NodeData.Length > Options.MinCompressionPacketSize)
				{
					FlushPacket(BlockBuffer.AsMemory(0, BlockSize), Packet);
					Packet = new BundleCompressionPacket(Packet.Offset + Packet.EncodedLength);
					BlockSize = 0;
				}

				// Create the export for this node
				int[] References = Node.State.References.Select(x => NodeToIndex[x]).ToArray();
				BundleExport Export = new BundleExport(Node.Hash, Node.Rank, Packet, BlockSize, Node.Length, References);
				Object.Exports.Add(Export);

				// Write out the new block
				int Offset = Packet.EncodedLength;
				if (NodeData.Length < Options.MinCompressionPacketSize || !NodeData.IsSingleSegment)
				{
					int RequiredSize = Math.Max(BlockSize + (int)NodeData.Length, (int)(Options.MaxBlobSize * 1.2));
					CreateFreeSpace(ref BlockBuffer, BlockSize, RequiredSize);
					foreach (ReadOnlyMemory<byte> NodeSegment in NodeData)
					{
						NodeSegment.CopyTo(BlockBuffer.AsMemory(BlockSize));
						BlockSize += NodeSegment.Length;
					}
				}
				else
				{
					FlushPacket(NodeData.First, Packet);
					Packet = new BundleCompressionPacket(Packet.Offset + Packet.EncodedLength);
				}
			}
			FlushPacket(BlockBuffer.AsMemory(0, BlockSize), Packet);

			// Flush the data
			Object.Data = EncodedBuffer.AsMemory(0, Packet.Offset + Packet.EncodedLength);
			return Object;
		}

		void FlushPacket(ReadOnlyMemory<byte> InputData, BundleCompressionPacket Packet)
		{
			if (InputData.Length > 0)
			{
				int MinFreeSpace = LZ4Codec.MaximumOutputSize(InputData.Length);
				CreateFreeSpace(ref EncodedBuffer, Packet.Offset, Packet.Offset + MinFreeSpace);

				ReadOnlySpan<byte> InputSpan = InputData.Span;
				Span<byte> OutputSpan = EncodedBuffer.AsSpan(Packet.Offset);

				Packet.DecodedLength = InputData.Length;
				Packet.EncodedLength = LZ4Codec.Encode(InputSpan, OutputSpan);

				Debug.Assert(Packet.EncodedLength >= 0);
			}
		}

		void CreateFreeSpace(ref byte[] Buffer, int UsedSize, int RequiredSize)
		{
			if (RequiredSize > Buffer.Length)
			{
				byte[] NewBuffer = new byte[RequiredSize];
				Buffer.AsSpan(0, UsedSize).CopyTo(NewBuffer);
				Buffer = NewBuffer;
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
		internal Bundle(IStorageClient StorageClient, NamespaceId NamespaceId, T Root, BundleOptions Options, IMemoryCache Cache)
			: base(StorageClient, NamespaceId, Root, Options, Cache)
		{
		}
	}
}
