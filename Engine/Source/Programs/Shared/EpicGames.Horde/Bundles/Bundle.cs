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
	}

	/// <summary>
	/// Base class for manipulating bundles
	/// </summary>
	public class Bundle
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

			public BlobInfo? Blob;
			public BundleExport? Export;
			public ReadOnlySequence<byte> Data;
			public NodeInfo[]? References;

			public NodeInfo(IoHash Hash, int Rank, int Length)
			{
				if (Length <= 0)
				{
					throw new ArgumentException("Node length must be greater than zero", nameof(Length));
				}

				this.Hash = Hash;
				this.Rank = Rank;
				this.Length = Length;
			}

			public NodeInfo(BlobInfo Blob, BundleImport Import)
				: this(Import.Hash, Import.Rank, Import.Length)
			{
				this.Blob = Blob;
			}

			void Set(BlobInfo? Blob, BundleExport? Export, ReadOnlySequence<byte> Data, NodeInfo[]? References)
			{
				this.Blob = Blob;
				this.Export = Export;
				this.Data = Data;
				this.References = References;
			}

			public void SetImport(BlobInfo Blob) => Set(Blob, null, ReadOnlySequence<byte>.Empty, null);
			public void SetExport(BlobInfo? Blob, BundleExport Export, ReadOnlySequence<byte> Data, NodeInfo[] References) => Set(Blob, Export, Data, References);
			public void SetStandalone(ReadOnlySequence<byte> Data, NodeInfo[] References) => Set(null, null, Data, References);
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

		object RootCacheKey = new object();

		/// <summary>
		/// Reference to the root node in the bundle
		/// </summary>
		public BundleNode Root { get; private set; }

		Dictionary<IoHash, NodeInfo> HashToNode = new Dictionary<IoHash, NodeInfo>();
		Dictionary<IoHash, BlobInfo> HashToBlob = new Dictionary<IoHash, BlobInfo>();
		IMemoryCache? Cache;

		CbWriter SharedWriter = new CbWriter();
		byte[] SharedBlobBuffer = Array.Empty<byte>();

		/// <summary>
		/// Options for the bundle
		/// </summary>
		public BundleOptions Options { get; }

		byte[] BlockBuffer = Array.Empty<byte>();
		byte[] EncodedBuffer = Array.Empty<byte>();

		/// <summary>
		/// Constructor
		/// </summary>
		protected Bundle(IStorageClient StorageClient, NamespaceId NamespaceId, BundleNode Root, BundleOptions Options, IMemoryCache? Cache)
		{
			this.StorageClient = StorageClient;
			this.NamespaceId = NamespaceId;
			this.Root = Root;
			this.Options = Options;
			this.Cache = Cache;
			this.HashToNode = new Dictionary<IoHash, NodeInfo>();
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
		protected Bundle(IStorageClient StorageClient, NamespaceId NamespaceId, BundleObject Object, BundleNode Root, BundleOptions Options, IMemoryCache? Cache)
			: this(StorageClient, NamespaceId, Root, Options, Cache)
		{
			RegisterObject(null, Object, Object.Data);
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
		public static Bundle<T> Create<T>(IStorageClient StorageClient, NamespaceId NamespaceId, BundleOptions Options, IMemoryCache? Cache) where T : BundleNode, new()
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
		public static Bundle<T> Create<T>(IStorageClient StorageClient, NamespaceId NamespaceId, T Root, BundleOptions Options, IMemoryCache? Cache) where T : BundleNode
		{
			return new Bundle<T>(StorageClient, NamespaceId, Root, Options, Cache);
		}

		/// <summary>
		/// Reads a bundle from storage
		/// </summary>
		public static async ValueTask<Bundle<T>> ReadAsync<T>(IStorageClient StorageClient, NamespaceId NamespaceId, BucketId BucketId, RefId RefId, BundleOptions Options, IMemoryCache? Cache) where T : BundleNode
		{
			Bundle<T> NewBundle = new Bundle<T>(StorageClient, NamespaceId, null!, Options, Cache);

			BundleObject Object = await StorageClient.GetRefAsync<BundleObject>(NamespaceId, BucketId, RefId);
			NewBundle.RegisterObject(null, Object, Object.Data);

			IoHash RootHash = Object.Exports[Object.Exports.Count - 1].Hash;
			BundleNodeRef<T> RootRef = new BundleNodeRef<T>(RootHash);

			((Bundle)NewBundle).Root = await NewBundle.GetAsync(RootRef);
			return NewBundle;
		}

		/// <summary>
		/// Flush a subset of nodes in the working set.
		/// </summary>
		private void SerializeWorkingSet(int IgnoreCount)
		{
			// Find all the nodes that need to be flushed, in order
			List<BundleNodeRef> NodeRefs = new List<BundleNodeRef>();
			LinkWorkingSet(Root, null, NodeRefs);
			NodeRefs.SortBy(x => x.LastModifiedTime);

			// Write them all to storage
			for (int Idx = 0; Idx + IgnoreCount < NodeRefs.Count; Idx++)
			{
				BundleNodeRef NodeRef = NodeRefs[Idx];

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
		/// <param name="IncomingRef">The parent reference</param>
		/// <param name="NodeRefs">List to accumulate the list of all modified references</param>
		private void LinkWorkingSet(BundleNode Node, BundleNodeRef? IncomingRef, List<BundleNodeRef> NodeRefs)
		{
			Node.IncomingRef = IncomingRef;

			foreach (BundleNodeRef ChildRef in Node.GetReferences())
			{
				ChildRef.ParentRef ??= IncomingRef;

				if (ChildRef.Node != null && ChildRef.LastModifiedTime != 0)
				{
					LinkWorkingSet(ChildRef.Node, ChildRef, NodeRefs);
					if (IncomingRef != null)
					{
						IncomingRef.LastModifiedTime = Math.Max(IncomingRef.LastModifiedTime, ChildRef.LastModifiedTime + 1);
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
			if (Node.Blob == null && WriteNodesSet.Add(Node))
			{
				foreach (NodeInfo ChildNode in Node.References!)
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
			if (Node.Blob == null)
			{
				if (Node.Export == null)
				{
					return Node.Data;
				}
				else
				{
					return new ReadOnlySequence<byte>(DecodePacket(RootCacheKey, Node.Data, Node.Export.Packet).Slice(Node.Export.Offset, Node.Export.Length));
				}
			}
			else
			{
				await MountBlobAsync(Node.Blob);
				Debug.Assert(Node.Export != null);
				BundleObject Object = await GetObjectAsync(Node.Blob.Hash);
				return new ReadOnlySequence<byte>(DecodePacket(Node.Blob.Hash, new ReadOnlySequence<byte>(Object.Data), Node.Export.Packet).Slice(Node.Export.Offset, Node.Export.Length));
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

			NodeInfo? Node;
			if (!HashToNode.TryGetValue(Hash, out Node))
			{
				int Rank = 0;
				foreach (NodeInfo Reference in NodeReferences)
				{
					Rank = Math.Max(Rank, Reference.Rank + 1);
				}

				Node = new NodeInfo(Hash, Rank, (int)Data.Length);
				HashToNode[Hash] = Node;
			}

			Node.SetStandalone(Data, NodeReferences);
			return Node;
		}

		async ValueTask MountBlobAsync(BlobInfo Blob)
		{
			if(!Blob.Mounted)
			{
				BundleObject Object = await GetObjectAsync(Blob.Hash);
				RegisterObject(Blob, Object, ReadOnlyMemory<byte>.Empty);
				Blob.Mounted = true;
			}
		}

		/// <summary>
		/// Reads an object data from the store
		/// </summary>
		/// <param name="Hash">Hash of the object</param>
		/// <returns>The parsed object</returns>
		async ValueTask<BundleObject> GetObjectAsync(IoHash Hash)
		{
			BundleObject Object;
			if (Cache == null || !Cache.TryGetValue(Hash, out Object))
			{
				ReadOnlyMemory<byte> Data = await StorageClient.ReadBlobToMemoryAsync(NamespaceId, Hash);
				Object = CbSerializer.Deserialize<BundleObject>(Data);

				if (Cache != null)
				{
					using (ICacheEntry Entry = Cache.CreateEntry(Hash))
					{
						Entry.SetValue(Object);
						Entry.SetSize(Data.Length);
					}
				}
			}
			return Object;
		}

		/// <summary>
		/// Gets a decoded block from the store
		/// </summary>
		/// <param name="BlobKey">Key for the blob</param>
		/// <param name="Data">Raw blob data</param>
		/// <param name="Packet">The decoded block location and size</param>
		/// <returns>The decoded data</returns>
		ReadOnlyMemory<byte> DecodePacket(object BlobKey, ReadOnlySequence<byte> Data, BundleCompressionPacket Packet)
		{
			object CacheKey = (BlobKey, Packet.Offset);

			ReadOnlyMemory<byte> DecodedData;
			if (Cache == null || !Cache.TryGetValue(CacheKey, out DecodedData))
			{
				Debug.Assert(Data.IsSingleSegment);

				byte[] DecodedBuffer = new byte[Packet.DecodedLength];
				LZ4Codec.Decode(Data.FirstSpan.Slice(Packet.Offset, Packet.EncodedLength), DecodedBuffer);

				DecodedData = DecodedBuffer;

				if (Cache != null)
				{
					using (ICacheEntry Entry = Cache.CreateEntry(CacheKey))
					{
						Entry.SetValue(DecodedData);
						Entry.SetSize(DecodedData.Length);
					}
				}
			}

			return DecodedData;
		}

		NodeInfo FindOrAddNode(IoHash Hash, int Rank, int Cost)
		{
			NodeInfo? Node;
			if (!HashToNode.TryGetValue(Hash, out Node))
			{
				Node = new NodeInfo(Hash, Rank, Cost);
				HashToNode[Hash] = Node;
			}
			return Node;
		}

		void RegisterObject(BlobInfo? Blob, BundleObject Object, ReadOnlyMemory<byte> Data)
		{
			List<NodeInfo> Nodes = new List<NodeInfo>();
			foreach (BundleExport Export in Object.Exports)
			{
				NodeInfo Node = FindOrAddNode(Export.Hash, Export.Rank, Export.Length);
				Nodes.Add(Node);
			}
			foreach (BundleImportObject ImportObject in Object.ImportObjects)
			{
				BlobInfo? ImportBlob;
				if (!HashToBlob.TryGetValue(ImportObject.Object.Hash, out ImportBlob))
				{
					ImportBlob = new BlobInfo(ImportObject.Object.Hash, ImportObject.TotalCost);
					HashToBlob[ImportObject.Object.Hash] = ImportBlob;
				}
				foreach (BundleImport Import in ImportObject.Imports)
				{
					NodeInfo Node = FindOrAddNode(Import.Hash, Import.Rank, Import.Length);
					if(Node.Blob == null)
					{
						Node.SetImport(ImportBlob);
					}
					Nodes.Add(Node);
				}
			}
			for(int Idx = 0; Idx < Object.Exports.Count; Idx++)
			{
				NodeInfo Node = Nodes[Idx];
				Node.SetExport(Blob, Object.Exports[Idx], new ReadOnlySequence<byte>(Data), Object.Exports[Idx].References.Select(x => Nodes[x]).ToArray());
			}
		}

		/// <summary>
		/// Persist the bundle to storage
		/// </summary>
		/// <param name="BucketId"></param>
		/// <param name="RefId"></param>
		/// <param name="Compact"></param>
		public virtual async Task WriteAsync(BucketId BucketId, RefId RefId, bool Compact)
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
					BlobInfo? LiveBlob = LiveNode.Blob;
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
							Node.SetStandalone(Data, Node.References!);
						}
					}
				}

				// Walk backwards through the list of live nodes and make anything that references a repacked node also standalone
				for (int Idx = LiveNodes.Count - 1; Idx >= 0; Idx--)
				{
					NodeInfo LiveNode = LiveNodes[Idx];
					if (LiveNode.References != null && LiveNode.References.Any(x => x.Blob == null))
					{
						ReadOnlySequence<byte> Data = await GetDataAsync(LiveNode);
						LiveNode.SetStandalone(Data, LiveNode.References!);
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
			NodeInfo[] WriteNodes = NewNodes.Where(x => x.Blob == null).OrderBy(x => x.Rank).ThenBy(x => x.Hash).ToArray();

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
			await WriteRefAsync(WriteNodes.Slice(MinIdx), BucketId, RefId);
		}

		private void FindModifiedLiveSet(NodeInfo Node, HashSet<NodeInfo> Nodes)
		{
			if (Nodes.Add(Node) && Node.Rank > 0)
			{
				foreach (NodeInfo Reference in Node.References!)
				{
					if (Reference.Blob == null)
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
					if (Node.Blob != null)
					{
						await MountBlobAsync(Node.Blob);
					}
					foreach (NodeInfo Reference in Node.References!)
					{
						await FindLiveNodesAsync(Reference, LiveNodes, LiveNodeSet);
					}
				}
			}
		}

		async Task WriteRefAsync(IReadOnlyList<NodeInfo> Nodes, BucketId BucketId, RefId RefId)
		{
			BundleObject Object = await CreateObjectAsync(Nodes);
			await StorageClient.SetRefAsync(NamespaceId, BucketId, RefId, Object);
			Object.Data = Object.Data.ToArray(); // Duplicated since its lifetime persists beyond this function. See remarks in CreateObjectAsync.

			foreach (NodeInfo Node in Nodes)
			{
				Node.Blob = null;
				Node.Data = new ReadOnlySequence<byte>(Object.Data);
			}

			RootCacheKey = new object();
		}

		ReadOnlyMemory<byte> EncodeObject(BundleObject Object)
		{
			SharedWriter.Clear();
			CbSerializer.Serialize(SharedWriter, Object);

			int Size = SharedWriter.GetSize();
			CreateFreeSpace(ref SharedBlobBuffer, 0, Math.Max(Size, Options.MaxBlobSize));

			SharedWriter.CopyTo(SharedBlobBuffer.AsSpan(0, Size));
			return SharedBlobBuffer.AsMemory(0, Size);
		}

		async Task WriteObjectAsync(IReadOnlyList<NodeInfo> Nodes)
		{
			BundleObject Object = await CreateObjectAsync(Nodes);
			IoHash Hash = await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, EncodeObject(Object));

			BlobInfo Blob = new BlobInfo(Hash, Object.Exports.Sum(x => x.Length));
			foreach (NodeInfo Node in Nodes)
			{
				Node.Blob = Blob;
				Node.Data = ReadOnlySequence<byte>.Empty;
			}
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
				ImportedNodes.UnionWith(Node.References!);
			}
			ImportedNodes.ExceptWith(Nodes);

			// Find all the imports
			foreach (IGrouping<BlobInfo, NodeInfo> ImportGroup in ImportedNodes.GroupBy(x => x.Blob!))
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
				int[] References = Node.References.Select(x => NodeToIndex[x]).ToArray();
				Node.Export = new BundleExport(Node.Hash, Node.Rank, Packet, Packet.DecodedLength, Node.Length, References);
				Object.Exports.Add(Node.Export);

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
		internal Bundle(IStorageClient StorageClient, NamespaceId NamespaceId, T Root, BundleOptions Options, IMemoryCache? Cache)
			: base(StorageClient, NamespaceId, Root, Options, Cache)
		{
		}
	}
}
