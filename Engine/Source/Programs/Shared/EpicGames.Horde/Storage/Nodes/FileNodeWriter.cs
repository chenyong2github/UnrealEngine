// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Storage.Nodes
{
	/// <summary>
	/// Utility class for generating FileNode data directly into <see cref="TreeWriter"/> instances, without constructing node representations first.
	/// </summary>
	public class FileNodeWriter
	{
		class InteriorNodeState
		{
			public InteriorNodeState? _parent;
			public readonly ArrayMemoryWriter _data;
			public readonly List<NodeHandle> _children = new List<NodeHandle>();

			public uint _rollingHash;

			public InteriorNodeState(int maxSize)
			{
				_data = new ArrayMemoryWriter(maxSize);
			}

			public void Reset()
			{
				_rollingHash = 0;
				_data.Clear();
				_children.Clear();
			}

			public void Write(NodeHandle handle)
			{
				_children.Add(handle);
				_data.WriteIoHash(handle.Hash);
			}
		}

		static readonly BundleType s_leafNodeType = TreeNode.GetBundleType<LeafFileNode>();
		static readonly BundleType s_interiorNodeType = TreeNode.GetBundleType<InteriorFileNode>();

		readonly TreeWriter _writer;
		readonly ChunkingOptions _options;

		// Tree state
		InteriorNodeState? _topInteriorNode = null;
		long _totalLength;
		readonly Stack<InteriorNodeState> _freeInteriorNodes = new Stack<InteriorNodeState>();

		// Leaf node state
		uint _leafHash;
		int _leafLength;

		/// <summary>
		/// Length of the file so far
		/// </summary>
		public long Length => _totalLength;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="writer">Writer for new nodes</param>
		/// <param name="options">Chunking options</param>
		public FileNodeWriter(TreeWriter writer, ChunkingOptions options)
		{
			_writer = writer;
			_options = options;
		}

		/// <summary>
		/// Reset the current state
		/// </summary>
		public void Reset()
		{
			FreeInteriorNodes();
			ResetLeafState();
			_totalLength = 0;
		}

		/// <summary>
		/// Resets the state of the current leaf node
		/// </summary>
		void ResetLeafState()
		{
			_leafHash = 0;
			_leafLength = 0;
		}

		/// <summary>
		/// Creates a new interior node state
		/// </summary>
		/// <returns>State object</returns>
		InteriorNodeState CreateInteriorNode()
		{
			InteriorNodeState? result;
			if (!_freeInteriorNodes.TryPop(out result))
			{
				result = new InteriorNodeState(_options.InteriorOptions.MaxSize);
			}
			return result;
		}

		/// <summary>
		/// Free all the current interior nodes
		/// </summary>
		void FreeInteriorNodes()
		{
			while (_topInteriorNode != null)
			{
				InteriorNodeState current = _topInteriorNode;
				_topInteriorNode = _topInteriorNode._parent;
				current.Reset();
				_freeInteriorNodes.Push(current);
			}
		}

		/// <summary>
		/// Creates data for the given file
		/// </summary>
		/// <param name="fileInfo">File to append</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task<NodeHandle> CreateAsync(FileInfo fileInfo, CancellationToken cancellationToken)
		{
			using (FileStream stream = fileInfo.OpenRead())
			{
				return await CreateAsync(stream, cancellationToken);
			}
		}

		/// <summary>
		/// Creates data from the given stream
		/// </summary>
		/// <param name="stream">Stream to append</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task<NodeHandle> CreateAsync(Stream stream, CancellationToken cancellationToken)
		{
			Reset();
			await AppendAsync(stream, cancellationToken);
			return await CompleteAsync(cancellationToken);
		}

		/// <summary>
		/// Creates data from the given data
		/// </summary>
		/// <param name="data">Stream to append</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task<NodeHandle> CreateAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken)
		{
			Reset();
			await AppendAsync(data, cancellationToken);
			return await FlushAsync(cancellationToken);
		}

		/// <summary>
		/// Appends data to the current file
		/// </summary>
		/// <param name="stream">Stream containing data to append</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task AppendAsync(Stream stream, CancellationToken cancellationToken)
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
					await AppendAsync(appendBuffer, cancellationToken);
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
		}

		/// <summary>
		/// Appends data to the current file
		/// </summary>
		/// <param name="data">Data to append</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task AppendAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken)
		{
			Memory<byte> buffer = _writer.GetOutputBuffer(_leafLength, _leafLength);
			for (; ; )
			{
				// Append data to the current leaf node
				int appendLength = LeafFileNode.AppendData(buffer.Span.Slice(0, _leafLength), data.Span, ref _leafHash, _options.LeafOptions);

				buffer = _writer.GetOutputBuffer(_leafLength, _leafLength + appendLength);
				data.Slice(0, appendLength).CopyTo(buffer.Slice(_leafLength));

				_leafLength += appendLength;
				data = data.Slice(appendLength);

				_totalLength += appendLength;

				if (data.Length == 0)
				{
					break;
				}

				// Flush the leaf node and any interior nodes that are full
				NodeHandle handle = await WriteLeafNodeAsync(cancellationToken);
				ResetLeafState();
				await AddToInteriorNodeAsync(handle, cancellationToken);
			}
		}

		async Task AddToInteriorNodeAsync(NodeHandle handle, CancellationToken cancellationToken)
		{
			_topInteriorNode ??= CreateInteriorNode();
			await AddToInteriorNodeAsync(_topInteriorNode, handle, cancellationToken);
		}

		async Task AddToInteriorNodeAsync(InteriorNodeState interiorNode, NodeHandle handle, CancellationToken cancellationToken)
		{
			// If the node is already full, flush it
			if (InteriorFileNode.IsComplete(interiorNode._data.WrittenSpan, interiorNode._rollingHash, _options.InteriorOptions))
			{
				NodeHandle interiorNodeHandle = await WriteInteriorNodeAndResetAsync(interiorNode, cancellationToken);
				interiorNode._parent ??= CreateInteriorNode();
				await AddToInteriorNodeAsync(interiorNode._parent, interiorNodeHandle, cancellationToken);
			}

			// Add this handle
			InteriorFileNode.AppendData(interiorNode._data.WrittenSpan, handle.Hash, ref interiorNode._rollingHash, _options.InteriorOptions);
			interiorNode.Write(handle);
		}

		/// <summary>
		/// Complete the current file, and write all open nodes to the underlying writer
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Handle to the root node</returns>
		public async Task<NodeHandle> CompleteAsync(CancellationToken cancellationToken)
		{
			NodeHandle handle = await WriteLeafNodeAsync(cancellationToken);
			ResetLeafState();

			for (InteriorNodeState? state = _topInteriorNode; state != null; state = state._parent)
			{
				await AddToInteriorNodeAsync(state, handle, cancellationToken);
				handle = await WriteInteriorNodeAndResetAsync(state, cancellationToken);
			}

			FreeInteriorNodes();
			return handle;
		}

		/// <summary>
		/// Flush the state of the writer
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Handle to the root FileNode</returns>
		public async Task<NodeHandle> FlushAsync(CancellationToken cancellationToken)
		{
			NodeHandle handle = await CompleteAsync(cancellationToken);
			await _writer.FlushAsync(cancellationToken);
			return handle;
		}

		/// <summary>
		/// Writes the state of the given interior node to storage
		/// </summary>
		/// <param name="state"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		async ValueTask<NodeHandle> WriteInteriorNodeAndResetAsync(InteriorNodeState state, CancellationToken cancellationToken)
		{
			Memory<byte> buffer = _writer.GetOutputBuffer(0, state._data.Length);
			state._data.WrittenMemory.CopyTo(buffer);

			NodeHandle handle = await _writer.WriteNodeAsync(state._data.Length, state._children, s_interiorNodeType, cancellationToken);
			state.Reset();

			return handle;
		}

		/// <summary>
		/// Writes the contents of the current leaf node to storage
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Handle to the written leaf node</returns>
		async ValueTask<NodeHandle> WriteLeafNodeAsync(CancellationToken cancellationToken)
		{
			return await _writer.WriteNodeAsync(_leafLength, Array.Empty<NodeHandle>(), s_leafNodeType, cancellationToken);
		}
	}
}
