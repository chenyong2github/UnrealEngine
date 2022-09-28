// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage.Nodes
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
		public ChunkingOptionsForNodeType(int minSize, int maxSize, int targetSize)
		{
			MinSize = minSize;
			MaxSize = maxSize;
			TargetSize = targetSize;
		}
	}

	/// <summary>
	/// Representation of a data stream, split into chunks along content-aware boundaries using a rolling hash (<see cref="BuzHash"/>).
	/// Chunks are pushed into a tree hierarchy as data is appended to the root, with nodes of the tree also split along content-aware boundaries with <see cref="IoHash.NumBytes"/> granularity.
	/// Once a chunk has been written to storage, it is treated as immutable.
	/// </summary>
	[TreeSerializer(typeof(FileNodeSerializer))]
	public abstract class FileNode : TreeNode
	{
		/// <summary>
		/// Length of the node
		/// </summary>
		public abstract long Length { get; }

		/// <summary>
		/// Rolling hash for the current node
		/// </summary>
		public abstract uint RollingHash { get; }

		/// <summary>
		/// Whether the node can have data appended to it
		/// </summary>
		/// <returns>True if the node is read only</returns>
		public abstract bool IsReadOnly();

		/// <summary>
		/// Creates a file node from a stream
		/// </summary>
		/// <param name="stream">The stream to read from</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="writer">Writer for new tree nodes</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New file node</returns>
		public static async Task<FileNode> CreateAsync(Stream stream, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			FileNode node = new LeafFileNode();

			byte[] buffer = new byte[4 * 1024];
			for (; ; )
			{
				int numBytes = await stream.ReadAsync(buffer, cancellationToken);
				if (numBytes == 0)
				{
					break;
				}
				node = await node.AppendAsync(buffer.AsMemory(0, numBytes), options, writer, cancellationToken);
			}

			return node;
		}

		/// <summary>
		/// Append data to this chunk. Must only be called on the root node in a chunk tree.
		/// </summary>
		/// <param name="input">The data to write</param>
		/// <param name="options">Settings for chunking the data</param>
		/// <param name="writer">Writer for new tree nodes</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public ValueTask<FileNode> AppendAsync(ReadOnlyMemory<byte> input, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			return AppendAsync(this, input, options, writer, cancellationToken);
		}

		/// <summary>
		/// Appends data to this file
		/// </summary>
		/// <param name="stream">Data to append to the file</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="writer">Writer for new node data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New node at the root of this file</returns>
		public ValueTask<FileNode> AppendAsync(Stream stream, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			return AppendAsync(this, stream, options, writer, cancellationToken);
		}

		/// <summary>
		/// Appends data to this file
		/// </summary>
		/// <param name="node">Node to append to</param>
		/// <param name="stream">Data to append to the file</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="writer">Writer for new node data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New node at the root of this file</returns>
		static async ValueTask<FileNode> AppendAsync(FileNode node, Stream stream, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			const int BufferLength = 32 * 1024;

			using IMemoryOwner<byte> owner = MemoryPool<byte>.Shared.Rent(BufferLength * 2);
			Memory<byte> buffer = owner.Memory;

			int readBufferOffset = 0;
			Memory<byte> appendBuffer = Memory<byte>.Empty;
			for (; ; )
			{
				// Start a read into memory
				Memory<byte> readBuffer = buffer.Slice(readBufferOffset, BufferLength);
				Task<int> readTask = Task.Run(async () => await stream.ReadAsync(readBuffer, cancellationToken), cancellationToken);

				// In the meantime, append the last data that was read to the tree
				if (appendBuffer.Length > 0)
				{
					node = await node.AppendAsync(appendBuffer, options, writer, cancellationToken);
				}

				// Wait for the read to finish
				int numBytes = await readTask;
				if (numBytes == 0)
				{
					break;
				}

				// Switch the buffers around
				appendBuffer = readBuffer.Slice(0, numBytes);
				readBufferOffset ^= BufferLength;
			}
			return node;
		}

		static async ValueTask<FileNode> AppendAsync(FileNode root, ReadOnlyMemory<byte> input, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			for (; ; )
			{
				// Append as much data as possible to the existing tree
				input = await root.AppendToNodeAsync(input, options, writer, cancellationToken);
				if (input.IsEmpty)
				{
					break;
				}

				// Increase the height of the tree by pushing the contents of this node into a new child node
				root = new InteriorFileNode(root.Length, root);
			}
			return root;
		}

		private async Task<ReadOnlyMemory<byte>> AppendToNodeAsync(ReadOnlyMemory<byte> appendData, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			if (appendData.Length == 0 || IsReadOnly())
			{
				return appendData;
			}
			else
			{
				return await AppendDataAsync(appendData, options, writer, cancellationToken);
			}
		}

		/// <summary>
		/// Attempt to append data to the current node.
		/// </summary>
		/// <param name="newData">The data to append</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="writer">Writer for new tree nodes</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Remaining data in the buffer</returns>
		public abstract ValueTask<ReadOnlyMemory<byte>> AppendDataAsync(ReadOnlyMemory<byte> newData, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken);

		/// <summary>
		/// Copies the contents of this node and its children to the given output stream
		/// </summary>
		/// <param name="outputStream">The output stream to receive the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public abstract Task CopyToStreamAsync(Stream outputStream, CancellationToken cancellationToken);

		/// <summary>
		/// Extracts the contents of this node to a file
		/// </summary>
		/// <param name="file">File to write with the contents of this node</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async Task CopyToFileAsync(FileInfo file, CancellationToken cancellationToken)
		{
			using (FileStream stream = file.OpenWrite())
			{
				await CopyToStreamAsync(stream, cancellationToken);
			}
		}

		/// <summary>
		/// Serialize this node and its children into a byte array
		/// </summary>
		/// <param name="cancellationToken"></param>
		/// <returns>Array of data stored by the tree</returns>
		public async Task<byte[]> ToByteArrayAsync(CancellationToken cancellationToken)
		{
			using (MemoryStream stream = new MemoryStream())
			{
				await CopyToStreamAsync(stream, cancellationToken);
				return stream.ToArray();
			}
		}
	}

	/// <summary>
	/// File node that contains a chunk of data
	/// </summary>
	public sealed class LeafFileNode : FileNode
	{
		class DataSegment : ReadOnlySequenceSegment<byte>
		{
			public DataSegment(long runningIndex, ReadOnlyMemory<byte> data)
			{
				RunningIndex = runningIndex;
				Memory = data;
			}

			public void SetNext(DataSegment next)
			{
				Next = next;
			}
		}

		/// <summary>
		/// First byte in the serialized data for this class, indicating its type.
		/// </summary>
		public const byte TypeId = (byte)'l';

		bool _isReadOnly;
		uint _rollingHash;
		DataSegment _firstSegment;
		DataSegment _lastSegment;
		ReadOnlySequence<byte> _writtenSequence; // Payload described by _firstSegment -> _lastSegment

		/// <summary>
		/// Create an empty leaf node
		/// </summary>
		public LeafFileNode()
		{
			_firstSegment = new DataSegment(0, ReadOnlyMemory<byte>.Empty);
			_lastSegment = _firstSegment;

			_writtenSequence = ReadOnlySequence<byte>.Empty;
		}

		/// <summary>
		/// Create a leaf node from the given serialized data
		/// </summary>
		public LeafFileNode(ITreeNodeReader reader)
		{
			byte typeId = reader.ReadUInt8();
			if (typeId != TypeId)
			{
				throw new InvalidDataException($"Invalid type id for {nameof(LeafFileNode)}");
			}

			_isReadOnly = true;
			_rollingHash = reader.ReadUInt32();

			_firstSegment = new DataSegment(0, reader.ReadVariableLengthBytes());
			_lastSegment = _firstSegment;

			_writtenSequence = new ReadOnlySequence<byte>(_firstSegment.Memory);
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteUInt8(TypeId);
			writer.WriteUInt32(_rollingHash);
			writer.WriteVariableLengthBytes(_writtenSequence);
		}

		/// <inheritdoc/>
		public override bool IsReadOnly() => _isReadOnly;

		/// <inheritdoc/>
		public override uint RollingHash => _rollingHash;

		/// <summary>
		/// Gets the data for this node
		/// </summary>
		public ReadOnlySequence<byte> Data => _writtenSequence;

		/// <inheritdoc/>
		public override long Length => _writtenSequence.Length;

		/// <inheritdoc/>
		public override ValueTask<ReadOnlyMemory<byte>> AppendDataAsync(ReadOnlyMemory<byte> newData, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			return new ValueTask<ReadOnlyMemory<byte>>(AppendData(newData, options));
		}

		ReadOnlyMemory<byte> AppendData(ReadOnlyMemory<byte> newData, ChunkingOptions options)
		{
			if (_isReadOnly)
			{
				return newData;
			}

			// If the target option sizes are fixed, just chunk the data along fixed boundaries
			if (options.LeafOptions.MinSize == options.LeafOptions.TargetSize && options.LeafOptions.MaxSize == options.LeafOptions.TargetSize)
			{
				int length = Math.Min(newData.Length, options.LeafOptions.MaxSize - (int)Length);
				AppendLeafData(newData.Span.Slice(0, length), 0);
				newData = newData.Slice(length);
			}
			else
			{
				// Fast path for appending data to the buffer up to the chunk window size
				int windowSize = options.LeafOptions.MinSize;
				if (Length < windowSize)
				{
					int appendLength = Math.Min(windowSize - (int)Length, newData.Length);
					AppendLeafData(newData.Span.Slice(0, appendLength));
					newData = newData.Slice(appendLength);
				}

				// Cap the maximum amount of data to append to this node
				int maxLength = Math.Min(newData.Length, options.LeafOptions.MaxSize - (int)Length);
				if (maxLength > 0)
				{
					ReadOnlySpan<byte> inputSpan = newData.Span.Slice(0, maxLength);
					int length = AppendLeafDataToChunkBoundary(inputSpan, options);
					newData = newData.Slice(length);
				}
			}

			// Mark this node as complete if we've reached the max size
			if (Length == options.LeafOptions.MaxSize)
			{
				_isReadOnly = true;
			}
			return newData;
		}

		private int AppendLeafDataToChunkBoundary(ReadOnlySpan<byte> headSpan, ChunkingOptions options)
		{
			int windowSize = options.LeafOptions.MinSize;
			Debug.Assert(Length >= windowSize);

			// Get the threshold for the rolling hash
			uint newRollingHash = _rollingHash;
			uint rollingHashThreshold = (uint)((1L << 32) / options.LeafOptions.TargetSize);

			// Offset within the head span, updated as we step through it.
			int offset = 0;

			// Step the window through the tail end of the existing payload window. In this state, update the hash to remove data from the current payload, and add data from the new payload.
			int tailLength = Math.Min(headSpan.Length, windowSize);
			ReadOnlySequence<byte> tailSequence = _writtenSequence.Slice(_writtenSequence.Length - windowSize, tailLength);

			foreach (ReadOnlyMemory<byte> tailSegment in tailSequence)
			{
				int count = BuzHash.Update(tailSegment.Span, headSpan.Slice(offset, tailSegment.Length), rollingHashThreshold, ref newRollingHash);
				if (count != -1)
				{
					offset += count;
					AppendLeafData(headSpan.Slice(0, offset), newRollingHash);
					_isReadOnly = true;
					return offset;
				}
				offset += tailSegment.Length;
			}

			// Step through the new window until we get to a chunk boundary.
			if (offset < headSpan.Length)
			{
				int count = BuzHash.Update(headSpan.Slice(offset - windowSize, headSpan.Length - offset), headSpan.Slice(offset), rollingHashThreshold, ref newRollingHash);
				if (count != -1)
				{
					offset += count;
					AppendLeafData(headSpan.Slice(0, offset), newRollingHash);
					_isReadOnly = true;
					return offset;
				}
			}

			// Otherwise just append all the data.
			AppendLeafData(headSpan, newRollingHash);
			return headSpan.Length;
		}

		private void AppendLeafData(ReadOnlySpan<byte> leafData)
		{
			uint newRollingHash = BuzHash.Add(_rollingHash, leafData);
			AppendLeafData(leafData, newRollingHash);
		}

		private void AppendLeafData(ReadOnlySpan<byte> leafData, uint newRollingHash)
		{
			_rollingHash = newRollingHash;

			DataSegment segment = new DataSegment(_lastSegment.RunningIndex + _lastSegment.Memory.Length, leafData.ToArray());
			if (_writtenSequence.Length == 0)
			{
				_firstSegment = segment;
			}
			else
			{
				_lastSegment.SetNext(segment);
			}
			_lastSegment = segment;

			_writtenSequence = new ReadOnlySequence<byte>(_firstSegment, 0, _lastSegment, _lastSegment.Memory.Length);
		}

		/// <inheritdoc/>
		public override async Task CopyToStreamAsync(Stream outputStream, CancellationToken cancellationToken)
		{
			foreach (ReadOnlyMemory<byte> segment in _writtenSequence)
			{
				await outputStream.WriteAsync(segment, cancellationToken);
			}
		}
	}

	/// <summary>
	/// An interior file node
	/// </summary>
	public class InteriorFileNode : FileNode
	{
		/// <summary>
		/// Type identifier for interior nodes. First byte in the serialized stream.
		/// </summary>
		public const byte TypeId = (byte)'i';

		bool _isReadOnly;
		uint _rollingHash;
		long _length;
		readonly List<TreeNodeRef<FileNode>> _children = new List<TreeNodeRef<FileNode>>();

		/// <summary>
		/// Child nodes
		/// </summary>
		public IReadOnlyList<TreeNodeRef<FileNode>> Children => _children;

		/// <inheritdoc/>
		public override long Length => _length;

		/// <inheritdoc/>
		public override uint RollingHash => _rollingHash;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="length"></param>
		/// <param name="child"></param>
		public InteriorFileNode(long length, FileNode child)
		{
			_length = length;
			_children.Add(new TreeNodeRef<FileNode>(child));
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public InteriorFileNode(ITreeNodeReader reader)
		{
			byte typeId = reader.ReadUInt8();
			if (typeId != TypeId)
			{
				throw new InvalidDataException($"Invalid type id for {nameof(InteriorFileNode)}");
			}

			_rollingHash = reader.ReadUInt32();
			_length = (long)reader.ReadUnsignedVarInt();
			_isReadOnly = reader.ReadBoolean();
			_children = reader.ReadList(() => reader.ReadRef<FileNode>());
		}

		/// <inheritdoc/>
		public override bool IsReadOnly() => _isReadOnly;

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteUInt8(TypeId);

			writer.WriteUInt32(_rollingHash);
			writer.WriteUnsignedVarInt((ulong)_length);
			writer.WriteBoolean(_isReadOnly);
			writer.WriteList(_children, x => writer.WriteRef(x));
		}

		/// <inheritdoc/>
		public override async ValueTask<ReadOnlyMemory<byte>> AppendDataAsync(ReadOnlyMemory<byte> newData, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			for (; ; )
			{
				Debug.Assert(_children != null);

				// Try to write to the last node
				if (_children.Count > 0)
				{
					TreeNodeRef<FileNode> lastNodeRef = _children[^1];

					FileNode? lastNode = lastNodeRef.Target;
					if (lastNode != null)
					{
						// Update the length to match the new node
						_length -= lastNode.Length;
						newData = await lastNode.AppendDataAsync(newData, options, writer, cancellationToken);
						_length += lastNode.Length;

						// If the last node is complete, write it to the buffer
						if (lastNode.IsReadOnly())
						{
							// Write the last node to allow it to be flushed
							await writer.WriteAsync(lastNodeRef, cancellationToken);

							// Update the hash
							AppendChildHash(lastNode.RollingHash);

							// Check if it's time to finish this chunk
							uint hashThreshold = (uint)(((1L << 32) * IoHash.NumBytes) / options.LeafOptions.TargetSize);
							if ((_children.Count >= options.InteriorOptions.MinSize && _rollingHash < hashThreshold) || (_children.Count >= options.InteriorOptions.MaxSize))
							{
								_isReadOnly = true;
								return newData;
							}
						}

						// Bail out if there's nothing left to write
						if (newData.Length == 0)
						{
							return newData;
						}

						// Collapse the final node
						await writer.WriteAsync(Children[^1], cancellationToken);
					}
				}

				// Add a new child node
				_children.Add(new TreeNodeRef<FileNode>(new LeafFileNode()));
			}
		}

		/// <summary>
		/// Updates the rolling hash to append a child hash
		/// </summary>
		/// <param name="childHash">The child hash to append</param>
		void AppendChildHash(uint childHash)
		{
			Span<byte> hashData = stackalloc byte[4];
			BinaryPrimitives.WriteUInt32LittleEndian(hashData, childHash);
			_rollingHash = BuzHash.Add(_rollingHash, hashData);
		}

		/// <inheritdoc/>
		public override async Task CopyToStreamAsync(Stream outputStream, CancellationToken cancellationToken)
		{
			foreach (TreeNodeRef<FileNode> childNodeRef in _children)
			{
				FileNode childNode = await childNodeRef.ExpandAsync(cancellationToken);
				await childNode.CopyToStreamAsync(outputStream, cancellationToken);
			}
		}
	}

	/// <summary>
	/// Factory class for file nodes
	/// </summary>
	public class FileNodeSerializer : TreeNodeSerializer<FileNode>
	{
		/// <inheritdoc/>
		public override FileNode Deserialize(ITreeNodeReader reader)
		{
			ReadOnlySpan<byte> data = reader.GetSpan(1);
			switch (data[0])
			{
				case LeafFileNode.TypeId:
					return new LeafFileNode(reader);
				case InteriorFileNode.TypeId:
					return new InteriorFileNode(reader);
				default:
					throw new InvalidDataException("Unknown type id while deserializing file node");
			};
		}
	}
}
