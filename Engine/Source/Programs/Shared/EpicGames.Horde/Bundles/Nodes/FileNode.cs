// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Bundles.Nodes
{
	/// <summary>
	/// Options for creating file nodes
	/// </summary>
	public class ChunkingOptions
	{
		/// <summary>
		/// Options for creating leaf nodes
		/// </summary>
		public ChunkingOptionsForNodeType LeafOptions { get; set; }

		/// <summary>
		/// Options for creating interior nodes
		/// </summary>
		public ChunkingOptionsForNodeType InteriorOptions { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public ChunkingOptions()
		{
			LeafOptions = new ChunkingOptionsForNodeType(32 * 1024, 256 * 1024, 64 * 1024);
			InteriorOptions = new ChunkingOptionsForNodeType(32 * 1024, 256 * 1024, 64 * 1024);
		}
	}

	/// <summary>
	/// Options for creating a specific type of file nodes
	/// </summary>
	public class ChunkingOptionsForNodeType
	{
		/// <summary>
		/// Minimum chunk size
		/// </summary>
		public int MinSize { get; set; }

		/// <summary>
		/// Maximum chunk size. Chunks will be split on this boundary if another match is not found.
		/// </summary>
		public int MaxSize { get; set; }

		/// <summary>
		/// Target chunk size for content-slicing
		/// </summary>
		public int TargetSize { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public ChunkingOptionsForNodeType(int MinSize, int MaxSize, int TargetSize)
		{
			this.MinSize = MinSize;
			this.MaxSize = MaxSize;
			this.TargetSize = TargetSize;
		}
	}

	/// <summary>
	/// Representation of a data stream, split into chunks along content-aware boundaries using a rolling hash (<see cref="BuzHash"/>).
	/// Chunks are pushed into a tree hierarchy as data is appended to the root, with nodes of the tree also split along content-aware boundaries with <see cref="IoHash.NumBytes"/> granularity.
	/// Once a chunk has been written to storage, it is treated as immutable.
	/// </summary>
	[BundleNodeDeserializer(typeof(FileNodeDeserializer))]
	public sealed class FileNode : BundleNode
	{
		const byte TypeId = (byte)'c';

		internal int Depth { get; private set; }
		internal ReadOnlyMemory<byte> Payload { get; private set; }
		ReadOnlyMemory<byte> Data; // Full serialized data, including type id and header fields.
		IoHash? Hash; // Hash of the serialized data buffer.

		// In-memory state
		bool IsRoot = true;
		uint RollingHash;
		byte[] WriteBuffer = Array.Empty<byte>();
		List<BundleNodeRef<FileNode>>? ChildNodeRefs;

		/// <summary>
		/// Length of this tree
		/// </summary>
		public long Length { get; private set; }

		/// <summary>
		/// Whether this node is read-only
		/// </summary>
		public bool IsComplete() => Hash != null;

		/// <summary>
		/// Accessor for the children of this node
		/// </summary>
		public IReadOnlyList<BundleNodeRef<FileNode>> Children => (IReadOnlyList<BundleNodeRef<FileNode>>?)ChildNodeRefs ?? Array.Empty<BundleNodeRef<FileNode>>();

		/// <inheritdoc/>
		public override ReadOnlyMemory<byte> Serialize()
		{
			if (ChildNodeRefs != null)
			{
				Span<byte> Span = GetWritableSpan(VarInt.Measure((ulong)Length) + ChildNodeRefs.Count * IoHash.NumBytes, 0);

				int LengthBytes = VarInt.Write(Span, Length);
				Span = Span.Slice(LengthBytes);

				foreach (BundleNodeRef<FileNode> ChildNodeRef in ChildNodeRefs)
				{
					ChildNodeRef.Hash.CopyTo(Span);
					Span = Span.Slice(IoHash.NumBytes);
				}
			}
			return Data;
		}

		/// <inheritdoc/>
		public override IEnumerable<BundleNodeRef> GetReferences() => Children;

		/// <summary>
		/// Create a file node from deserialized data
		/// </summary>
		public static FileNode Deserialize(Bundle Bundle, ReadOnlyMemory<byte> Data)
		{
			ReadOnlySpan<byte> Span = Data.Span;
			if (Span[0] != TypeId)
			{
				throw new InvalidDataException("Invalid type id");
			}

			FileNode Node = new FileNode();
			Node.Data = Data;
			Node.Hash = IoHash.Compute(Data.Span);
			Node.Depth = (int)VarInt.Read(Span.Slice(1), out int DepthBytes);

			int HeaderLength = 1 + DepthBytes;
			Node.Payload = Data.Slice(HeaderLength);

			if (Node.Depth == 0)
			{
				Node.Length = Node.Payload.Length;
			}
			else
			{
				Node.Length = (long)VarInt.Read(Span, out int LengthBytes);
				Span = Span.Slice(LengthBytes);

				Node.ChildNodeRefs = new List<BundleNodeRef<FileNode>>(Node.Payload.Length / IoHash.NumBytes);

				Span = Span.Slice(HeaderLength);
				while (Span.Length > 0)
				{
					IoHash ChildHash = new IoHash(Span);
					Node.ChildNodeRefs.Add(new BundleNodeRef<FileNode>(Bundle, ChildHash));
				}
			}
			return Node;
		}

		/// <summary>
		/// Gets a writable span of the given size.
		/// </summary>
		/// <param name="SpanSize">Required size of the writable span</param>
		/// <param name="HintMaxPayloadSize">Hint for the maximum size of the payload, to avoid having to reallocate the buffer. May be zero if unknown.</param>
		/// <returns></returns>
		private Span<byte> GetWritableSpan(int SpanSize, int HintMaxPayloadSize)
		{
			Debug.Assert(!IsComplete());

			int HeaderLength = 1 + VarInt.Measure(Depth);
			int PrevPayloadLength = Payload.Length;

			int MinWriteBufferSize = HeaderLength + Math.Max(PrevPayloadLength + SpanSize, HintMaxPayloadSize);
			if (MinWriteBufferSize > WriteBuffer.Length)
			{
				byte[] NewWriteBuffer = new byte[MinWriteBufferSize];

				NewWriteBuffer[0] = TypeId;
				VarInt.Write(NewWriteBuffer.AsSpan(1), Depth);
				Payload.Span.CopyTo(NewWriteBuffer.AsSpan(HeaderLength));

				WriteBuffer = NewWriteBuffer;
			}

			Data = WriteBuffer.AsMemory(0, HeaderLength + PrevPayloadLength + SpanSize);
			Payload = Data.Slice(HeaderLength);

			return WriteBuffer.AsSpan(HeaderLength + PrevPayloadLength, SpanSize);
		}

		/// <summary>
		/// Serialize this node and its children into a byte array
		/// </summary>
		/// <returns>Array of data stored by the tree</returns>
		public async Task<byte[]> ToByteArrayAsync()
		{
			using (MemoryStream Stream = new MemoryStream())
			{
				await CopyToStreamAsync(Stream);
				return Stream.ToArray();
			}
		}

		/// <summary>
		/// Copy data from the given stream into this file node
		/// </summary>
		/// <param name="InputStream"></param>
		/// <param name="Options"></param>
		/// <returns></returns>
		public async Task CopyFromStreamAsync(Stream InputStream, ChunkingOptions Options)
		{
			byte[] Buffer = new byte[64 * 1024];
			for (; ; )
			{
				int ReadSize = await InputStream.ReadAsync(Buffer);
				if(ReadSize == 0)
				{
					break;
				}
				Append(Buffer.AsMemory(0, ReadSize), Options);
			}
		}

		/// <summary>
		/// Copies the contents of this file from disk
		/// </summary>
		/// <param name="File"></param>
		/// <param name="Options"></param>
		/// <returns></returns>
		public async Task CopyFromFileAsync(FileInfo File, ChunkingOptions Options)
		{
			using (FileStream Stream = File.OpenRead())
			{
				await CopyFromStreamAsync(Stream, Options);
			}
		}

		/// <summary>
		/// Copies the contents of this node and its children to the given output stream
		/// </summary>
		/// <param name="OutputStream">The output stream to receive the data</param>
		public async Task CopyToStreamAsync(Stream OutputStream)
		{
			if (ChildNodeRefs != null)
			{
				foreach (BundleNodeRef<FileNode> ChildNodeRef in ChildNodeRefs)
				{
					FileNode ChildNode = await ChildNodeRef.GetAsync();
					await ChildNode.CopyToStreamAsync(OutputStream);
				}
			}
			else
			{
				await OutputStream.WriteAsync(Payload);
			}
		}

		/// <summary>
		/// Extracts the contents of this node to a file
		/// </summary>
		/// <param name="File">File to write with the contents of this node</param>
		/// <returns></returns>
		public async Task CopyToFileAsync(FileInfo File)
		{
			using (FileStream Stream = File.OpenWrite())
			{
				await CopyToStreamAsync(Stream);
			}
		}

		/// <summary>
		/// Append data to this chunk. Must only be called on the root node in a chunk tree.
		/// </summary>
		/// <param name="Input">The data to write</param>
		/// <param name="Options">Settings for chunking the data</param>
		public void Append(ReadOnlyMemory<byte> Input, ChunkingOptions Options)
		{
			if (!IsRoot)
			{
				throw new InvalidOperationException("Data may only be appended to the root of a file node tree");
			}

			for (; ; )
			{
				// Append as much data as possible to the existing tree
				Input = AppendToNode(Input, Options);
				if (Input.IsEmpty)
				{
					break;
				}

				// Increase the height of the tree by pushing the contents of this node into a new child node
				FileNode NewNode = new FileNode();
				NewNode.IsRoot = false;
				NewNode.Depth = Depth;
				NewNode.Payload = Payload;
				NewNode.Data = Data;
				NewNode.ChildNodeRefs = ChildNodeRefs;

				// Detach all the child refs
				if (ChildNodeRefs != null)
				{
					foreach (BundleNodeRef<FileNode> ChildNodeRef in ChildNodeRefs)
					{
						ChildNodeRef.Detach();
					}
					ChildNodeRefs = null;
				}

				// Increase the depth and reset the current node
				Depth++;
				ChildNodeRefs = new List<BundleNodeRef<FileNode>>();
				Payload = ReadOnlyMemory<byte>.Empty;
				Data = ReadOnlyMemory<byte>.Empty;
				WriteBuffer = Array.Empty<byte>();

				// Append the new node as a child
				ChildNodeRefs.Add(new BundleNodeRef<FileNode>(NewNode));
			}
		}

		private ReadOnlyMemory<byte> AppendToNode(ReadOnlyMemory<byte> Data, ChunkingOptions Options)
		{
			if (Data.Length == 0 || IsComplete())
			{
				return Data;
			}

			if (Depth == 0)
			{
				return AppendToLeafNode(Data, Options);
			}
			else
			{
				return AppendToInteriorNode(Data, Options);
			}
		}

		private ReadOnlyMemory<byte> AppendToLeafNode(ReadOnlyMemory<byte> NewData, ChunkingOptions Options)
		{
			// Fast path for appending data to the buffer up to the chunk window size
			int WindowSize = Options.LeafOptions.MinSize;
			if (Payload.Length < WindowSize)
			{
				int AppendLength = Math.Min(WindowSize - Payload.Length, NewData.Length);
				AppendLeafData(NewData.Span.Slice(0, AppendLength), Options);
				NewData = NewData.Slice(AppendLength);
			}

			// Cap the maximum amount of data to append to this node
			int MaxLength = Math.Min(NewData.Length, Options.LeafOptions.MaxSize - Payload.Length);
			if (MaxLength > 0)
			{
				int AppendLength = HashScan(NewData.Span.Slice(0, MaxLength), Options);
				AppendLeafData(NewData.Span.Slice(0, AppendLength), Options);
				NewData = NewData.Slice(AppendLength);
			}

			return NewData;
		}

		private int HashScan(ReadOnlySpan<byte> NewSpan, ChunkingOptions Options)
		{
			int WindowSize = Options.LeafOptions.MinSize;
			Debug.Assert(Payload.Length >= WindowSize);

			int Length = 0;

			// If this is the first time we're hashing the data, compute the hash of the existing window
			if (Payload.Length == WindowSize)
			{
				RollingHash = BuzHash.Add(RollingHash, Payload.Span);
			}

			// Get the threshold for the rolling hash
			uint RollingHashThreshold = (uint)((1L << 32) / Options.LeafOptions.TargetSize);

			// Get the remaining part of the payload span leading into the new data
			ReadOnlySpan<byte> PayloadSpan = Payload.Span.Slice(Payload.Length - WindowSize);

			// Step the window through the tail end of the existing payload window. In this state, update the hash to remove data from the current payload, and add data from the new payload.
			int SplitLength = Math.Min(NewSpan.Length, WindowSize);
			for (; Length < SplitLength; Length++)
			{
				RollingHash = BuzHash.Add(RollingHash, NewSpan[Length]);
				if (RollingHash < RollingHashThreshold)
				{
					return Length;
				}
				RollingHash = BuzHash.Sub(RollingHash, PayloadSpan[Length], WindowSize);
			}

			// Step through the new window.
			for (; Length < NewSpan.Length; Length++)
			{
				RollingHash = BuzHash.Add(RollingHash, NewSpan[Length]);
				if (RollingHash < RollingHashThreshold)
				{
					return Length;
				}
				RollingHash = BuzHash.Sub(RollingHash, NewSpan[Length - WindowSize], WindowSize);
			}

			return Length;
		}

		private int AppendInternal(ReadOnlySpan<byte> TailSpan, ReadOnlySpan<byte> HeadSpan, uint RollingHashThreshold, int WindowSize, int MaxLength, ChunkingOptions Options)
		{
			for (int Length = 0; Length < MaxLength; Length++)
			{
				RollingHash = BuzHash.Add(RollingHash, HeadSpan[Length]);

				if (RollingHash < RollingHashThreshold)
				{
					AppendLeafData(HeadSpan.Slice(0, Length), Options);
					Hash = IoHash.Compute(Data.Span);
					return Length;
				}

				RollingHash = BuzHash.Sub(RollingHash, TailSpan[Length], WindowSize);
			}
			return -1;
		}

		private void AppendLeafData(ReadOnlySpan<byte> Data, ChunkingOptions Options)
		{
			Span<byte> WriteSpan = GetWritableSpan(Data.Length, Options.LeafOptions.MaxSize);
			Data.CopyTo(WriteSpan);
			Length += Data.Length;
		}

		private ReadOnlyMemory<byte> AppendToInteriorNode(ReadOnlyMemory<byte> NewData, ChunkingOptions Options)
		{
			for (; ; )
			{
				Debug.Assert(ChildNodeRefs != null);

				// Try to write to the last node
				if (ChildNodeRefs.Count > 0)
				{
					FileNode? LastNode = ChildNodeRefs[^1].Node;
					if (LastNode != null)
					{
						// Update the length to match the new node
						Length -= LastNode.Length;
						NewData = LastNode.AppendToNode(NewData, Options);
						Length += LastNode.Length;

						// If the last node is complete, write it to the buffer
						if (LastNode.IsComplete())
						{
							// Add it to the write buffer
							Span<byte> LastHashSpan = GetWritableSpan(IoHash.NumBytes, Options.InteriorOptions.MaxSize);
							LastNode.Hash!.Value.CopyTo(LastHashSpan);

							// Update the hash
							RollingHash = BuzHash.Add(RollingHash, LastHashSpan);

							// Check if it's time to finish this chunk
							uint HashThreshold = (uint)(((1L << 32) * IoHash.NumBytes) / Options.LeafOptions.TargetSize);
							if ((Payload.Length >= Options.InteriorOptions.MinSize && RollingHash < HashThreshold) || (Payload.Length >= Options.InteriorOptions.MaxSize))
							{
								Hash = IoHash.Compute(Data.Span);
								return NewData;
							}
						}

						// Bail out if there's nothing left to write
						if (NewData.Length == 0)
						{
							return NewData;
						}
					}
				}

				// Add a new child node
				FileNode ChildNode = new FileNode();
				ChildNode.IsRoot = false;
				ChildNodeRefs.Add(new BundleNodeRef<FileNode>(ChildNode));
			}
		}
	}

	/// <summary>
	/// Factory class for file nodes
	/// </summary>
	public class FileNodeDeserializer : BundleNodeDeserializer<FileNode>
	{
		/// <inheritdoc/>
		public override FileNode Deserialize(Bundle Bundle, ReadOnlyMemory<byte> Data) => FileNode.Deserialize(Bundle, Data);
	}
}
