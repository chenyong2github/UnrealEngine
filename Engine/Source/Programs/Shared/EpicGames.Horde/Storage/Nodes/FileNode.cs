// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
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
		/// Serailize this node
		/// </summary>
		/// <param name="writer"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public abstract ValueTask<ITreeBlob> SerializeAsync(ITreeBlobWriter writer, CancellationToken cancellationToken);

		/// <summary>
		/// Creates a file node from a stream
		/// </summary>
		/// <param name="stream">The stream to read from</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New file node</returns>
		public static async Task<FileNode> CreateAsync(Stream stream, ChunkingOptions options, CancellationToken cancellationToken)
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
				node = node.Append(buffer.AsMemory(0, numBytes), options);
			}

			return node;
		}

		/// <summary>
		/// Append data to this chunk. Must only be called on the root node in a chunk tree.
		/// </summary>
		/// <param name="input">The data to write</param>
		/// <param name="options">Settings for chunking the data</param>
		public FileNode Append(ReadOnlyMemory<byte> input, ChunkingOptions options)
		{
			return Append(this, input, options);
		}

		static FileNode Append(FileNode root, ReadOnlyMemory<byte> input, ChunkingOptions options)
		{
			for (; ; )
			{
				// Append as much data as possible to the existing tree
				input = root.AppendToNode(input, options);
				if (input.IsEmpty)
				{
					break;
				}

				// Increase the height of the tree by pushing the contents of this node into a new child node
				root = new InteriorFileNode(root.Length, root);

/*
				FileNode newNode = new InteriorFileNode(root.Length, root);
				if (_data is InternalNodeData internalData)
				{
					foreach (TreeNodeRef<FileNode> childNodeRef in internalData._children)
					{
						childNodeRef.Reparent(newNode);
					}
				}
				_data = new InternalNodeData(newNode.Length, new TreeNodeRef<FileNode>(this, newNode));*/
			}
			return root;
		}

		private ReadOnlyMemory<byte> AppendToNode(ReadOnlyMemory<byte> appendData, ChunkingOptions options)
		{
			if (appendData.Length == 0 || IsReadOnly())
			{
				return appendData;
			}
			else
			{
				return AppendData(appendData, options);
			}
		}

		/// <summary>
		/// Attempt to append data to the current node.
		/// </summary>
		/// <param name="newData">The data to append</param>
		/// <param name="options">Options for chunking the data</param>
		/// <returns>Remaining data in the buffer</returns>
		public abstract ReadOnlyMemory<byte> AppendData(ReadOnlyMemory<byte> newData, ChunkingOptions options);

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

		const int HeaderLength = 1 + sizeof(uint);

		bool _isReadOnly;
		uint _rollingHash;
		readonly byte[] _header;
		readonly DataSegment _firstSegment;
		DataSegment _lastSegment;
		ReadOnlySequence<byte> _writtenSequence; // Payload described by _firstSegment -> _lastSegment

		/// <summary>
		/// Create an empty leaf node
		/// </summary>
		public LeafFileNode()
		{
			_header = new byte[HeaderLength];
			_header[0] = TypeId;

			_firstSegment = new DataSegment(0, _header);
			_lastSegment = _firstSegment;

			_writtenSequence = CreateSequence(_firstSegment, _lastSegment);
		}

		/// <summary>
		/// Create a leaf node from the given serialized data
		/// </summary>
		/// <param name="data">Data to construct from</param>
		public LeafFileNode(ReadOnlySequence<byte> data)
		{
			_header = Array.Empty<byte>();

			ReadOnlySpan<byte> span = data.Slice(0, HeaderLength).AsSingleSegment().Span;
			if (span[0] != TypeId)
			{
				throw new InvalidDataException($"Invalid type id for {nameof(LeafFileNode)}");
			}

			_isReadOnly = true;
			_rollingHash = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(1, sizeof(uint)));

			_firstSegment = null!;
			_lastSegment = null!;

			_writtenSequence = data;
		}

		static ReadOnlySequence<byte> CreateSequence(DataSegment firstSegment, DataSegment lastSegment)
		{
			return new ReadOnlySequence<byte>(firstSegment, 0, lastSegment, lastSegment.Memory.Length);
		}

		/// <inheritdoc/>
		public override bool IsReadOnly() => _isReadOnly;

		/// <inheritdoc/>
		public override uint RollingHash => _rollingHash;

		/// <summary>
		/// Gets the data for this node
		/// </summary>
		public ReadOnlySequence<byte> Data => _writtenSequence.Slice(HeaderLength);

		/// <inheritdoc/>
		public override long Length => _writtenSequence.Length - HeaderLength;

		/// <inheritdoc/>
		public override IReadOnlyList<TreeNodeRef> GetReferences() => Array.Empty<TreeNodeRef>();

		/// <inheritdoc/>
		public override async ValueTask<ITreeBlob> SerializeAsync(ITreeBlobWriter writer, CancellationToken cancellationToken)
		{
			return await writer.WriteBlobAsync(_writtenSequence, Array.Empty<ITreeBlob>(), cancellationToken);
		}

		/// <inheritdoc/>
		public override ReadOnlyMemory<byte> AppendData(ReadOnlyMemory<byte> newData, ChunkingOptions options)
		{
			if (_isReadOnly)
			{
				return newData;
			}

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

			// Length of the data taken from the input span, updated as we step through it.
			int length = 0;

			// Step the window through the tail end of the existing payload window. In this state, update the hash to remove data from the current payload, and add data from the new payload.
			int splitLength = Math.Min(headSpan.Length, windowSize);
			ReadOnlySequence<byte> tailSequence = _writtenSequence.Slice(_writtenSequence.Length - windowSize, splitLength);

			foreach (ReadOnlyMemory<byte> tailSegment in tailSequence)
			{
				int baseLength = length;
				int spanLength = length + tailSegment.Length;

				ReadOnlySpan<byte> tailSpan = tailSegment.Span;
				for (; length < spanLength; length++)
				{
					newRollingHash = BuzHash.Add(newRollingHash, headSpan[length]);
					if (newRollingHash < rollingHashThreshold)
					{
						AppendLeafData(headSpan.Slice(0, length), newRollingHash);
						_isReadOnly = true;
						return length;
					}
					newRollingHash = BuzHash.Sub(newRollingHash, tailSpan[length - baseLength], windowSize);
				}
			}

			// Step through the new window until we get to a chunk boundary.
			for (; length < headSpan.Length; length++)
			{
				newRollingHash = BuzHash.Add(newRollingHash, headSpan[length]);
				if (newRollingHash < rollingHashThreshold)
				{
					AppendLeafData(headSpan.Slice(0, length), newRollingHash);
					_isReadOnly = true;
					return length;
				}
				newRollingHash = BuzHash.Sub(newRollingHash, headSpan[length - windowSize], windowSize);
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
			BinaryPrimitives.WriteUInt32LittleEndian(_header.AsSpan(1, sizeof(uint)), newRollingHash);

			DataSegment segment = new DataSegment(_lastSegment.RunningIndex + _lastSegment.Memory.Length, leafData.ToArray());
			_lastSegment.SetNext(segment);
			_lastSegment = segment;

			_writtenSequence = CreateSequence(_firstSegment, _lastSegment);
		}

		/// <inheritdoc/>
		public override async Task CopyToStreamAsync(Stream outputStream, CancellationToken cancellationToken)
		{
			foreach (ReadOnlyMemory<byte> segment in _writtenSequence.Slice(HeaderLength))
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
			_children.Add(new TreeNodeRef<FileNode>(this, child));
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="data"></param>
		/// <param name="children"></param>
		public InteriorFileNode(ReadOnlySequence<byte> data, IReadOnlyList<ITreeBlob> children)
		{
			ReadOnlySpan<byte> span = data.AsSingleSegment().Span;
			if (span[0] != TypeId)
			{
				throw new InvalidDataException($"Invalid type id for {nameof(InteriorFileNode)}");
			}

			int pos = 1;

			_rollingHash = BinaryPrimitives.ReadUInt32LittleEndian(span[pos..]);
			pos += sizeof(uint);

			_length = (int)VarInt.ReadUnsigned(span[pos..], out int lengthBytes);
			pos += lengthBytes;

			_isReadOnly = span[pos] != 0;
			_children = children.ConvertAll(x => new TreeNodeRef<FileNode>(this, x));
		}

		/// <inheritdoc/>
		public override bool IsReadOnly() => _isReadOnly;

		/// <inheritdoc/>
		public override IReadOnlyList<TreeNodeRef> GetReferences() => _children;

		/// <inheritdoc/>
		public override async ValueTask<ITreeBlob> SerializeAsync(ITreeBlobWriter writer, CancellationToken cancellationToken)
		{
			List<ITreeBlob> children = new List<ITreeBlob>();
			foreach (TreeNodeRef<FileNode> child in _children)
			{
				children.Add(await child.CollapseAsync(writer, cancellationToken));
			}

			byte[] data = new byte[1 + sizeof(uint) + VarInt.MeasureUnsigned((ulong)_length) + 1];
			data[0] = TypeId;

			int pos = 1;

			BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(pos), _rollingHash);
			pos += sizeof(uint);

			int lengthBytes = VarInt.WriteUnsigned(data.AsSpan(pos), _length);
			pos += lengthBytes;

			data[pos] = _isReadOnly ? (byte)1 : (byte)0;
			pos++;

			Debug.Assert(pos == data.Length);
			return await writer.WriteBlobAsync(new ReadOnlySequence<byte>(data), children, cancellationToken);
		}

		/// <inheritdoc/>
		public override ReadOnlyMemory<byte> AppendData(ReadOnlyMemory<byte> newData, ChunkingOptions options)
		{
			Span<byte> hashData = stackalloc byte[4];
			for (; ; )
			{
				Debug.Assert(_children != null);

				// Try to write to the last node
				if (_children.Count > 0)
				{
					FileNode? lastNode = _children[^1].Node;
					if (lastNode != null)
					{
						// Update the length to match the new node
						_length -= lastNode.Length;
						newData = lastNode.AppendData(newData, options);
						_length += lastNode.Length;

						// If the last node is complete, write it to the buffer
						if (lastNode.IsReadOnly())
						{
							// Update the hash
							BinaryPrimitives.WriteUInt32LittleEndian(hashData, lastNode.RollingHash);
							_rollingHash = BuzHash.Add(_rollingHash, hashData);

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
					}
				}

				// Add a new child node
				_children.Add(new TreeNodeRef<FileNode>(this, new LeafFileNode()));
			}
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
		public override async ValueTask<FileNode> DeserializeAsync(ITreeBlob blob, CancellationToken cancellationToken)
		{
			ReadOnlySequence<byte> data = await blob.GetDataAsync(cancellationToken);
			switch (data.FirstSpan[0])
			{
				case LeafFileNode.TypeId:
					return new LeafFileNode(data);
				case InteriorFileNode.TypeId:
					return new InteriorFileNode(data, await blob.GetReferencesAsync(cancellationToken));
				default:
					throw new InvalidDataException("Unknown type id while deserializing file node");
			};
		}

		/// <inheritdoc/>
		public override ValueTask<ITreeBlob> SerializeAsync(ITreeBlobWriter writer, FileNode node, CancellationToken cancellationToken) => node.SerializeAsync(writer, cancellationToken);
	}
}
