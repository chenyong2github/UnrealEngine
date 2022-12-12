// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage.Nodes;
using EpicGames.Serialization;
using Microsoft.Extensions.Options;
using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Xml.Linq;

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
		/// Creates a file node from a block of memory
		/// </summary>
		/// <param name="memory">The memory to read from</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="writer">Writer for new tree nodes</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New file node</returns>
		public static Task<FileNode> CreateAsync(ReadOnlyMemory<byte> memory, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			using ReadOnlyMemoryStream stream = new ReadOnlyMemoryStream(memory);
			return CreateAsync(stream, options, writer, cancellationToken);
		}

		/// <summary>
		/// Creates a file node from a file
		/// </summary>
		/// <param name="fileInfo">The stream to read from</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="writer">Writer for new tree nodes</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New file node</returns>
		public static async Task<FileNode> CreateAsync(FileInfo fileInfo, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			using (Stream stream = fileInfo.OpenRead())
			{
				return await FileNode.CreateAsync(stream, options, writer, cancellationToken);
			}
		}

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
		/// <param name="reader">Reader for nodes in the tree</param>
		/// <param name="outputStream">The output stream to receive the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public abstract Task CopyToStreamAsync(TreeReader reader, Stream outputStream, CancellationToken cancellationToken);

		/// <summary>
		/// Extracts the contents of this node to a file
		/// </summary>
		/// <param name="reader">Reader for nodes in the tree</param>
		/// <param name="file">File to write with the contents of this node</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async Task CopyToFileAsync(TreeReader reader, FileInfo file, CancellationToken cancellationToken)
		{
			using (FileStream stream = file.OpenWrite())
			{
				await CopyToStreamAsync(reader, stream, cancellationToken);
			}
		}

		/// <summary>
		/// Serialize this node and its children into a byte array
		/// </summary>
		/// <param name="reader">Reader for nodes in the tree</param>
		/// <param name="cancellationToken"></param>
		/// <returns>Array of data stored by the tree</returns>
		public async Task<byte[]> ToByteArrayAsync(TreeReader reader, CancellationToken cancellationToken)
		{
			using (MemoryStream stream = new MemoryStream())
			{
				await CopyToStreamAsync(reader, stream, cancellationToken);
				return stream.ToArray();
			}
		}
	}

	/// <summary>
	/// File node that contains a chunk of data
	/// </summary>
	[TreeNode("{B27AFB68-9E20-4A4B-A4D8-788A4098D439}", 1)]
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

		bool _isReadOnly;
		uint _rollingHash;
		int _length;
		byte[]? _buffer;

		/// <summary>
		/// Create an empty leaf node
		/// </summary>
		public LeafFileNode()
		{
		}

		/// <summary>
		/// Create a leaf node from the given serialized data
		/// </summary>
		public LeafFileNode(ITreeNodeReader reader)
		{
			_isReadOnly = true;
			_rollingHash = reader.ReadUInt32();
			_buffer = reader.ReadVariableLengthBytes().ToArray();
			_length = _buffer.Length;
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteUInt32(_rollingHash);
			writer.WriteVariableLengthBytes(Data.Span);
		}

		/// <inheritdoc/>
		public override IEnumerable<TreeNodeRef> EnumerateRefs() => Enumerable.Empty<TreeNodeRef>();

		/// <inheritdoc/>
		public override bool IsReadOnly() => _isReadOnly;

		/// <inheritdoc/>
		public override uint RollingHash => _rollingHash;

		/// <summary>
		/// Gets the data for this node
		/// </summary>
		public ReadOnlyMemory<byte> Data => _buffer.AsMemory(0, _length);

		/// <inheritdoc/>
		public override long Length => _length;

		/// <inheritdoc/>
		public override ValueTask<ReadOnlyMemory<byte>> AppendDataAsync(ReadOnlyMemory<byte> newData, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			ReadOnlyMemory<byte> result;
			if (_isReadOnly)
			{
				result = newData;
			}
			else
			{
				if (_buffer == null || _buffer.Length < options.LeafOptions.MaxSize)
				{
					Array.Resize(ref _buffer, options.LeafOptions.MaxSize);
				}

				int appendLength = AppendData(_buffer.AsSpan(0, _length), newData.Span, ref _rollingHash, options.LeafOptions);
				newData.Slice(0, appendLength).CopyTo(_buffer.AsMemory(_length));
				_length += appendLength;

				result = newData.Slice(appendLength);
				_isReadOnly = (result.Length > 0);
			}
			return new ValueTask<ReadOnlyMemory<byte>>(result);
		}

		/// <summary>
		/// Determines how much data to append to an existing leaf node
		/// </summary>
		/// <param name="currentData">Current data in the leaf node</param>
		/// <param name="appendData">Data to be appended</param>
		/// <param name="rollingHash">Current BuzHash of the data</param>
		/// <param name="options">Options for chunking the data</param>
		/// <returns>The number of bytes to append</returns>
		internal static int AppendData(ReadOnlySpan<byte> currentData, ReadOnlySpan<byte> appendData, ref uint rollingHash, ChunkingOptionsForNodeType options)
		{
			// If the target option sizes are fixed, just chunk the data along fixed boundaries
			if (options.MinSize == options.TargetSize && options.MaxSize == options.TargetSize)
			{
				return Math.Min(appendData.Length, options.MaxSize - (int)currentData.Length);
			}

			// Cap the append data span to the maximum amount we can add
			int maxAppendLength = options.MaxSize - currentData.Length;
			if (maxAppendLength < appendData.Length)
			{
				appendData = appendData.Slice(0, maxAppendLength);
			}

			// Length of the data to be appended
			int appendLength = 0;

			// Fast path for appending data to the buffer up to the chunk window size
			int windowSize = options.MinSize;
			if (currentData.Length < windowSize)
			{
				appendLength = Math.Min(windowSize - (int)currentData.Length, appendData.Length);
				rollingHash = BuzHash.Add(rollingHash, appendData.Slice(0, appendLength));
			}

			// Get the threshold for the rolling hash
			uint rollingHashThreshold = (uint)((1L << 32) / options.TargetSize);

			// Step through the part of the data where the tail of the window is in currentData, and the head of the window is in appendData.
			if(appendLength < appendData.Length && windowSize > appendLength)
			{
				int overlap = windowSize - appendLength;
				int overlapLength = Math.Min(appendData.Length - appendLength, overlap);

				ReadOnlySpan<byte> tailSpan = currentData.Slice(currentData.Length - overlap, overlapLength);
				ReadOnlySpan<byte> headSpan = appendData.Slice(appendLength, overlapLength);

				int count = BuzHash.Update(tailSpan, headSpan, rollingHashThreshold, ref rollingHash);
				if (count != -1)
				{
					appendLength += count;
					return appendLength;
				}

				appendLength += headSpan.Length;
			}

			// Step through the rest of the data which is completely contained in appendData.
			if (appendLength < appendData.Length)
			{
				Debug.Assert(appendLength >= windowSize);
					
				ReadOnlySpan<byte> tailSpan = appendData.Slice(appendLength - windowSize, appendData.Length - windowSize);
				ReadOnlySpan<byte> headSpan = appendData.Slice(appendLength);

				int count = BuzHash.Update(tailSpan, headSpan, rollingHashThreshold, ref rollingHash);
				if (count != -1)
				{
					appendLength += count;
					return appendLength;
				}

				appendLength += headSpan.Length;
			}

			return appendLength;
		}

		/// <inheritdoc/>
		public override async Task CopyToStreamAsync(TreeReader reader, Stream outputStream, CancellationToken cancellationToken)
		{
			await outputStream.WriteAsync(Data, cancellationToken);
		}
	}

	/// <summary>
	/// An interior file node
	/// </summary>
	[TreeNode("{F4DEDDBC-70CB-4C7A-8347-F011AFCCCDB9}", 1)]
	public class InteriorFileNode : FileNode
	{
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
			writer.WriteUInt32(_rollingHash);
			writer.WriteUnsignedVarInt((ulong)_length);
			writer.WriteBoolean(_isReadOnly);
			writer.WriteList(_children, x => writer.WriteRef(x));
		}

		/// <inheritdoc/>
		public override IEnumerable<TreeNodeRef> EnumerateRefs() => _children;

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
		public override async Task CopyToStreamAsync(TreeReader reader, Stream outputStream, CancellationToken cancellationToken)
		{
			foreach (TreeNodeRef<FileNode> childNodeRef in _children)
			{
				FileNode childNode = await childNodeRef.ExpandAsync(reader, cancellationToken);
				await childNode.CopyToStreamAsync(reader, outputStream, cancellationToken);
			}
		}
	}
}
