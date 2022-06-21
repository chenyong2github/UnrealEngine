// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
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
	[BundleNodeDeserializer(typeof(FileNodeDeserializer))]
	public sealed class FileNode : BundleNode
	{
		const byte TypeId = (byte)'c';

		static readonly ReadOnlyMemory<byte> s_defaultSegment = CreateFirstSegmentData(0, Array.Empty<byte>());

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

		internal int Depth { get; private set; }
		internal ReadOnlySequence<byte> Payload { get; private set; }
		ReadOnlySequence<byte> _data = new ReadOnlySequence<byte>(s_defaultSegment); // Full serialized data, including type id and header fields.
		IoHash? _hash; // Hash of the serialized data buffer.

		// In-memory state
		bool _isRoot = true;
		uint _rollingHash;
		DataSegment? _firstSegment;
		DataSegment? _lastSegment;
		List<BundleNodeRef<FileNode>>? _childNodeRefs;

		/// <summary>
		/// Length of this tree
		/// </summary>
		public long Length { get; private set; }

		/// <summary>
		/// Whether this node is read-only
		/// </summary>
		public override bool IsReadOnly() => _hash != null;

		/// <summary>
		/// Accessor for the children of this node
		/// </summary>
		public IReadOnlyList<BundleNodeRef<FileNode>> Children => (IReadOnlyList<BundleNodeRef<FileNode>>?)_childNodeRefs ?? Array.Empty<BundleNodeRef<FileNode>>();

		/// <inheritdoc/>
		public override ReadOnlySequence<byte> Serialize()
		{
			MarkComplete();
			return _data;
		}

		/// <inheritdoc/>
		public override IEnumerable<BundleNodeRef> GetReferences() => Children;

		/// <summary>
		/// Create a file node from deserialized data
		/// </summary>
		public static FileNode Deserialize(ReadOnlyMemory<byte> data)
		{
			ReadOnlySpan<byte> span = data.Span;
			if (span[0] != TypeId)
			{
				throw new InvalidDataException("Invalid type id");
			}

			FileNode node = new FileNode();
			node._data = new ReadOnlySequence<byte>(data);
			node._hash = IoHash.Compute(data.Span);
			node.Depth = (int)VarInt.ReadUnsigned(span.Slice(1), out int depthBytes);

			int headerLength = 1 + depthBytes;
			span = span.Slice(headerLength);

			ReadOnlyMemory<byte> payload = data.Slice(headerLength);
			node.Payload = new ReadOnlySequence<byte>(payload);

			if (node.Depth == 0)
			{
				node.Length = node.Payload.Length;
			}
			else
			{
				node.Length = (long)VarInt.ReadUnsigned(span, out int lengthBytes);
				span = span.Slice(lengthBytes);

				node._childNodeRefs = new List<BundleNodeRef<FileNode>>(payload.Length / IoHash.NumBytes);
				while (span.Length > 0)
				{
					IoHash childHash = new IoHash(span);
					node._childNodeRefs.Add(new BundleNodeRef<FileNode>(node, childHash));
					span = span.Slice(IoHash.NumBytes);
				}
			}
			return node;
		}

		/// <summary>
		/// Serialize this node and its children into a byte array
		/// </summary>
		/// <returns>Array of data stored by the tree</returns>
		public async Task<byte[]> ToByteArrayAsync(Bundle bundle)
		{
			using (MemoryStream stream = new MemoryStream())
			{
				await CopyToStreamAsync(bundle, stream);
				return stream.ToArray();
			}
		}

		/// <summary>
		/// Copy data from the given stream into this file node
		/// </summary>
		/// <param name="inputStream"></param>
		/// <param name="options"></param>
		/// <returns></returns>
		public async Task CopyFromStreamAsync(Stream inputStream, ChunkingOptions options)
		{
			byte[] buffer = new byte[64 * 1024];
			for (; ; )
			{
				int readSize = await inputStream.ReadAsync(buffer);
				if(readSize == 0)
				{
					break;
				}
				Append(buffer.AsMemory(0, readSize), options);
			}
		}

		/// <summary>
		/// Copies the contents of this file from disk
		/// </summary>
		/// <param name="file"></param>
		/// <param name="options"></param>
		/// <returns></returns>
		public async Task CopyFromFileAsync(FileInfo file, ChunkingOptions options)
		{
			using (FileStream stream = file.OpenRead())
			{
				await CopyFromStreamAsync(stream, options);
			}
		}

		/// <summary>
		/// Copies the contents of this node and its children to the given output stream
		/// </summary>
		/// <param name="bundle">Bundle that can provide node data</param>
		/// <param name="outputStream">The output stream to receive the data</param>
		public async Task CopyToStreamAsync(Bundle bundle, Stream outputStream)
		{
			if (_childNodeRefs != null)
			{
				foreach (BundleNodeRef<FileNode> childNodeRef in _childNodeRefs)
				{
					FileNode childNode = childNodeRef.Node ?? await bundle.GetAsync(childNodeRef);
					await childNode.CopyToStreamAsync(bundle, outputStream);
				}
			}
			else
			{
				foreach (ReadOnlyMemory<byte> payloadSegment in Payload)
				{
					await outputStream.WriteAsync(payloadSegment);
				}
			}
		}

		/// <summary>
		/// Extracts the contents of this node to a file
		/// </summary>
		/// <param name="bundle">Bundle that can provide node data</param>
		/// <param name="file">File to write with the contents of this node</param>
		/// <returns></returns>
		public async Task CopyToFileAsync(Bundle bundle, FileInfo file)
		{
			using (FileStream stream = file.OpenWrite())
			{
				await CopyToStreamAsync(bundle, stream);
			}
		}

		/// <summary>
		/// Append data to this chunk. Must only be called on the root node in a chunk tree.
		/// </summary>
		/// <param name="input">The data to write</param>
		/// <param name="options">Settings for chunking the data</param>
		public void Append(ReadOnlyMemory<byte> input, ChunkingOptions options)
		{
			if (!_isRoot)
			{
				throw new InvalidOperationException("Data may only be appended to the root of a file node tree");
			}

			for (; ; )
			{
				// Append as much data as possible to the existing tree
				input = AppendToNode(input, options);
				if (input.IsEmpty)
				{
					break;
				}

				// Increase the height of the tree by pushing the contents of this node into a new child node
				FileNode newNode = new FileNode();
				newNode.Depth = Depth;
				newNode.Payload = Payload;
				newNode._data = _data;
				newNode._hash = _hash;

				newNode._isRoot = false;
				newNode._rollingHash = _rollingHash;
				newNode._firstSegment = _firstSegment;
				newNode._lastSegment = _lastSegment;
				newNode._childNodeRefs = _childNodeRefs;

				newNode.Length = Length;

				// Detach all the child refs
				if (_childNodeRefs != null)
				{
					foreach (BundleNodeRef<FileNode> childNodeRef in _childNodeRefs)
					{
						childNodeRef.Reparent(newNode);
					}
					_childNodeRefs = null;
				}

				// Increase the depth and reset the current node
				Depth++;
				Payload = ReadOnlySequence<byte>.Empty;
				_data = ReadOnlySequence<byte>.Empty;
				_hash = null;

				_isRoot = true;
				_rollingHash = BuzHash.Add(0, newNode._hash!.Value.ToByteArray());
				_firstSegment = null;
				_lastSegment = null;
				_childNodeRefs = new List<BundleNodeRef<FileNode>>();

				// Append the new node as a child
				_childNodeRefs.Add(new BundleNodeRef<FileNode>(this, newNode));
			}
		}

		private ReadOnlyMemory<byte> AppendToNode(ReadOnlyMemory<byte> data, ChunkingOptions options)
		{
			if (data.Length == 0 || IsReadOnly())
			{
				return data;
			}

			if (Depth == 0)
			{
				return AppendToLeafNode(data, options);
			}
			else
			{
				return AppendToInteriorNode(data, options);
			}
		}

		private ReadOnlyMemory<byte> AppendToLeafNode(ReadOnlyMemory<byte> newData, ChunkingOptions options)
		{
			// Fast path for appending data to the buffer up to the chunk window size
			int windowSize = options.LeafOptions.MinSize;
			if (Payload.Length < windowSize)
			{
				int appendLength = Math.Min(windowSize - (int)Payload.Length, newData.Length);
				AppendLeafData(newData.Span.Slice(0, appendLength));
				newData = newData.Slice(appendLength);
			}

			// Cap the maximum amount of data to append to this node
			int maxLength = Math.Min(newData.Length, options.LeafOptions.MaxSize - (int)Payload.Length);
			if (maxLength > 0)
			{
				ReadOnlySpan<byte> inputSpan = newData.Span.Slice(0, maxLength);
				int length = AppendLeafDataToChunkBoundary(inputSpan, options);
				newData = newData.Slice(length);
			}

			// Compute the hash if this node is complete
			if (Payload.Length == options.LeafOptions.MaxSize)
			{
				MarkComplete();
			}
			return newData;
		}

		private int AppendLeafDataToChunkBoundary(ReadOnlySpan<byte> inputSpan, ChunkingOptions options)
		{
			int windowSize = options.LeafOptions.MinSize;
			Debug.Assert(Payload.Length >= windowSize);

			// If this is the first time we're hashing the data, compute the hash of the existing window
			if (Payload.Length == windowSize)
			{
				foreach (ReadOnlyMemory<byte> segment in Payload)
				{
					_rollingHash = BuzHash.Add(_rollingHash, segment.Span);
				}
			}

			// Get the threshold for the rolling hash
			uint rollingHashThreshold = (uint)((1L << 32) / options.LeafOptions.TargetSize);

			// Length of the data taken from the input span, updated as we step through it.
			int length = 0;

			// Step the window through the tail end of the existing payload window. In this state, update the hash to remove data from the current payload, and add data from the new payload.
			int splitLength = Math.Min(inputSpan.Length, windowSize);
			ReadOnlySequence<byte> splitPayload = Payload.Slice(Payload.Length - windowSize, splitLength);

			foreach (ReadOnlyMemory<byte> payloadSegment in splitPayload)
			{
				int baseLength = length;
				int spanLength = length + payloadSegment.Length;

				ReadOnlySpan<byte> payloadSpan = payloadSegment.Span;
				for (; length < spanLength; length++)
				{
					_rollingHash = BuzHash.Add(_rollingHash, inputSpan[length]);
					if (_rollingHash < rollingHashThreshold)
					{
						AppendLeafData(inputSpan.Slice(0, length));
						MarkComplete();
						return length;
					}
					_rollingHash = BuzHash.Sub(_rollingHash, payloadSpan[length - baseLength], windowSize);
				}
			}

			// Step through the new window.
			for (; length < inputSpan.Length; length++)
			{
				_rollingHash = BuzHash.Add(_rollingHash, inputSpan[length]);
				if (_rollingHash < rollingHashThreshold)
				{
					AppendLeafData(inputSpan.Slice(0, length));
					MarkComplete();
					return length;
				}
				_rollingHash = BuzHash.Sub(_rollingHash, inputSpan[length - windowSize], windowSize);
			}

			AppendLeafData(inputSpan);
			return inputSpan.Length;
		}

		private void AppendLeafData(ReadOnlySpan<byte> leafData)
		{
			if (_lastSegment == null)
			{
				byte[] buffer = CreateFirstSegmentData(Depth, leafData);
				_firstSegment = new DataSegment(0, buffer);
				_lastSegment = _firstSegment;
			}
			else
			{
				DataSegment newSegment = new DataSegment(_lastSegment.RunningIndex + _lastSegment.Memory.Length, leafData.ToArray());
				_lastSegment.SetNext(newSegment);
				_lastSegment = newSegment;
			}

			int headerSize = (int)(_data.Length - Payload.Length);
			_data = new ReadOnlySequence<byte>(_firstSegment!, 0, _lastSegment, _lastSegment.Memory.Length);
			Payload = _data.Slice(headerSize);

			Length += leafData.Length;
		}

		private static byte[] CreateFirstSegmentData(int depth, ReadOnlySpan<byte> leafData)
		{
			int depthBytes = VarInt.MeasureUnsigned(depth);

			byte[] buffer = new byte[1 + depthBytes + leafData.Length];
			buffer[0] = TypeId;

			VarInt.WriteUnsigned(buffer.AsSpan(1, depthBytes), depth);
			leafData.CopyTo(buffer.AsSpan(1 + depthBytes));

			return buffer;
		}

		private void MarkComplete()
		{
			if (_hash == null)
			{
				if (_childNodeRefs != null)
				{
					int depthBytes = VarInt.MeasureUnsigned(Depth);
					int lengthBytes = VarInt.MeasureUnsigned((ulong)Length);

					byte[] newData = new byte[1 + depthBytes + lengthBytes + (_childNodeRefs.Count * IoHash.NumBytes)];
					_data = new ReadOnlySequence<byte>(newData);
					Payload = new ReadOnlySequence<byte>(newData.AsMemory(1 + depthBytes));

					newData[0] = TypeId;
					Span<byte> span = newData.AsSpan(1);

					VarInt.WriteUnsigned(span, Depth);
					span = span.Slice(depthBytes);

					VarInt.WriteUnsigned(span, Length);
					span = span.Slice(lengthBytes);

					foreach (BundleNodeRef<FileNode> childNodeRef in _childNodeRefs)
					{
						childNodeRef.Hash.CopyTo(span);
						span = span.Slice(IoHash.NumBytes);
					}

					Debug.Assert(span.Length == 0);
				}
				_hash = IoHash.Compute(_data);
			}
		}

		private ReadOnlyMemory<byte> AppendToInteriorNode(ReadOnlyMemory<byte> newData, ChunkingOptions options)
		{
			for (; ; )
			{
				Debug.Assert(_childNodeRefs != null);

				// Try to write to the last node
				if (_childNodeRefs.Count > 0)
				{
					FileNode? lastNode = _childNodeRefs[^1].Node;
					if (lastNode != null)
					{
						// Update the length to match the new node
						Length -= lastNode.Length;
						newData = lastNode.AppendToNode(newData, options);
						Length += lastNode.Length;

						// If the last node is complete, write it to the buffer
						if (lastNode.IsReadOnly())
						{
							// Update the hash
							byte[] hashData = lastNode._hash!.Value.ToByteArray();
							_rollingHash = BuzHash.Add(_rollingHash, hashData);

							// Check if it's time to finish this chunk
							uint hashThreshold = (uint)(((1L << 32) * IoHash.NumBytes) / options.LeafOptions.TargetSize);
							if ((Payload.Length >= options.InteriorOptions.MinSize && _rollingHash < hashThreshold) || (Payload.Length >= options.InteriorOptions.MaxSize))
							{
								MarkComplete();
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
				FileNode childNode = new FileNode();
				childNode._isRoot = false;
				_childNodeRefs.Add(new BundleNodeRef<FileNode>(this, childNode));
			}
		}
	}

	/// <summary>
	/// Factory class for file nodes
	/// </summary>
	public class FileNodeDeserializer : BundleNodeDeserializer<FileNode>
	{
		/// <inheritdoc/>
		public override FileNode Deserialize(ReadOnlyMemory<byte> data) => FileNode.Deserialize(data);
	}
}
