// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Buffers;
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

		static readonly ReadOnlyMemory<byte> DefaultSegment = CreateFirstSegmentData(0, Array.Empty<byte>());

		class DataSegment : ReadOnlySequenceSegment<byte>
		{
			public DataSegment(long RunningIndex, ReadOnlyMemory<byte> Data)
			{
				this.RunningIndex = RunningIndex;
				this.Memory = Data;
			}

			public void SetNext(DataSegment Next)
			{
				this.Next = Next;
			}
		}

		internal int Depth { get; private set; }
		internal ReadOnlySequence<byte> Payload { get; private set; }
		ReadOnlySequence<byte> Data = new ReadOnlySequence<byte>(DefaultSegment); // Full serialized data, including type id and header fields.
		IoHash? Hash; // Hash of the serialized data buffer.

		// In-memory state
		bool IsRoot = true;
		uint RollingHash;
		DataSegment? FirstSegment;
		DataSegment? LastSegment;
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
		public override ReadOnlySequence<byte> Serialize()
		{
			MarkComplete();
			return Data;
		}

		/// <inheritdoc/>
		public override IEnumerable<BundleNodeRef> GetReferences() => Children;

		/// <summary>
		/// Create a file node from deserialized data
		/// </summary>
		public static FileNode Deserialize(ReadOnlyMemory<byte> Data)
		{
			ReadOnlySpan<byte> Span = Data.Span;
			if (Span[0] != TypeId)
			{
				throw new InvalidDataException("Invalid type id");
			}

			FileNode Node = new FileNode();
			Node.Data = new ReadOnlySequence<byte>(Data);
			Node.Hash = IoHash.Compute(Data.Span);
			Node.Depth = (int)VarInt.Read(Span.Slice(1), out int DepthBytes);

			int HeaderLength = 1 + DepthBytes;
			ReadOnlyMemory<byte> Payload = Data.Slice(HeaderLength);
			Node.Payload = new ReadOnlySequence<byte>(Payload);

			if (Node.Depth == 0)
			{
				Node.Length = Node.Payload.Length;
			}
			else
			{
				Node.Length = (long)VarInt.Read(Span, out int LengthBytes);
				Span = Span.Slice(LengthBytes);

				Node.ChildNodeRefs = new List<BundleNodeRef<FileNode>>(Payload.Length / IoHash.NumBytes);

				Span = Span.Slice(HeaderLength);
				while (Span.Length > 0)
				{
					IoHash ChildHash = new IoHash(Span);
					Node.ChildNodeRefs.Add(new BundleNodeRef<FileNode>(ChildHash));
				}
			}
			return Node;
		}

		/// <summary>
		/// Serialize this node and its children into a byte array
		/// </summary>
		/// <returns>Array of data stored by the tree</returns>
		public async Task<byte[]> ToByteArrayAsync(Bundle Bundle)
		{
			using (MemoryStream Stream = new MemoryStream())
			{
				await CopyToStreamAsync(Bundle, Stream);
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
		/// <param name="Bundle">Bundle that can provide node data</param>
		/// <param name="OutputStream">The output stream to receive the data</param>
		public async Task CopyToStreamAsync(Bundle Bundle, Stream OutputStream)
		{
			if (ChildNodeRefs != null)
			{
				foreach (BundleNodeRef<FileNode> ChildNodeRef in ChildNodeRefs)
				{
					FileNode ChildNode = ChildNodeRef.Node ?? await Bundle.GetAsync(ChildNodeRef);
					await ChildNode.CopyToStreamAsync(Bundle, OutputStream);
				}
			}
			else
			{
				foreach (ReadOnlyMemory<byte> PayloadSegment in Payload)
				{
					await OutputStream.WriteAsync(PayloadSegment);
				}
			}
		}

		/// <summary>
		/// Extracts the contents of this node to a file
		/// </summary>
		/// <param name="Bundle">Bundle that can provide node data</param>
		/// <param name="File">File to write with the contents of this node</param>
		/// <returns></returns>
		public async Task CopyToFileAsync(Bundle Bundle, FileInfo File)
		{
			using (FileStream Stream = File.OpenWrite())
			{
				await CopyToStreamAsync(Bundle, Stream);
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
				NewNode.Depth = Depth;
				NewNode.Payload = Payload;
				NewNode.Data = Data;
				NewNode.Hash = Hash;

				NewNode.IsRoot = false;
				NewNode.RollingHash = RollingHash;
				NewNode.FirstSegment = FirstSegment;
				NewNode.LastSegment = LastSegment;
				NewNode.ChildNodeRefs = ChildNodeRefs;

				NewNode.Length = Length;

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
				Payload = ReadOnlySequence<byte>.Empty;
				Data = ReadOnlySequence<byte>.Empty;
				Hash = null;

				IsRoot = true;
				RollingHash = BuzHash.Add(0, NewNode.Hash!.Value.ToByteArray());
				FirstSegment = null;
				LastSegment = null;
				ChildNodeRefs = new List<BundleNodeRef<FileNode>>();

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
				int AppendLength = Math.Min(WindowSize - (int)Payload.Length, NewData.Length);
				AppendLeafData(NewData.Span.Slice(0, AppendLength), Options);
				NewData = NewData.Slice(AppendLength);
			}

			// Cap the maximum amount of data to append to this node
			int MaxLength = Math.Min(NewData.Length, Options.LeafOptions.MaxSize - (int)Payload.Length);
			if (MaxLength > 0)
			{
				ReadOnlySpan<byte> InputSpan = NewData.Span.Slice(0, MaxLength);
				int Length = AppendLeafDataToChunkBoundary(InputSpan, Options);
				NewData = NewData.Slice(Length);
			}

			// Compute the hash if this node is complete
			if (Payload.Length == Options.LeafOptions.MaxSize)
			{
				MarkComplete();
			}
			return NewData;
		}

		private int AppendLeafDataToChunkBoundary(ReadOnlySpan<byte> InputSpan, ChunkingOptions Options)
		{
			int WindowSize = Options.LeafOptions.MinSize;
			Debug.Assert(Payload.Length >= WindowSize);

			// If this is the first time we're hashing the data, compute the hash of the existing window
			if (Payload.Length == WindowSize)
			{
				foreach (ReadOnlyMemory<byte> Segment in Payload)
				{
					RollingHash = BuzHash.Add(RollingHash, Segment.Span);
				}
			}

			// Get the threshold for the rolling hash
			uint RollingHashThreshold = (uint)((1L << 32) / Options.LeafOptions.TargetSize);

			// Length of the data taken from the input span, updated as we step through it.
			int Length = 0;

			// Step the window through the tail end of the existing payload window. In this state, update the hash to remove data from the current payload, and add data from the new payload.
			int SplitLength = Math.Min(InputSpan.Length, WindowSize);
			ReadOnlySequence<byte> SplitPayload = Payload.Slice(Payload.Length - WindowSize, SplitLength);

			foreach (ReadOnlyMemory<byte> PayloadSegment in SplitPayload)
			{
				int BaseLength = Length;
				int SpanLength = Length + PayloadSegment.Length;

				ReadOnlySpan<byte> PayloadSpan = PayloadSegment.Span;
				for (; Length < SpanLength; Length++)
				{
					RollingHash = BuzHash.Add(RollingHash, InputSpan[Length]);
					if (RollingHash < RollingHashThreshold)
					{
						AppendLeafData(InputSpan.Slice(0, Length), Options);
						MarkComplete();
						return Length;
					}
					RollingHash = BuzHash.Sub(RollingHash, PayloadSpan[Length - BaseLength], WindowSize);
				}
			}

			// Step through the new window.
			for (; Length < InputSpan.Length; Length++)
			{
				RollingHash = BuzHash.Add(RollingHash, InputSpan[Length]);
				if (RollingHash < RollingHashThreshold)
				{
					AppendLeafData(InputSpan.Slice(0, Length), Options);
					MarkComplete();
					return Length;
				}
				RollingHash = BuzHash.Sub(RollingHash, InputSpan[Length - WindowSize], WindowSize);
			}

			AppendLeafData(InputSpan, Options);
			return InputSpan.Length;
		}

		private void AppendLeafData(ReadOnlySpan<byte> LeafData, ChunkingOptions Options)
		{
			int DepthBytes = VarInt.Measure(Depth);

			if (LastSegment == null)
			{
				byte[] Buffer = CreateFirstSegmentData(Depth, LeafData);
				FirstSegment = new DataSegment(0, Buffer);
				LastSegment = FirstSegment;
			}
			else
			{
				DataSegment NewSegment = new DataSegment(LastSegment.RunningIndex + LastSegment.Memory.Length, LeafData.ToArray());
				LastSegment.SetNext(NewSegment);
				LastSegment = NewSegment;
			}

			int HeaderSize = (int)(Data.Length - Payload.Length);
			Data = new ReadOnlySequence<byte>(FirstSegment!, 0, LastSegment, LastSegment.Memory.Length);
			Payload = Data.Slice(HeaderSize);

			Length += LeafData.Length;
		}

		private static byte[] CreateFirstSegmentData(int Depth, ReadOnlySpan<byte> LeafData)
		{
			int DepthBytes = VarInt.Measure(Depth);

			byte[] Buffer = new byte[1 + DepthBytes + LeafData.Length];
			Buffer[0] = TypeId;

			VarInt.Write(Buffer.AsSpan(1, DepthBytes), Depth);
			LeafData.CopyTo(Buffer.AsSpan(1 + DepthBytes));

			return Buffer;
		}

		private void MarkComplete()
		{
			if (Hash == null)
			{
				if (ChildNodeRefs != null)
				{
					int DepthBytes = VarInt.Measure(Depth);
					int LengthBytes = VarInt.Measure((ulong)Length);

					byte[] NewData = new byte[1 + DepthBytes + LengthBytes + (ChildNodeRefs.Count * IoHash.NumBytes)];
					Data = new ReadOnlySequence<byte>(NewData);
					Payload = new ReadOnlySequence<byte>(NewData.AsMemory(1 + DepthBytes));

					NewData[0] = TypeId;
					Span<byte> Span = NewData.AsSpan(1);

					VarInt.Write(Span, Depth);
					Span = Span.Slice(DepthBytes);

					VarInt.Write(Span, Length);
					Span = Span.Slice(LengthBytes);

					foreach (BundleNodeRef<FileNode> ChildNodeRef in ChildNodeRefs)
					{
						ChildNodeRef.Hash.CopyTo(Span);
						Span = Span.Slice(IoHash.NumBytes);
					}

					Debug.Assert(Span.Length == 0);
				}
				Hash = IoHash.Compute(Data);
			}
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
							// Update the hash
							byte[] HashData = LastNode.Hash!.Value.ToByteArray();
							RollingHash = BuzHash.Add(RollingHash, HashData);

							// Check if it's time to finish this chunk
							uint HashThreshold = (uint)(((1L << 32) * IoHash.NumBytes) / Options.LeafOptions.TargetSize);
							if ((Payload.Length >= Options.InteriorOptions.MinSize && RollingHash < HashThreshold) || (Payload.Length >= Options.InteriorOptions.MaxSize))
							{
								MarkComplete();
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
		public override FileNode Deserialize(ReadOnlyMemory<byte> Data) => FileNode.Deserialize(Data);
	}
}
