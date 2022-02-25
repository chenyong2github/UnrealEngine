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
	/// Options for creating chunk nodes
	/// </summary>
	public class ChunkOptions
	{
		/// <summary>
		/// Options for creating leaf nodes
		/// </summary>
		public TypedChunkOptions LeafOptions { get; set; }

		/// <summary>
		/// Options for creating interior nodes
		/// </summary>
		public TypedChunkOptions InteriorOptions { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public ChunkOptions()
		{
			LeafOptions = new TypedChunkOptions(32 * 1024, 256 * 1024, 64 * 1024);
			InteriorOptions = new TypedChunkOptions(32 * 1024, 256 * 1024, 64 * 1024);
		}
	}

	/// <summary>
	/// Options for creating a specific type of chunk nodes
	/// </summary>
	public class TypedChunkOptions
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
		public TypedChunkOptions(int MinSize, int MaxSize, int TargetSize)
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
	[BundleNodeFactory(typeof(ChunkNodeFactory))]
	public sealed class ChunkNode : BundleNode
	{
		const byte TypeId = (byte)'c';

		internal int Depth { get; private set; }
		internal ReadOnlyMemory<byte> Payload { get; private set; }
		ReadOnlyMemory<byte> Data; // Full serialized data, including type id and header fields.

		// In-memory state
		uint Hash;
		byte[]? WriteBuffer; // Null if the node is complete and does not support writes
		List<ChunkNode?>? ChildNodes;

		/// <summary>
		/// Constructor
		/// </summary>
		public ChunkNode(Bundle Owner, ChunkOptions Options)
			: this(Owner, null, 0, Options)
		{
		}

		/// <summary>
		/// Create a writable chunk node
		/// </summary>
		private ChunkNode(Bundle Owner, BundleNode? Parent, int Depth, ChunkOptions Options)
			: base(Owner, Parent)
		{
			MakeWritable(Depth, Options);
		}

		/// <summary>
		/// Create a chunk node from deserialized data
		/// </summary>
		public ChunkNode(Bundle Owner, BundleNode? Parent, IoHash Hash, ReadOnlyMemory<byte> Data)
			: base(Owner, Parent, Hash)
		{
			ReadOnlySpan<byte> Span = Data.Span;
			if (Span[0] != TypeId)
			{
				throw new InvalidDataException("Invalid type id");
			}

			Depth = (int)VarInt.Read(Span.Slice(1), out int DepthBytes);
			int HeaderLength = 1 + DepthBytes;

			Payload = Data.Slice(HeaderLength);
			this.Data = Data;

			if (Depth > 0)
			{
				ChildNodes = new List<ChunkNode?>(Payload.Length / IoHash.NumBytes);
				ChildNodes.AddRange(Enumerable.Repeat<ChunkNode?>(null, Payload.Length / IoHash.NumBytes));
			}
		}

		private void MakeWritable(int Depth, ChunkOptions Options)
		{
			int WriteBufferSize = 1 + VarInt.Measure(Depth);
			if (Depth == 0)
			{
				WriteBufferSize += Options.LeafOptions.MaxSize;
			}
			else
			{
				WriteBufferSize += Options.InteriorOptions.MaxSize;
			}

			byte[] WriteBuffer = new byte[WriteBufferSize];
			WriteBuffer[0] = TypeId;
			int DepthBytes = VarInt.Write(WriteBuffer.AsSpan(1), Depth);
			int HeaderLength = 1 + DepthBytes;

			this.Depth = Depth;
			this.Payload = ReadOnlyMemory<byte>.Empty;
			this.Data = WriteBuffer.AsMemory(0, HeaderLength);
			this.Hash = 0;
			this.WriteBuffer = WriteBuffer;
			this.ChildNodes = (Depth > 0) ? new List<ChunkNode?>() : null;
		}

		/// <summary>
		/// Returns the number of child nodes to this chunk
		/// </summary>
		public int GetChildCount() => (ChildNodes == null)? 0 : ChildNodes.Count;

		/// <summary>
		/// Gets a child node at the given index
		/// </summary>
		/// <param name="Index">Index of the child node</param>
		/// <returns>Node containing data for the given child</returns>
		public async ValueTask<ChunkNode> GetChildNodeAsync(int Index)
		{
			if(ChildNodes == null)
			{
				throw new InvalidOperationException("Node does not contain any children");
			}

			ChunkNode? ChildNode = ChildNodes[Index];
			if (ChildNode == null)
			{
				IoHash Hash = new IoHash(Payload.Span.Slice(Index * IoHash.NumBytes));
				ReadOnlyMemory<byte> Data = await Owner.GetDataAsync(Hash);
				ChildNode = new ChunkNode(Owner, this, Hash, Data);
				ChildNodes[Index] = ChildNode;
			}
			return ChildNode;
		}

		/// <summary>
		/// Enumerate the children from this node
		/// </summary>
		/// <returns></returns>
		public async IAsyncEnumerable<ChunkNode> GetChildNodesAsync()
		{
			for (int Idx = 0; Idx < GetChildCount(); Idx++)
			{
				yield return await GetChildNodeAsync(Idx);
			}
		}

		/// <summary>
		/// Serialize this node and its children into a byte array
		/// </summary>
		/// <returns>Array of data stored by the tree</returns>
		public async Task<byte[]> ToByteArrayAsync()
		{
			using (MemoryStream Stream = new MemoryStream())
			{
				await CopyToAsync(Stream);
				return Stream.ToArray();
			}
		}

		/// <summary>
		/// Copies the contents of this node and its children to the given output stream
		/// </summary>
		/// <param name="OutputStream">The output stream to receive the data</param>
		public async Task CopyToAsync(Stream OutputStream)
		{
			if (Depth > 0)
			{
				for (int Idx = 0; Idx < GetChildCount(); Idx++)
				{
					ChunkNode Node = await GetChildNodeAsync(Idx);
					await Node.CopyToAsync(OutputStream);
				}
			}
			else
			{
				await OutputStream.WriteAsync(Payload);
			}
		}

		/// <summary>
		/// Append data to this chunk. Must only be called on the root node in a chunk tree.
		/// </summary>
		/// <param name="Input">The data to write</param>
		/// <param name="Options">Settings for chunking the data</param>
		public void Append(ReadOnlyMemory<byte> Input, ChunkOptions Options)
		{
			if (Parent is ChunkNode)
			{
				throw new InvalidOperationException("Data may only be appended to the root of a tree of chunk nodes.");
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
				ChunkNode NewNode = new ChunkNode(Owner, this, IoHash.Zero, Data);
				if (ChildNodes != null)
				{
					for (int Idx = 0; Idx < ChildNodes.Count; Idx++)
					{
						ChunkNode? ChildNode = ChildNodes[Idx];
						if (ChildNode != null)
						{
							ChildNode.Parent = NewNode;
							NewNode.ChildNodes![Idx] = ChildNode;
						}
					}
				}

				// Reinitialize the current node
				MakeWritable(Depth + 1, Options);
				AddChildNode(NewNode);
			}
		}

		private ReadOnlyMemory<byte> AppendToNode(ReadOnlyMemory<byte> Data, ChunkOptions Options)
		{
			if (WriteBuffer == null || Data.Length == 0)
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

		private ReadOnlyMemory<byte> AppendToLeafNode(ReadOnlyMemory<byte> NewData, ChunkOptions Options)
		{
			ReadOnlySpan<byte> NewSpan = NewData.Span;

			uint HashThreshold = (uint)((1L << 32) / Options.LeafOptions.TargetSize);
			for (int Length = 0; Length < NewData.Length; Length++)
			{
				Hash = BuzHash.Add(Hash, NewSpan[Length]);

				int BlockSize = Payload.Length + Length;
				if ((BlockSize >= Options.LeafOptions.MinSize && Hash < HashThreshold) || (BlockSize >= Options.LeafOptions.MaxSize))
				{
					AppendData(NewSpan.Slice(0, Length));
					WriteBuffer = null;
					return NewData.Slice(Length);
				}
			}

			AppendData(NewSpan);
			return ReadOnlyMemory<byte>.Empty;
		}

		private ReadOnlyMemory<byte> AppendToInteriorNode(ReadOnlyMemory<byte> NewData, ChunkOptions Options)
		{
			for (; ; )
			{
				Debug.Assert(ChildNodes != null);

				// Try to write to the last node
				if (ChildNodes.Count > 0)
				{
					ChunkNode? LastNode = ChildNodes[^1];
					if (LastNode != null)
					{
						NewData = LastNode.AppendToNode(NewData, Options);
						if (LastNode.WriteBuffer == null)
						{
							Span<byte> LastHashSpan = WriteBuffer.AsSpan(Data.Length - IoHash.NumBytes);

							// Add it to the write buffer
							IoHash LastHash = LastNode.Serialize();
							LastHash.CopyTo(LastHashSpan);

							// Update the hash
							Hash = BuzHash.Add(Hash, LastHashSpan);

							// Check if it's time to finish this chunk
							uint HashThreshold = (uint)(((1L << 32) * IoHash.NumBytes) / Options.LeafOptions.TargetSize);
							if ((Payload.Length >= Options.InteriorOptions.MinSize && Hash < HashThreshold) || (Payload.Length >= Options.InteriorOptions.MaxSize))
							{
								WriteBuffer = null;
								return NewData;
							}
						}
						if (NewData.Length == 0)
						{
							return NewData;
						}
					}
				}

				// Create a new child node
				ChunkNode NewNode = new ChunkNode(Owner, this, Depth - 1, Options);
				AddChildNode(NewNode);
			}
		}

		void AddChildNode(ChunkNode NewNode)
		{
			Debug.Assert(ChildNodes != null);
			ChildNodes.Add(NewNode);
			ExpandData(IoHash.NumBytes);
		}

		void ExpandData(int Size)
		{
			Debug.Assert(WriteBuffer != null);

			int NewDataLength = Data.Length + Size;
			if (NewDataLength > WriteBuffer.Length)
			{
				Array.Resize(ref WriteBuffer, NewDataLength + 4096);
			}

			int HeaderLength = Data.Length - Payload.Length;
			Data = WriteBuffer.AsMemory(0, NewDataLength);
			Payload = Data.Slice(HeaderLength);
		}

		void AppendData(ReadOnlySpan<byte> NewData)
		{
			ExpandData(NewData.Length);
			NewData.CopyTo(WriteBuffer.AsSpan(Data.Length - NewData.Length));
		}

		/// <inheritdoc/>
		protected override IoHash SerializeDirty()
		{
			// If we're still writing to the last node, flush it to the write buffer
			if (ChildNodes != null && ChildNodes.Count > 0)
			{
				ChunkNode? LastNode = ChildNodes[^1];
				if (LastNode != null && LastNode.WriteBuffer != null)
				{
					Span<byte> LastHashSpan = WriteBuffer.AsSpan(Payload.Length - IoHash.NumBytes);
					IoHash LastHash = LastNode.Serialize();
					LastHash.CopyTo(LastHashSpan);
				}
			}

			// Do not allow this node to be modified from this point out
			WriteBuffer = null;

			// Write the current data
			List<IoHash> References = new List<IoHash>();
			if (Depth > 0)
			{
				for (ReadOnlySpan<byte> Span = Payload.Span; Span.Length > 0; Span = Span.Slice(IoHash.NumBytes))
				{
					References.Add(new IoHash(Span));
				}
			}
			return Owner.WriteNode(Data, References);
		}
	}

	/// <summary>
	/// Factory class for chunk nodes
	/// </summary>
	public class ChunkNodeFactory : BundleNodeFactory<ChunkNode>
	{
		/// <inheritdoc/>
		public override ChunkNode CreateRoot(Bundle Bundle)
		{
			return new ChunkNode(Bundle, new ChunkOptions());
		}

		/// <inheritdoc/>
		public override ChunkNode ParseRoot(Bundle Bundle, IoHash Hash, ReadOnlyMemory<byte> Data)
		{
			return new ChunkNode(Bundle, null, Hash, Data);
		}
	}
}
