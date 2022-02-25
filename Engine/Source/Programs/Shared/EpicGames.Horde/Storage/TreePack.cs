// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using K4os.Compression.LZ4;
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
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	#region Storage primitives

	/// <summary>
	/// Blob within the tree pack
	/// </summary>
	public class TreePackObject
	{
		/// <summary>
		/// Time that this object was minted. Used to determine how likely it will be that a client will already have it.
		/// </summary>
		[CbField("time")]
		public DateTime Time { get; set; }

		/// <summary>
		/// Imports from object attachments.
		/// </summary>
		[CbField("objimp")]
		public List<TreePackObjectImport> ObjectImports { get; set; } = new List<TreePackObjectImport>();

		/// <summary>
		/// Imports from binary attachments.
		/// </summary>
		[CbField("binimp")]
		public List<TreePackBinaryImport> BinaryImports { get; set; } = new List<TreePackBinaryImport>();

		/// <summary>
		/// Exported items in the data 
		/// </summary>
		[CbField("exports")]
		public ReadOnlyMemory<byte> Exports { get; set; }

		/// <summary>
		/// Raw data for this exported entries in this blob.
		/// </summary>
		[CbField("data")]
		public ReadOnlyMemory<byte> Data { get; set; }

		/// <summary>
		/// Parses an object from a ref
		/// </summary>
		/// <param name="Ref"></param>
		/// <returns></returns>
		public static TreePackObject Parse(IRef Ref) => Parse(Ref.Value.GetView());

		/// <summary>
		/// Parse an object from the given data
		/// </summary>
		/// <param name="Data"></param>
		/// <returns></returns>
		public static TreePackObject Parse(ReadOnlyMemory<byte> Data) => CbSerializer.Deserialize<TreePackObject>(Data);

		/// <summary>
		/// Serialize this object to compact binary
		/// </summary>
		/// <returns></returns>
		public CbObject ToCbObject() => CbSerializer.Serialize(this);

		/// <summary>
		/// Gets the root hash for this object
		/// </summary>
		/// <returns></returns>
		public IoHash GetRootHash()
		{
			TreePackExport Export = new TreePackExport(Exports.Span);
			return Export.Hash;
		}

		/// <summary>
		/// Gets the root node data for this object
		/// </summary>
		/// <returns></returns>
		public ReadOnlyMemory<byte> GetRootNode()
		{
			TreePackExport Export = new TreePackExport(Exports.Span);
			ReadOnlyMemory<byte> CompressedData = Data.Slice(Export.Offset, Export.CompressedLength);
			return TreePack.UncompressData(CompressedData, Export.UncompressedLength);
		}
	}

	/// <summary>
	/// Reference to another tree pack object
	/// </summary>
	public class TreePackObjectImport
	{
		/// <summary>
		/// Imported object. The <see cref="Keys"/> field contains a flat array of hashes imported from the object.
		/// </summary>
		[CbField("object")]
		public CbObjectAttachment Object { get; set; }

		/// <summary>
		/// List of keys imported from this object.
		/// </summary>
		[CbField("keys")]
		public ReadOnlyMemory<byte> Keys { get; set; }
	}

	/// <summary>
	/// Reference to another tree pack object
	/// </summary>
	public class TreePackBinaryImport
	{
		/// <summary>
		/// Imported binary.
		/// </summary>
		[CbField("binary")]
		public CbBinaryAttachment Binary { get; set; }

		/// <summary>
		/// Time that the binary was minted
		/// </summary>
		[CbField("time")]
		public DateTime Time { get; set; }

		/// <summary>
		/// Length of the imported binary.
		/// </summary>
		[CbField("length")]
		public int Length { get; set; }

		/// <summary>
		/// Packed list of exports taken from this binary
		/// </summary>
		[CbField("exports")]
		public ReadOnlyMemory<byte> Exports { get; set; }
	}

	/// <summary>
	/// Entry for a block of data exported from a blob
	/// </summary>
	public class TreePackExport
	{
		/// <summary>
		/// Length of the export when encoded to a byte array
		/// </summary>
		public const int EncodedLength = IoHash.NumBytes + sizeof(int) + sizeof(int) + sizeof(int);

		/// <summary>
		/// Hash of the exported item
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// Offset of the exported data
		/// </summary>
		public int Offset { get; }

		/// <summary>
		/// Compressed length of the exported data
		/// </summary>
		public int CompressedLength { get; }

		/// <summary>
		/// Length of the exported data when uncompressed
		/// </summary>
		public int UncompressedLength { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public TreePackExport(IoHash Hash, int Offset, int CompressedLength, int UncompressedLength)
		{
			this.Hash = Hash;
			this.Offset = Offset;
			this.CompressedLength = CompressedLength;
			this.UncompressedLength = UncompressedLength;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Span"></param>
		public TreePackExport(ReadOnlySpan<byte> Span)
		{
			Hash = new IoHash(Span);
			Span = Span.Slice(IoHash.NumBytes);

			Offset = BinaryPrimitives.ReadInt32LittleEndian(Span);
			Span = Span.Slice(sizeof(int));

			CompressedLength = BinaryPrimitives.ReadInt32LittleEndian(Span);
			Span = Span.Slice(sizeof(int));

			UncompressedLength = BinaryPrimitives.ReadInt32LittleEndian(Span);
		}

		/// <summary>
		/// Encode an export into a byte array 
		/// </summary>
		/// <param name="Output"></param>
		public void CopyTo(Span<byte> Output)
		{
			Hash.CopyTo(Output);
			Output = Output.Slice(IoHash.NumBytes);

			BinaryPrimitives.WriteInt32LittleEndian(Output, Offset);
			Output = Output.Slice(sizeof(int));

			BinaryPrimitives.WriteInt32LittleEndian(Output, CompressedLength);
			Output = Output.Slice(sizeof(int));

			BinaryPrimitives.WriteInt32LittleEndian(Output, UncompressedLength);
		}
	}

	#endregion

	#region Node types

	/// <summary>
	/// Type of a node in a tree pack. The first byte of any node should be this value.
	/// </summary>
	public enum TreePackNodeType : byte
	{
		/// <summary>
		/// Raw binary data
		/// </summary>
		Binary,

		/// <summary>
		/// A compact binary encoded object
		/// </summary>
		Object,

		/// <summary>
		/// A directory listing
		/// </summary>
		Directory,

		/// <summary>
		/// A concat node
		/// </summary>
		Concat,
	}

	/// <summary>
	/// Flags for a directory entry
	/// </summary>
	public enum TreePackDirEntryFlags : byte
	{
		/// <summary>
		/// This item is a directory
		/// </summary>
		Directory = 1,

		/// <summary>
		/// This item is a file
		/// </summary>
		File = 2,

		/// <summary>
		/// Indicates that the referenced file is executable
		/// </summary>
		Executable = 4,

		/// <summary>
		/// File should be stored as read-only
		/// </summary>
		ReadOnly = 8,

		/// <summary>
		/// File contents are utf-8 encoded text. Client may want to replace line-endings with OS-specific format.
		/// </summary>
		Text = 16,

		/// <summary>
		/// The attached entry includes a Git SHA1 of the corresponding blob/tree contents.
		/// </summary>
		HasGitSha1 = 32,

		/// <summary>
		/// The data for this entry is a Perforce depot path and revision rather than the actual file contents.
		/// </summary>
		PerforceDepotPathAndRevision = 64,
	}

	/// <summary>
	/// Entry within a directory
	/// </summary>
	public class TreePackDirEntry
	{
		/// <summary>
		/// Flags for this entry
		/// </summary>
		public TreePackDirEntryFlags Flags { get; }

		/// <summary>
		/// Name of the entry
		/// </summary>
		public Utf8String Name { get; }

		/// <summary>
		/// Hash of this node's contents.
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// Hash of the corresponding Git object. Only included if <see cref="Flags"/> specifies the <see cref="TreePackDirEntryFlags.HasGitSha1"/> flag.
		/// </summary>
		public Sha1Hash GitSha1 { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public TreePackDirEntry(TreePackDirEntryFlags Flags, Utf8String Name, IoHash Hash, Sha1Hash GitSha1)
		{
			this.Flags = Flags;
			this.Name = Name;
			this.Hash = Hash;
			this.GitSha1 = GitSha1;
		}
	}

	/// <summary>
	/// A directory node
	/// </summary>
	public class TreePackDirNode
	{
		/// <summary>
		/// Entries for this directory
		/// </summary>
		public List<TreePackDirEntry> Entries { get; } = new List<TreePackDirEntry>();

		/// <summary>
		/// Parses a node from a block of memory
		/// </summary>
		public static TreePackDirNode Parse(ReadOnlyMemory<byte> Data)
		{
			ReadOnlySpan<byte> Span = Data.Span;

			Debug.Assert(Span[0] == (byte)TreePackNodeType.Directory);
			Span = Span.Slice(1);

			TreePackDirNode Node = new TreePackDirNode();
			while (Span.Length > 0)
			{
				TreePackDirEntryFlags Flags = (TreePackDirEntryFlags)Span[0];
				Span = Span.Slice(1);

				int NameLength = Span.IndexOf((byte)0);
				Utf8String Name = new Utf8String(Span.Slice(0, NameLength).ToArray());
				Span = Span.Slice(NameLength + 1);

				IoHash Hash = new IoHash(Span);
				Span = Span.Slice(IoHash.NumBytes);

				Sha1Hash GitSha1 = Sha1Hash.Zero;
				if ((Flags & TreePackDirEntryFlags.HasGitSha1) != 0)
				{
					GitSha1 = new Sha1Hash(Span);
					Span = Span.Slice(Sha1Hash.NumBytes);
				}

				Node.Entries.Add(new TreePackDirEntry(Flags, Name, Hash, GitSha1));
			}

			Debug.Assert(Span.Length == 0);
			return Node;
		}

		/// <summary>
		/// Serializes this entry to a byte array
		/// </summary>
		/// <returns></returns>
		public byte[] ToByteArray()
		{
			int Size = 1;
			foreach (TreePackDirEntry Entry in Entries)
			{
				Size += 1 + (Entry.Name.Length + 1) + IoHash.NumBytes;
				if ((Entry.Flags & TreePackDirEntryFlags.HasGitSha1) != 0)
				{
					Size += Sha1Hash.NumBytes;
				}
			}

			byte[] Data = new byte[Size];
			Data[0] = (byte)TreePackNodeType.Directory;

			Span<byte> Span = Data.AsSpan(1);
			foreach (TreePackDirEntry Entry in Entries)
			{
				Span[0] = (byte)Entry.Flags;
				Span = Span.Slice(1);

				Entry.Name.Span.CopyTo(Span);
				Span = Span.Slice(Entry.Name.Length + 1);

				Entry.Hash.CopyTo(Span);
				Span = Span.Slice(IoHash.NumBytes);

				if ((Entry.Flags & TreePackDirEntryFlags.HasGitSha1) != 0)
				{
					Entry.GitSha1.CopyTo(Span);
					Span = Span.Slice(Sha1Hash.NumBytes);
				}
			}

			Debug.Assert(Span.Length == 0);
			return Data;
		}
	}

	/// <summary>
	/// Flags for <see cref="TreePackConcatEntry"/>
	/// </summary>
	public enum TreePackConcatNodeFlags : int
	{
		/// <summary>
		/// Indicates that the referenced object is a leaf node
		/// </summary>
		Leaf = 1,
	}

	/// <summary>
	/// Entry in a concat node
	/// </summary>
	public class TreePackConcatEntry
	{
		/// <summary>
		/// Flags for this entry
		/// </summary>
		public TreePackConcatNodeFlags Flags { get; }

		/// <summary>
		/// Hash of this node/tree
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// Length of this node/tree
		/// </summary>
		public long Length { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public TreePackConcatEntry(TreePackConcatNodeFlags Flags, IoHash Hash, long Length)
		{
			this.Flags = Flags;
			this.Hash = Hash;
			this.Length = Length;
		}

		/// <inheritdoc/>
		public override string ToString() => Hash.ToString();
	}

	/// <summary>
	/// A node which concatenates other nodes together
	/// </summary>
	public class TreePackConcatNode
	{
		/// <summary>
		/// Entries in this concat node
		/// </summary>
		public List<TreePackConcatEntry> Entries = new List<TreePackConcatEntry>();

		/// <summary>
		/// Parses a concat node from a block of memory
		/// </summary>
		public static TreePackConcatNode Parse(ReadOnlySpan<byte> Span)
		{
			Debug.Assert(Span[0] == (byte)TreePackNodeType.Concat);
			Span = Span.Slice(1);

			TreePackConcatNode Node = new TreePackConcatNode();
			while (Span.Length > 0)
			{
				TreePackConcatNodeFlags Flags = (TreePackConcatNodeFlags)BinaryPrimitives.ReadInt32LittleEndian(Span);
				Span = Span.Slice(sizeof(int));

				IoHash Hash = new IoHash(Span);
				Span = Span.Slice(IoHash.NumBytes);

				long Length = BinaryPrimitives.ReadInt64LittleEndian(Span);
				Span = Span.Slice(sizeof(long));

				Node.Entries.Add(new TreePackConcatEntry(Flags, Hash, Length));
			}
			return Node;
		}

		/// <summary>
		/// Serializes into a byte array 
		/// </summary>
		/// <returns></returns>
		public byte[] ToByteArray()
		{
			byte[] Data = new byte[1 + (Entries.Count * (sizeof(int) + IoHash.NumBytes + sizeof(long)))];
			Data[0] = (byte)TreePackNodeType.Concat;

			Span<byte> Span = Data.AsSpan(1);
			foreach (TreePackConcatEntry Entry in Entries)
			{
				BinaryPrimitives.WriteInt32LittleEndian(Span, (int)Entry.Flags);
				Span = Span.Slice(sizeof(int));

				Entry.Hash.CopyTo(Span);
				Span = Span.Slice(IoHash.NumBytes);

				BinaryPrimitives.WriteInt64LittleEndian(Span, Entry.Length);
				Span = Span.Slice(sizeof(long));
			}

			Debug.Assert(Span.Length == 0);
			return Data;
		}
	}

	#endregion

	/// <summary>
	/// Options for configuring a tree pack object
	/// </summary>
	public class TreePackOptions
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
		/// Target chunk size for content-slicing
		/// </summary>
		public int TargetChunkSize { get; set; } = 32 * 1024;

		/// <summary>
		/// Minimum chunk size
		/// </summary>
		public int MinChunkSize { get; set; } = 32 * 1024;

		/// <summary>
		/// Maximum chunk size. Chunks will be split on this boundary if another match is not found.
		/// </summary>
		public int MaxChunkSize { get; set; } = 256 * 1024;
	}

	/// <summary>
	/// Manages storage of a pack of tree objects
	/// </summary>
	public class TreePack
	{
		/// <summary>
		/// Information about a known node in memory. Nodes may come from three places:
		/// <list type="bullet">
		///   <item>New nodes (have data, no blob, no offset/length)</item>
		///   <item>Existing object nodes (have data, have blob, have offset/length)</item>
		///   <item>Existing binary nodes (no data, have blob, have offset/length)</item>
		/// </list>
		/// </summary>
		class NodeInfo
		{
			public IoHash Hash { get; }
			public BlobInfo? Blob { get; set; }
			public int Offset { get; set; }
			public int CompressedLength { get; set; }
			public int UncompressedLength { get; set; }
			public ReadOnlyMemory<byte> CompressedData { get; set; }

			// Temp for computing live set
			public int Rank;

			public NodeInfo(IoHash Hash, BlobInfo? Blob, int Offset, int CompressedLength, int UncompressedLength, ReadOnlyMemory<byte> CompressedData)
			{
				this.Hash = Hash;
				this.Blob = Blob;
				this.Offset = Offset;
				this.CompressedLength = CompressedLength;
				this.UncompressedLength = UncompressedLength;
				this.CompressedData = CompressedData;
			}

			public override string ToString() => $"{Hash}";
		}

		/// <summary>
		/// Information about a blob in memory. 
		/// </summary>
		class BlobInfo
		{
			public IoHash Hash { get; }
			public DateTime Time;
			public int Length;
			public bool IsLeaf;
			public ReadOnlyMemory<byte> Data;

			// Temp for computing live set
			public HashSet<BlobInfo> ReferencedBy = new HashSet<BlobInfo>();

			public BlobInfo(IoHash Hash, DateTime Time, int Length, bool IsLeaf)
			{
				this.Hash = Hash;
				this.Time = Time;
				this.Length = Length;
				this.IsLeaf = IsLeaf;
			}

			public override string ToString() => IsLeaf ? $"{Hash} (Leaf)" : $"{Hash}";
		}

		IStorageClient StorageClient { get; }
		NamespaceId NamespaceId { get; }

		/// <summary>
		/// 
		/// </summary>
		public TreePackOptions Options { get; }
		Dictionary<IoHash, NodeInfo> Nodes = new Dictionary<IoHash, NodeInfo>();
		Dictionary<IoHash, BlobInfo> Blobs = new Dictionary<IoHash, BlobInfo>();

		List<NodeInfo> LeafNodes = new List<NodeInfo>();
		long LeafNodesSize = 0;

		/// <summary>
		/// Constructor
		/// </summary>
		public TreePack(IStorageClient StorageClient, NamespaceId NamespaceId)
			: this(StorageClient, NamespaceId, new TreePackOptions())
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public TreePack(IStorageClient StorageClient, NamespaceId NamespaceId, TreePackOptions Options)
		{
			this.StorageClient = StorageClient;
			this.NamespaceId = NamespaceId;
			this.Options = Options;
		}

		/// <summary>
		/// Adds a root blob to the pack
		/// </summary>
		/// <param name="Ref"></param>
		/// <returns>Hash of the root node</returns>
		public ReadOnlyMemory<byte> AddRootBlob(IRef Ref) => AddRootBlob(Ref.Value.GetView());

		/// <summary>
		/// Adds a root blob to the pack
		/// </summary>
		/// <param name="Data"></param>
		/// <returns>Hash of the root node</returns>
		public ReadOnlyMemory<byte> AddRootBlob(ReadOnlyMemory<byte> Data)
		{
			TreePackObject Object = CbSerializer.Deserialize<TreePackObject>(Data);
			RegisterObject(Object, null);

			TreePackExport Export = new TreePackExport(Object.Exports.Span);
			return UncompressData(Object.Data.Slice(Export.Offset, Export.CompressedLength), Export.UncompressedLength);
		}

		/// <summary>
		/// Adds a root blob to the pack
		/// </summary>
		/// <param name="Object"></param>
		/// <returns>Hash of the root node</returns>
		public void AddRootObject(TreePackObject Object)
		{
			RegisterObject(Object, null);
		}

		/// <summary>
		/// Adds a root object to the pack
		/// </summary>
		/// <param name="Ref"></param>
		/// <returns></returns>
		public TreePackObject AddRootObject(IRef Ref)
		{
			TreePackObject Object = TreePackObject.Parse(Ref);
			AddRootObject(Object);
			return Object;
		}

		/// <summary>
		/// Gets references from a node to other nodes
		/// </summary>
		/// <param name="Data"></param>
		/// <returns></returns>
		private static IEnumerable<IoHash> GetReferences(ReadOnlyMemory<byte> Data)
		{
			TreePackNodeType Type = (TreePackNodeType)Data.Span[0];
			switch (Type)
			{
				case TreePackNodeType.Binary:
					return Enumerable.Empty<IoHash>();
				case TreePackNodeType.Object:
					HashSet<IoHash> Hashes = new HashSet<IoHash>();
					new CbField(Data.Slice(1)).IterateAttachments(x => Hashes.Add(x.AsAttachment()));
					return Hashes;
				case TreePackNodeType.Concat:
					return TreePackConcatNode.Parse(Data.Span).Entries.Select(x => x.Hash);
				case TreePackNodeType.Directory:
					return TreePackDirNode.Parse(Data).Entries.Select(x => x.Hash);
				default:
					throw new NotImplementedException();
			}
		}

		BlobInfo FindOrAddBlob(IoHash Hash, DateTime UtcNow, int Length, bool IsLeaf)
		{
			BlobInfo? BlobInfo;
			if (!Blobs.TryGetValue(Hash, out BlobInfo))
			{
				BlobInfo = new BlobInfo(Hash, UtcNow, Length, IsLeaf);
				Blobs.Add(Hash, BlobInfo);
			}
			return BlobInfo;
		}

		NodeInfo FindOrAddNode(IoHash Hash, int Offset, int CompressedLength, int UncompressedLength, BlobInfo? BlobInfo)
		{
			NodeInfo? NodeInfo;
			if (!Nodes.TryGetValue(Hash, out NodeInfo))
			{
				NodeInfo = new NodeInfo(Hash, BlobInfo, Offset, CompressedLength, UncompressedLength, ReadOnlyMemory<byte>.Empty);
				Nodes.Add(Hash, NodeInfo);
			}
			return NodeInfo;
		}

		/// <summary>
		/// Adds information about an object to the store
		/// </summary>
		/// <param name="RootObject"></param>
		public void AddNodes(TreePackObject RootObject)
		{
			RegisterObject(RootObject, null);
		}

		/// <summary>
		/// Adds a new node to pack
		/// </summary>
		/// <param name="Data">Node data</param>
		public async Task<IoHash> AddNodeAsync(ReadOnlyMemory<byte> Data)
		{
			IoHash Hash = IoHash.Compute(Data.Span);
			await AddNodeAsync(Hash, Data);
			return Hash;
		}

		/// <summary>
		/// Compresses the given node data 
		/// </summary>
		/// <param name="Data"></param>
		/// <returns></returns>
		public static ReadOnlyMemory<byte> CompressData(ReadOnlyMemory<byte> Data)
		{
			byte[] Output = new byte[LZ4Codec.MaximumOutputSize(Data.Length)];
			int Length = LZ4Codec.Encode(Data.Span, Output);
			return Output.AsMemory(0, Length);
		}

		/// <summary>
		/// Uncompresses the given node data
		/// </summary>
		/// <param name="Data"></param>
		/// <param name="UncompressedLength">Size of the uncompressed data</param>
		/// <returns></returns>
		public static byte[] UncompressData(ReadOnlyMemory<byte> Data, int UncompressedLength)
		{
			byte[] Output = new byte[UncompressedLength];
			LZ4Codec.Decode(Data.Span, Output);
			return Output;
		}

		/// <summary>
		/// Adds a new node to pack
		/// </summary>
		/// <param name="Hash">Hash for the node</param>
		/// <param name="Data">Node data</param>
		public async Task AddNodeAsync(IoHash Hash, ReadOnlyMemory<byte> Data)
		{
			if (!Nodes.ContainsKey(Hash))
			{
				ReadOnlyMemory<byte> CompressedData = CompressData(Data);

				NodeInfo Node = new NodeInfo(Hash, null, -1, CompressedData.Length, Data.Length, CompressedData);
				if (!GetReferences(Data).Any())
				{
					if (LeafNodesSize + CompressedData.Length > Options.MaxBlobSize)
					{
						await WriteObjectAsync(LeafNodes, DateTime.UtcNow);
						LeafNodes.Clear();
						LeafNodesSize = 0;
					}

					LeafNodes.Add(Node);
					LeafNodesSize += CompressedData.Length;
				}
				Nodes.Add(Hash, Node);
			}
		}

		/// <summary>
		/// Gets data for the given node
		/// </summary>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public ValueTask<ReadOnlyMemory<byte>> GetDataAsync(IoHash Hash)
		{
			NodeInfo Node = Nodes[Hash];
			return GetDataAsync(Node);
		}

		/// <summary>
		/// Gets data for the given node
		/// </summary>
		/// <param name="Node"></param>
		/// <returns></returns>
		private async ValueTask<ReadOnlyMemory<byte>> GetDataAsync(NodeInfo Node)
		{
			if (Node.CompressedData.Length == 0 && Node.Blob != null)
			{
				BlobInfo? Blob = Node.Blob;
				if (Blob.Data.Length == 0)
				{
					Blob.Data = await StorageClient.ReadBlobToMemoryAsync(NamespaceId, Blob.Hash);
				}

				if (Node.Blob.IsLeaf)
				{
					ReadOnlyMemory<byte> BlobData = Node.Blob.Data;
					Node.CompressedData = BlobData.Slice(Node.Offset, Node.CompressedLength);
				}
				else
				{
					TreePackObject Object = CbSerializer.Deserialize<TreePackObject>(Node.Blob.Data);
					RegisterObject(Object, Node.Blob);
				}
			}
			return UncompressData(Node.CompressedData, Node.UncompressedLength);
		}

		/// <summary>
		/// Writes the pack file to storage, and set a reference to it
		/// </summary>
		/// <param name="BucketId"></param>
		/// <param name="RefId"></param>
		/// <param name="RootHash"></param>
		/// <param name="UtcNow"></param>
		/// <returns></returns>
		public async Task WriteAsync(BucketId BucketId, RefId RefId, IoHash RootHash, DateTime UtcNow)
		{
			TreePackObject Object = await FlushAsync(RootHash, UtcNow);
			await StorageClient.SetRefAsync(NamespaceId, BucketId, RefId, Object);
		}

		/// <summary>
		/// Flush any pending blobs to disk
		/// </summary>
		/// <param name="RootHash">New root key</param>
		/// <param name="UtcNow">Timestamp for any new blobs</param>
		public async Task<TreePackObject> FlushAsync(IoHash RootHash, DateTime UtcNow)
		{
			// Reset all the current values
			foreach (NodeInfo Node in Nodes.Values)
			{
				Node.Rank = -1;
			}

			// Ensure that the root object is written out as part of the output
			NodeInfo RootNode = Nodes[RootHash];

			// Find the live set of objects
			HashSet<NodeInfo> LiveNodes = new HashSet<NodeInfo>();
			await FindLiveSetAsync(RootNode, LiveNodes);

			// Find the live set of blobs
			HashSet<BlobInfo> LiveBlobs = new HashSet<BlobInfo>();
			foreach (NodeInfo LiveNode in LiveNodes)
			{
				BlobInfo? LiveBlob = LiveNode.Blob;
				if (LiveBlob != null)
				{
					ExpandReferences(LiveBlob, LiveBlobs);
				}
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

				// Find all the live nodes in this blob
				List<NodeInfo> MergeNodes = LiveNodes.Where(x => x.Blob == MergeBlob).ToList();

				// If it's a leaf node, we'll need to merge it in
				if (MergeBlob.IsLeaf)
				{
					byte[] Data = await StorageClient.ReadBlobToMemoryAsync(NamespaceId, MergeBlob.Hash);
					foreach (NodeInfo MergeNode in MergeNodes)
					{
						MergeNode.CompressedData = Data.AsMemory(MergeNode.Offset, MergeNode.CompressedLength).ToArray();
					}
				}

				// Read them all in and add them to the live set
				foreach (NodeInfo NodeInfo in MergeNodes)
				{
					NodeInfo.Blob = null;
				}

				// Remove the merged blob from the live list
				LiveBlobs.Remove(MergeBlob);
			}

			// Clear the list of leaf nodes, since they're all being flushed anyway
			LeafNodes.Clear();
			LeafNodesSize = 0;

			// Write all the new blobs. Sort by rank and go from end to start, updating the list as we go so that we can figure out imports correctly.
			NodeInfo[] WriteNodes = LiveNodes.Where(x => x.Blob == null).OrderBy(x => x.Rank).ToArray();

			int Length = WriteNodes.Length;

			int NextBlobSize = WriteNodes[WriteNodes.Length - 1].CompressedLength;
			for (int Idx = WriteNodes.Length - 2; Idx >= 1; Idx--)
			{
				NextBlobSize += WriteNodes[Idx].CompressedLength;
				if (NextBlobSize > Options.MaxBlobSize)
				{
					await WriteObjectAsync(WriteNodes.Slice(Idx + 1, Length - (Idx + 1)), UtcNow);
					NextBlobSize = WriteNodes[Idx].CompressedLength;
					Length = Idx + 1;
				}
			}

			return BuildObject(WriteNodes.Slice(0, Length), UtcNow);
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

		/// <summary>
		/// Finds the live set for a particular tree, and updates tree entries with the size of used items within them
		/// </summary>
		/// <param name="Node"></param>
		/// <param name="LiveNodes"></param>
		async Task FindLiveSetAsync(NodeInfo Node, HashSet<NodeInfo> LiveNodes)
		{
			if (LiveNodes.Add(Node))
			{
				int MaxRank = 0;
				if (Node.Blob == null || !Node.Blob.IsLeaf)
				{
					ReadOnlyMemory<byte> Data = await GetDataAsync(Node.Hash);
					foreach (IoHash RefHash in GetReferences(Data))
					{
						NodeInfo RefNode = Nodes[RefHash];
						await FindLiveSetAsync(RefNode, LiveNodes);

						if (Node.Blob != null && RefNode.Blob != null)
						{
							RefNode.Blob.ReferencedBy.Add(Node.Blob);
						}

						MaxRank = Math.Max(MaxRank, Node.Rank + 1);
					}
				}
				Node.Rank = MaxRank;
			}
		}

		void RegisterObject(TreePackObject Object, BlobInfo? BlobInfo)
		{
			// Register all the exports from the object
			foreach (TreePackExport Export in ReadExports(Object.Exports))
			{
				NodeInfo NodeInfo = FindOrAddNode(Export.Hash, Export.Offset, Export.CompressedLength, Export.UncompressedLength, BlobInfo);
				NodeInfo.UncompressedLength = Export.UncompressedLength;
				NodeInfo.CompressedLength = Export.CompressedLength;
				NodeInfo.CompressedData = Object.Data.Slice(Export.Offset, Export.CompressedLength);
			}

			// Register all the object imports
			foreach (TreePackObjectImport Import in Object.ObjectImports)
			{
				BlobInfo ImportedBlob = FindOrAddBlob(Import.Object.Hash, DateTime.MinValue, 0, false);

				IoHash[] Keys = ReadKeys(Import.Keys);
				foreach (IoHash Key in Keys)
				{
					FindOrAddNode(Key, -1, -1, -1, ImportedBlob);
				}
			}

			// Register all the binary imports
			foreach (TreePackBinaryImport Import in Object.BinaryImports)
			{
				BlobInfo ImportedBlob = FindOrAddBlob(Import.Binary.Hash, Import.Time, Import.Length, true);

				TreePackExport[] Exports = ReadExports(Import.Exports);
				foreach (TreePackExport Export in Exports)
				{
					FindOrAddNode(Export.Hash, Export.Offset, Export.CompressedLength, Export.UncompressedLength, ImportedBlob);
				}
			}
		}

		TreePackObject BuildObject(IReadOnlyList<NodeInfo> Nodes, DateTime UtcNow)
		{
			byte[] Data = new byte[Nodes.Sum(x => x.CompressedLength)];
			int NextOffset = 0;

			HashSet<NodeInfo> References = new HashSet<NodeInfo>();
			foreach (NodeInfo Node in Nodes)
			{
				Memory<byte> NodeData = Data.AsMemory(NextOffset, Node.CompressedLength);
				Node.CompressedData.CopyTo(NodeData);

				Node.Offset = NextOffset;
				Node.CompressedData = NodeData;

				NextOffset += Node.CompressedLength;

				byte[] OutputData = UncompressData(Node.CompressedData, Node.UncompressedLength);
				foreach (IoHash Hash in GetReferences(OutputData))
				{
					NodeInfo? RefNode;
					if (this.Nodes.TryGetValue(Hash, out RefNode) && RefNode.Blob != null)
					{
						References.Add(RefNode);
					}
				}
			}

			TreePackObject Object = new TreePackObject();
			Object.Time = UtcNow;
			Object.Data = Data;
			Object.Exports = WriteExports(Nodes.Select(x => new TreePackExport(x.Hash, x.Offset, x.CompressedLength, x.UncompressedLength)).ToArray());

			foreach (IGrouping<BlobInfo, NodeInfo> Group in References.GroupBy(x => x.Blob!))
			{
				BlobInfo ImportBlob = Group.Key;
				if (ImportBlob.IsLeaf)
				{
					TreePackBinaryImport BinaryImport = new TreePackBinaryImport();
					BinaryImport.Binary = new CbBinaryAttachment(ImportBlob.Hash);
					BinaryImport.Time = ImportBlob.Time;
					BinaryImport.Length = ImportBlob.Length;
					BinaryImport.Exports = WriteExports(Group.Select(x => new TreePackExport(x.Hash, x.Offset, x.CompressedLength, x.UncompressedLength)).ToArray());
					Object.BinaryImports.Add(BinaryImport);
				}
				else
				{
					TreePackObjectImport ObjectImport = new TreePackObjectImport();
					ObjectImport.Object = new CbObjectAttachment(ImportBlob.Hash);
					ObjectImport.Keys = WriteKeys(Group.Select(x => x.Hash).ToArray());
					Object.ObjectImports.Add(ObjectImport);
				}
			}

			return Object;
		}

		async Task<BlobInfo> WriteObjectAsync(IReadOnlyList<NodeInfo> Nodes, DateTime UtcNow)
		{
			TreePackObject Object = BuildObject(Nodes, UtcNow);

			BlobInfo Blob;
			if (Object.ObjectImports.Count == 0 && Object.BinaryImports.Count == 0)
			{
				IoHash Hash = await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, Object.Data);
				Blob = new BlobInfo(Hash, Object.Time, Object.Data.Length, true);
			}
			else
			{
				IoHash Hash = await StorageClient.WriteObjectAsync(NamespaceId, Object);
				Blob = new BlobInfo(Hash, Object.Time, Object.Data.Length, false);
			}

			foreach (NodeInfo Node in Nodes)
			{
				Node.Blob = Blob;
				Node.CompressedData = ReadOnlyMemory<byte>.Empty;
			}

			Blobs[Blob.Hash] = Blob;
			return Blob;
		}

		static IoHash[] ReadKeys(ReadOnlyMemory<byte> KeyData)
		{
			int NumEntries = KeyData.Length / IoHash.NumBytes;

			IoHash[] Keys = new IoHash[NumEntries];

			ReadOnlySpan<byte> Span = KeyData.Span;
			for (int Idx = 0; Idx < NumEntries; Idx++)
			{
				Keys[Idx] = new IoHash(Span);
				Span = Span.Slice(IoHash.NumBytes);
			}

			return Keys;
		}

		static byte[] WriteKeys(IoHash[] Keys)
		{
			byte[] Data = new byte[Keys.Length * IoHash.NumBytes];

			Span<byte> Output = Data;
			foreach (IoHash Key in Keys)
			{
				Key.CopyTo(Output);
				Output = Output.Slice(IoHash.NumBytes);
			}

			return Data;
		}

		static TreePackExport[] ReadExports(ReadOnlyMemory<byte> ExportData)
		{
			int NumEntries = ExportData.Length / (IoHash.NumBytes + sizeof(int) + sizeof(int) + sizeof(int));

			TreePackExport[] Exports = new TreePackExport[NumEntries];

			ReadOnlySpan<byte> Span = ExportData.Span;
			for (int Idx = 0; Idx < NumEntries; Idx++)
			{
				Exports[Idx] = new TreePackExport(Span);
				Span = Span.Slice(TreePackExport.EncodedLength);
			}

			return Exports;
		}

		static byte[] WriteExports(TreePackExport[] Exports)
		{
			byte[] Data = new byte[Exports.Length * (IoHash.NumBytes + sizeof(int) + sizeof(int) + sizeof(int))];

			Span<byte> Output = Data;
			foreach (TreePackExport Export in Exports.OrderBy(x => x.Offset))
			{
				Export.CopyTo(Output);
				Output = Output.Slice(TreePackExport.EncodedLength);
			}

			return Data;
		}

		/// <inheritdoc cref="GetCostHeuristic(int, TimeSpan)"/>
		double GetCostHeuristic(BlobInfo Info, DateTime UtcNow) => GetCostHeuristic(Info.Length, UtcNow - Info.Time);

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
					if (BlobSize > 0 && BlobSize + Node.CompressedLength > Options.MaxBlobSize)
					{
						Cost += GetCostHeuristic(BlobSize, TimeSpan.Zero);
						BlobSize = 0;
					}
					BlobSize += Node.CompressedLength;
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

		/// <summary>
		/// 
		/// </summary>
		/// <param name="RootHash"></param>
		/// <param name="Directory"></param>
		/// <param name="Logger"></param>
		/// <returns></returns>
		public async Task WriteTreeSummaryAsync(IoHash RootHash, DirectoryReference Directory, ILogger Logger)
		{
			DirectoryReference.CreateDirectory(Directory);

			HashSet<NodeInfo> FoundNodes = new HashSet<NodeInfo>();
			await FindNodesAsync(RootHash, FoundNodes);

			HashSet<BlobInfo> FoundBlobs = new HashSet<BlobInfo>();
			foreach (NodeInfo FoundNode in FoundNodes)
			{
				BlobInfo? FoundBlob = FoundNode.Blob;
				if (FoundBlob != null && FoundBlobs.Add(FoundBlob) && !FoundBlob.IsLeaf)
				{
					TreePackObject Object = TreePackObject.Parse(FoundBlob.Data);

					FileReference OutputFile = FileReference.Combine(Directory, $"{FoundBlob.Hash}.yml");
					Logger.LogInformation("Writing summary: {File}", OutputFile);

					WriteSummary(OutputFile, Object);
				}
			}
		}

		async Task FindNodesAsync(IoHash Hash, HashSet<NodeInfo> FoundNodes)
		{
			NodeInfo Node = Nodes[Hash];
			if (FoundNodes.Add(Node) && (Node.Blob == null || !Node.Blob.IsLeaf))
			{
				ReadOnlyMemory<byte> Data = await GetDataAsync(Node.Hash);
				foreach (IoHash RefHash in GetReferences(Data))
				{
					await FindNodesAsync(RefHash, FoundNodes);
				}
			}
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="File"></param>
		/// <param name="Object"></param>
		public static void WriteSummary(FileReference File, TreePackObject Object)
		{
			Dictionary<IoHash, string> HashToLocator = new Dictionary<IoHash, string>();
			foreach (TreePackObjectImport ObjectImport in Object.ObjectImports)
			{
				foreach (IoHash Key in ReadKeys(ObjectImport.Keys))
				{
					HashToLocator[Key] = $"{ObjectImport.Object}";
				}
			}
			foreach (TreePackBinaryImport BinaryImport in Object.BinaryImports)
			{
				foreach (TreePackExport Export in ReadExports(BinaryImport.Exports))
				{
					HashToLocator[Export.Hash] = $"{BinaryImport.Binary} [{Export.Offset},{Export.CompressedLength}]";
				}
			}

			using StreamWriter Writer = new StreamWriter(FileReference.Open(File, FileMode.Create));

			Writer.WriteLine("time: {0}", Object.Time.ToString("u"));

			if (Object.ObjectImports.Count > 0)
			{
				Writer.WriteLine("objimp:");
				foreach (TreePackObjectImport ObjectImport in Object.ObjectImports)
				{
					Writer.WriteLine($"- blob: {ObjectImport.Object.Hash}");
					Writer.WriteLine("  keys:");
					foreach (IoHash Key in ReadKeys(ObjectImport.Keys))
					{
						Writer.WriteLine($"  - \"{Key}\"");
					}
					Writer.WriteLine();
				}
			}

			if (Object.BinaryImports.Count > 0)
			{
				Writer.WriteLine("binimp:");
				foreach (TreePackBinaryImport BinaryImport in Object.BinaryImports)
				{
					Writer.WriteLine($"- blob: {BinaryImport.Binary.Hash}");
					Writer.WriteLine("  keys:");
					foreach (TreePackExport Export in ReadExports(BinaryImport.Exports))
					{
						Writer.WriteLine($"  - \"{Export.Hash}\"");
					}
					Writer.WriteLine();
				}
			}

			Writer.WriteLine("exports:");

			TreePackExport[] Exports = ReadExports(Object.Exports);
			foreach (TreePackExport Export in Exports)
			{
				ReadOnlyMemory<byte> Data = UncompressData(Object.Data.Slice(Export.Offset, Export.CompressedLength), Export.UncompressedLength);
				TreePackNodeType Type = (TreePackNodeType)Data.Span[0];

				Writer.WriteLine($"- hash: {Export.Hash}");
				Writer.WriteLine($"  type: {Type.ToString()}");

				if (Type == TreePackNodeType.Concat)
				{
					Writer.WriteLine("  entries:");

					TreePackConcatNode Node = TreePackConcatNode.Parse(Data.Span);
					foreach (TreePackConcatEntry Entry in Node.Entries)
					{
						Writer.WriteLine($"    - hash: \"{Entry.Hash}\"");
						HashToLocator.TryGetValue(Entry.Hash, out string? Locator);
						Writer.WriteLine($"      from: \"{Locator ?? "current"}\"");
					}

					Writer.WriteLine();
				}
				else if (Type == TreePackNodeType.Directory)
				{
					Writer.WriteLine("  entries:");

					TreePackDirNode Node = TreePackDirNode.Parse(Data);
					foreach (TreePackDirEntry Entry in Node.Entries)
					{
						Writer.WriteLine($"    - name: \"{Entry.Name}\"");
						Writer.WriteLine($"      type: {Entry.Flags}");
						Writer.WriteLine($"      hash: \"{Entry.Hash}\"");

						HashToLocator.TryGetValue(Entry.Hash, out string? Locator);
						Writer.WriteLine($"      from: \"{Locator ?? "current"}\"");

						Writer.WriteLine();
					}
				}
				else
				{
					Writer.WriteLine("  length: {0}", Export.UncompressedLength);
					Writer.WriteLine();
				}
			}
		}
	}
}
