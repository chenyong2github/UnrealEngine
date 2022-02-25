// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using K4os.Compression.LZ4;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;
using System;
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
			public int Cost { get; }

			public BlobInfo? Blob;
			public BundleExport? Export;
			public ReadOnlyMemory<byte> Data;
			public NodeInfo[]? References;

			public NodeInfo(IoHash Hash, int Rank, int Cost)
			{
				this.Hash = Hash;
				this.Rank = Rank;
				this.Cost = Cost;
			}

			public NodeInfo(BlobInfo Blob, BundleImport Import)
				: this(Import.Hash, Import.Rank, Import.Cost)
			{
				this.Blob = Blob;
			}

			void Set(BlobInfo? Blob, BundleExport? Export, ReadOnlyMemory<byte> Data, NodeInfo[]? References)
			{
				this.Blob = Blob;
				this.Export = Export;
				this.Data = Data;
				this.References = References;
			}

			public void SetImport(BlobInfo Blob) => Set(Blob, null, ReadOnlyMemory<byte>.Empty, null);
			public void SetExport(BlobInfo? Blob, BundleExport Export, ReadOnlyMemory<byte> Data, NodeInfo[] References) => Set(Blob, Export, Data, References);
			public void SetStandalone(ReadOnlyMemory<byte> Data, NodeInfo[] References) => Set(null, null, Data, References);
		}

		/// <summary>
		/// Information about a blob
		/// </summary>
		[DebuggerDisplay("{Hash}")]
		class BlobInfo
		{
			public IoHash Hash { get; }
			public DateTime CreationTimeUtc { get; }
			public int TotalCost { get; }

			public bool Mounted { get; set; }
			public HashSet<BlobInfo> ReferencedBy { get; } = new HashSet<BlobInfo>(); // Used for repacking

			public BlobInfo(IoHash Hash, DateTime CreationTimeUtc, int TotalCost)
			{
				this.Hash = Hash;
				this.CreationTimeUtc = CreationTimeUtc;
				this.TotalCost = TotalCost;
			}
 		}

		IStorageClient StorageClient;
		NamespaceId NamespaceId;

		/// <summary>
		/// Hash of the root object
		/// </summary>
		public IoHash RootHash { get; protected set; }
		object RootCacheKey = new object();

		Dictionary<IoHash, NodeInfo> HashToNode = new Dictionary<IoHash, NodeInfo>();
		Dictionary<IoHash, BlobInfo> HashToBlob = new Dictionary<IoHash, BlobInfo>();
		IMemoryCache? Cache;

		/// <summary>
		/// Options for the bundle
		/// </summary>
		public BundleOptions Options { get; }

		byte[] BlockBuffer = Array.Empty<byte>();
		byte[] EncodedBuffer = Array.Empty<byte>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StorageClient">Client interface for the storage backend</param>
		/// <param name="NamespaceId">Namespace for storing blobs in the storage system</param>
		/// <param name="Options">Options for controlling how things are serialized</param>
		/// <param name="Cache">Cache for storing decompressed objects, serialized blobs.</param>
		public Bundle(IStorageClient StorageClient, NamespaceId NamespaceId, BundleOptions Options, IMemoryCache? Cache)
		{
			this.StorageClient = StorageClient;
			this.NamespaceId = NamespaceId;
			this.Options = Options;
			this.Cache = Cache;
			this.HashToNode = new Dictionary<IoHash, NodeInfo>();
		}

		/// <summary>
		/// Gets data for the node with the given hash
		/// </summary>
		/// <param name="Hash">Hash of the node to return data for</param>
		/// <returns>The node data</returns>
		public ValueTask<ReadOnlyMemory<byte>> GetDataAsync(IoHash Hash)
		{
			return GetDataAsync(HashToNode[Hash]);
		}

		/// <summary>
		/// Gets the data for a given node
		/// </summary>
		/// <param name="Node">The node to get the data for</param>
		/// <returns>The node data</returns>
		async ValueTask<ReadOnlyMemory<byte>> GetDataAsync(NodeInfo Node)
		{
			if (Node.Blob == null)
			{
				if (Node.Export == null)
				{
					return Node.Data;
				}
				else
				{
					return DecodePacket(RootCacheKey, Node.Data, Node.Export.Packet).Slice(Node.Export.Offset, Node.Export.Length);
				}
			}
			else
			{
				await MountBlobAsync(Node.Blob);
				Debug.Assert(Node.Export != null);
				BundleObject Object = await GetObjectAsync(Node.Blob.Hash);
				return DecodePacket(Node.Blob.Hash, Object.Data, Node.Export.Packet).Slice(Node.Export.Offset, Node.Export.Length);
			}
		}

		/// <summary>
		/// Write a node's data
		/// </summary>
		/// <param name="Data">The node data</param>
		/// <param name="References">Hashes for referenced nodes</param>
		/// <returns>Hash of the data</returns>
		public IoHash WriteNode(ReadOnlyMemory<byte> Data, IEnumerable<IoHash> References)
		{
			IoHash Hash = IoHash.Compute(Data.Span);
			NodeInfo[] NodeReferences = References.Select(x => HashToNode[x]).Distinct().OrderBy(x => x.Hash).ToArray();

			NodeInfo? Node;
			if (!HashToNode.TryGetValue(Hash, out Node))
			{
				int Rank = 0;
				foreach (NodeInfo Reference in NodeReferences)
				{
					Rank = Math.Max(Rank, Reference.Rank + 1);
				}

				Node = new NodeInfo(Hash, Rank, Data.Length);
				HashToNode[Hash] = Node;
			}

			Node.SetStandalone(Data, NodeReferences);
			return Hash;
		}

		async ValueTask MountBlobAsync(BlobInfo Blob)
		{
			if(!Blob.Mounted)
			{
				BundleObject Object = await StorageClient.ReadObjectAsync<BundleObject>(NamespaceId, Blob.Hash);
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
		ReadOnlyMemory<byte> DecodePacket(object BlobKey, ReadOnlyMemory<byte> Data, BundleCompressionPacket Packet)
		{
			object CacheKey = (BlobKey, Packet.Offset);

			ReadOnlyMemory<byte> DecodedData;
			if (Cache == null || !Cache.TryGetValue(CacheKey, out DecodedData))
			{
				byte[] DecodedBuffer = new byte[Packet.DecodedLength];
				LZ4Codec.Decode(Data.Span.Slice(Packet.Offset, Packet.EncodedLength), DecodedBuffer);

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

		/// <summary>
		/// Reads a ref into the bundle
		/// </summary>
		/// <param name="BucketId">Bucket containing the ref</param>
		/// <param name="RefId">Reference id</param>
		public virtual async Task ReadAsync(BucketId BucketId, RefId RefId)
		{
			BundleObject Object = await StorageClient.GetRefAsync<BundleObject>(NamespaceId, BucketId, RefId);
			RegisterObject(null, Object, Object.Data);
			RootHash = Object.Exports[Object.Exports.Count - 1].Hash;
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
				NodeInfo Node = FindOrAddNode(Export.Hash, Export.Rank, Export.Cost);
				Nodes.Add(Node);
			}
			foreach (BundleImportObject ImportObject in Object.ImportObjects)
			{
				BlobInfo? ImportBlob;
				if (!HashToBlob.TryGetValue(ImportObject.Object.Hash, out ImportBlob))
				{
					ImportBlob = new BlobInfo(ImportObject.Object.Hash, ImportObject.CreationTimeUtc, ImportObject.TotalCost);
					HashToBlob[ImportObject.Object.Hash] = ImportBlob;
				}
				foreach (BundleImport Import in ImportObject.Imports)
				{
					NodeInfo Node = FindOrAddNode(Import.Hash, Import.Rank, Import.Cost);
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
				Node.SetExport(Blob, Object.Exports[Idx], Data, Object.Exports[Idx].References.Select(x => Nodes[x]).ToArray());
			}
		}

		/// <summary>
		/// Persist the bundle to storage
		/// </summary>
		/// <param name="BucketId"></param>
		/// <param name="RefId"></param>
		/// <param name="Compact"></param>
		/// <param name="UtcNow"></param>
		public virtual async Task WriteAsync(BucketId BucketId, RefId RefId, bool Compact, DateTime UtcNow)
		{
			NodeInfo RootNode = HashToNode[RootHash];

			// If we're compacting the output, see if there are any other blocks we should merge
			if (Compact)
			{
				// Find the live set of objects
				HashSet<NodeInfo> LiveNodes = new HashSet<NodeInfo>();
				await FindFullLiveSetAsync(RootNode, LiveNodes);

				// Find the live set of blobs
				HashSet<BlobInfo> LiveBlobs = new HashSet<BlobInfo>();
				foreach (NodeInfo LiveNode in LiveNodes)
				{
					if (LiveNode.Blob != null)
					{
						LiveBlobs.Add(LiveNode.Blob);
					}
				}

				// Clear the reference list on each one
				foreach (BlobInfo LiveBlob in LiveBlobs)
				{
					LiveBlob.ReferencedBy.Clear();
				}

				// Find the first reference from each node to other blobs
				foreach (NodeInfo NodeInfo in LiveNodes)
				{
					if (NodeInfo.Rank > 0 && NodeInfo.Blob != null)
					{
						foreach (NodeInfo ReferencedNode in NodeInfo.References!)
						{
							BlobInfo? ReferencedBlob = ReferencedNode.Blob;
							if (ReferencedBlob != null)
							{
								ReferencedBlob.ReferencedBy.Add(NodeInfo.Blob);
							}
						}
					}
				}

				// Clear out the reference data on each blob
				HashSet<BlobInfo> VisitedBlobs = new HashSet<BlobInfo>();
				foreach (BlobInfo LiveBlob in LiveBlobs)
				{
					ExpandReferences(LiveBlob, VisitedBlobs);
				}

				// Find the total cost of all the current blobs, then loop through the blobs trying to find a more optimal arrangement
				List<NodeInfo> SortedLiveNodes = LiveNodes.OrderByDescending(x => x.Rank).ThenByDescending(x => x.Hash).ToList();
				for (; ; )
				{
					BlobInfo? MergeBlob = null;
					double MergeCost = GetNewCostHeuristic(SortedLiveNodes, new HashSet<BlobInfo>(), UtcNow);

					foreach (BlobInfo LiveBlob in LiveBlobs)
					{
						double PotentialCost = GetNewCostHeuristic(SortedLiveNodes, LiveBlob.ReferencedBy, UtcNow);
						if (PotentialCost < MergeCost)
						{
							MergeBlob = LiveBlob;
							MergeCost = PotentialCost;
						}
					}

					// Bail out if we didn't find anything to merge
					if (MergeBlob == null)
					{
						break;
					}

					// Make sure this blob is resident (it may not have been loaded above if it's a leaf blob)
					await MountBlobAsync(MergeBlob);

					// Find all the live nodes in this blob
					List<NodeInfo> MergeNodes = LiveNodes.Where(x => x.Blob == MergeBlob).ToList();
					foreach (NodeInfo MergeNode in MergeNodes)
					{
						ReadOnlyMemory<byte> Data = await GetDataAsync(MergeNode);
						MergeNode.SetStandalone(Data, MergeNode.References!);
					}

					// Remove the merged blob from the live list
					LiveBlobs.Remove(MergeBlob);
				}
			}

			// Find all the modified nodes
			HashSet<NodeInfo> NewNodes = new HashSet<NodeInfo>();
			FindModifiedLiveSet(RootNode, NewNodes);

			// Write all the new blobs. Sort by rank and go from end to start, updating the list as we go so that we can figure out imports correctly.
			NodeInfo[] WriteNodes = NewNodes.Where(x => x.Blob == null).OrderBy(x => x.Rank).ThenBy(x => x.Hash).ToArray();

			int MinIdx = 0;
			int NextBlobCost = WriteNodes[0].Cost;

			for (int MaxIdx = 1; MaxIdx < WriteNodes.Length; MaxIdx++)
			{
				NextBlobCost += WriteNodes[MaxIdx].Cost;
				if (NextBlobCost > Options.MaxBlobSize)
				{
					await WriteObjectAsync(WriteNodes.Slice(MinIdx, MaxIdx - MinIdx), UtcNow);
					MinIdx = MaxIdx;
					NextBlobCost = WriteNodes[MaxIdx].Cost;
				}
			}

			if (NextBlobCost > Options.MaxInlineBlobSize && MinIdx + 1 < WriteNodes.Length)
			{
				await WriteObjectAsync(WriteNodes.Slice(MinIdx, WriteNodes.Length - 1 - MinIdx), UtcNow);
				MinIdx = WriteNodes.Length - 1;
			}

			// Write the final ref
			await WriteRefAsync(WriteNodes.Slice(MinIdx), BucketId, RefId, UtcNow);
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

		async Task FindFullLiveSetAsync(NodeInfo Node, HashSet<NodeInfo> LiveNodes)
		{
			if (LiveNodes.Add(Node) && Node.Rank > 0)
			{
				if (Node.Blob != null)
				{
					await MountBlobAsync(Node.Blob);
				}
				foreach (NodeInfo Reference in Node.References!)
				{
					await FindFullLiveSetAsync(Reference, LiveNodes);
				}
			}
		}

		void ExpandReferences(BlobInfo Blob, HashSet<BlobInfo> VisitedBlobs)
		{
			if (VisitedBlobs.Add(Blob))
			{
				BlobInfo[] ReferencedBy = Blob.ReferencedBy.ToArray();
				foreach (BlobInfo ReferencedBlob in ReferencedBy)
				{
					ExpandReferences(ReferencedBlob, VisitedBlobs);
					Blob.ReferencedBy.UnionWith(ReferencedBlob.ReferencedBy);
				}
				Blob.ReferencedBy.Add(Blob);
			}
		}

		async Task WriteRefAsync(IReadOnlyList<NodeInfo> Nodes, BucketId BucketId, RefId RefId, DateTime UtcNow)
		{
			BundleObject Object = await CreateObjectAsync(Nodes, UtcNow);
			await StorageClient.SetRefAsync(NamespaceId, BucketId, RefId, Object);

			foreach (NodeInfo Node in Nodes)
			{
				Node.Blob = null;
				Node.Data = Object.Data;
			}

			RootCacheKey = new object();
		}

		async Task WriteObjectAsync(IReadOnlyList<NodeInfo> Nodes, DateTime UtcNow)
		{
			BundleObject Object = await CreateObjectAsync(Nodes, UtcNow);
			IoHash Hash = await StorageClient.WriteObjectAsync(NamespaceId, Object);

			BlobInfo Blob = new BlobInfo(Hash, Object.CreationTimeUtc, Object.Exports.Sum(x => x.Cost));
			foreach (NodeInfo Node in Nodes)
			{
				Node.Blob = Blob;
				Node.Data = ReadOnlyMemory<byte>.Empty;
			}
		}

		async Task<BundleObject> CreateObjectAsync(IReadOnlyList<NodeInfo> Nodes, DateTime UtcNow)
		{
			// Find node indices for all the export
			Dictionary<NodeInfo, int> NodeToIndex = new Dictionary<NodeInfo, int>();
			foreach (NodeInfo Node in Nodes)
			{
				NodeToIndex[Node] = NodeToIndex.Count;
			}

			// Create the new object
			BundleObject Object = new BundleObject();
			Object.CreationTimeUtc = UtcNow;

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

				List<BundleImport> Imports = ImportGroup.Select(x => new BundleImport(x.Hash, x.Rank, x.Cost)).ToList();
				BundleImportObject ImportObject = new BundleImportObject(Blob.CreationTimeUtc, new CbObjectAttachment(Blob.Hash), Blob.TotalCost, Imports);
				Object.ImportObjects.Add(ImportObject);
			}

			// Compress all the nodes into the encoded buffer
			BundleCompressionPacket Packet = new BundleCompressionPacket(0);
			foreach (NodeInfo Node in Nodes)
			{
				ReadOnlyMemory<byte> NodeData = await GetDataAsync(Node);

				// If we can't fit this data into the current block, flush the contents of it first
				if (Packet.DecodedLength + NodeData.Length > Options.MinCompressionPacketSize)
				{
					FlushPacket(Packet);
					Packet = new BundleCompressionPacket(Packet.Offset + Packet.EncodedLength);
				}

				// Create the export for this node
				int[] References = Node.References.Select(x => NodeToIndex[x]).ToArray();
				Node.Export = new BundleExport(Node.Hash, Node.Rank, Node.Cost, Packet, Packet.DecodedLength, NodeData.Length, References);
				Object.Exports.Add(Node.Export);

				// Write out the new block
				int Offset = Packet.EncodedLength;
				if (NodeData.Length < Options.MinCompressionPacketSize)
				{
					int RequiredSize = Math.Max(Packet.DecodedLength + NodeData.Length, (int)(Options.MaxBlobSize * 1.2));
					CreateFreeSpace(ref BlockBuffer, Packet.DecodedLength, RequiredSize);
					NodeData.CopyTo(BlockBuffer.AsMemory(Packet.DecodedLength));
					Packet.DecodedLength += NodeData.Length;
				}
				else
				{
					FlushPacket(Packet);
					Packet = new BundleCompressionPacket(Packet.Offset + Packet.EncodedLength);
				}
			}
			FlushPacket(Packet);

			// Flush the data
			Object.Data = EncodedBuffer.AsSpan(0, Packet.Offset + Packet.EncodedLength).ToArray();

			return Object;
		}

		void FlushPacket(BundleCompressionPacket Packet)
		{
			if (Packet.DecodedLength > 0)
			{
				int MinFreeSpace = LZ4Codec.MaximumOutputSize(Packet.DecodedLength);
				CreateFreeSpace(ref EncodedBuffer, Packet.Offset, Packet.Offset + MinFreeSpace);

				ReadOnlySpan<byte> InputSpan = BlockBuffer.AsSpan(0, Packet.DecodedLength);
				Span<byte> OutputSpan = EncodedBuffer.AsSpan(Packet.Offset);

				Packet.EncodedLength = LZ4Codec.Encode(InputSpan, OutputSpan);
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

		/// <inheritdoc cref="GetCostHeuristic(int, TimeSpan)"/>
		double GetCostHeuristic(BlobInfo Info, DateTime UtcNow) => GetCostHeuristic(Info.TotalCost, UtcNow - Info.CreationTimeUtc);

		/// <summary>
		/// Gets the cost of packaging the given set of nodes which are either not in a blob or are in a blob listed to be collapsed
		/// </summary>
		/// <param name="Nodes">The full list of nodes</param>
		/// <param name="CollapseBlobs">Set of blobs that should be collapsed</param>
		/// <param name="UtcNow">The current time</param>
		/// <returns></returns>
		double GetNewCostHeuristic(List<NodeInfo> Nodes, HashSet<BlobInfo> CollapseBlobs, DateTime UtcNow)
		{
			double Cost = 0.0;
			foreach (BlobInfo CollapseBlob in CollapseBlobs)
			{
				Cost -= GetCostHeuristic(CollapseBlob, UtcNow);
			}

			int BlobSize = 0;
			for (int Idx = Nodes.Count - 1; Idx >= 0; Idx--)
			{
				NodeInfo Node = Nodes[Idx];
				if (Node.Blob == null || CollapseBlobs.Contains(Node.Blob))
				{
					if (BlobSize > 0 && BlobSize + Node.Data.Length > Options.MaxBlobSize)
					{
						Cost += GetCostHeuristic(BlobSize, TimeSpan.Zero);
						BlobSize = 0;
					}
					BlobSize += Node.Data.Length;
				}
			}
			Cost += GetCostHeuristic(BlobSize, TimeSpan.Zero);
			return Cost;
		}

		/// <summary>
		/// Heuristic which estimates the cost of a particular blob. This is used to compare scenarios of merging blobs to reduce download
		/// size against keeping older blobs which a lot of agents already have.
		/// </summary>
		/// <param name="Size">Size of the blob</param>
		/// <param name="Age">Age of the blob</param>
		/// <returns>Heuristic for the cost of a blob</returns>
		static double GetCostHeuristic(int Size, TimeSpan Age)
		{
			// Time overhead to starting a download
			const double DownloadInit = 0.1;

			// Download speed for agents, in bytes/sec
			const double DownloadRate = 1024 * 1024;

			// Probability of an agent having to download everything. Prevents bias against keeping a large number of files.
			const double CleanSyncProbability = 0.2;

			// Average length of time between agents having to update
			TimeSpan AverageCoherence = TimeSpan.FromHours(4.0);

			// Scale the age into a -1.0 -> 1.0 range around AverageCoherence
			double ScaledAge = (AverageCoherence - Age).TotalSeconds / AverageCoherence.TotalSeconds;

			// Get the probability of agents having to sync this blob based on its age. This is modeled as a logistic function (1 / (1 + e^-x))
			// with value of 0.5 at AverageCoherence, and MaxInterval at zero.

			// Find the scale factor for the 95% interval
			//    1 / (1 + e^-x) = MaxInterval
			//    e^-x = (1 / MaxInterval) - 1
			//    x = -ln((1 / MaxInterval) - 1)
			const double MaxInterval = 0.95;
			double SigmoidScale = -Math.Log((1.0 / MaxInterval) - 1.0);

			// Find the probability of having to sync this 
			double Param = ScaledAge * SigmoidScale;
			double Probability = 1.0 / (1.0 + Math.Exp(-Param));

			// Scale the probability against having to do a full sync
			Probability = CleanSyncProbability + (Probability * (1.0 - CleanSyncProbability));

			// Compute the final cost estimate; the amount of time we expect agents to spend downloading the file
			return Probability * (DownloadInit + (Size / DownloadRate));
		}
	}

	/// <summary>
	/// Derived version of <see cref="Bundle"/> supporting a concrete root node type
	/// </summary>
	/// <typeparam name="T">The root node type</typeparam>
	public class Bundle<T> : Bundle where T : BundleNode
	{
		static BundleNodeFactory<T> Factory = CreateFactory();

		/// <summary>
		/// Object at the root of the bundle
		/// </summary>
		public T Root { get; private set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public Bundle(IStorageClient StorageClient, NamespaceId NamespaceId, BundleOptions Options, IMemoryCache? Cache)
			: base(StorageClient, NamespaceId, Options, Cache)
		{
			Root = Factory.CreateRoot(this);
		}

		/// <summary>
		/// Create a factory for manipulating T instances
		/// </summary>
		/// <returns></returns>
		static BundleNodeFactory<T> CreateFactory()
		{
			BundleNodeFactoryAttribute? Attribute = typeof(T).GetCustomAttribute<BundleNodeFactoryAttribute>();
			if (Attribute == null)
			{
				throw new InvalidOperationException("Nodes used as the root of a bundle must have a BundleNodeFactoryAttribute");
			}
			return (BundleNodeFactory<T>)Activator.CreateInstance(Attribute.Type)!;
		}

		/// <inheritdoc/>
		public override async Task ReadAsync(BucketId BucketId, RefId RefId)
		{
			await base.ReadAsync(BucketId, RefId);
			ReadOnlyMemory<byte> Data = await GetDataAsync(RootHash);
			Root = Factory.ParseRoot(this, RootHash, Data);
		}

		/// <inheritdoc/>
		public override async Task WriteAsync(BucketId BucketId, RefId RefId, bool Compact, DateTime UtcNow)
		{
			RootHash = Root.Serialize();
			await base.WriteAsync(BucketId, RefId, Compact, UtcNow);
		}
	}
}
