// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace EpicGames.Horde.Compute.Buffers
{
	/// <summary>
	/// Implementation of <see cref="IComputeBuffer"/> suitable for cross-process communication
	/// </summary>
	public sealed class PooledBuffer : IComputeBuffer
	{
		PooledBufferCore _core;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="capacity">Total capacity of the buffer</param>
		public PooledBuffer(int capacity)
			: this(2, capacity / 2)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="numChunks">Number of chunks in the buffer</param>
		/// <param name="chunkLength">Length of each chunk</param>
		public PooledBuffer(int numChunks, int chunkLength)
			: this(PooledBufferCore.Create(numChunks, chunkLength, 1))
		{
		}

		private PooledBuffer(PooledBufferCore core) => _core = core;

		/// <inheritdoc/>
		public IComputeBufferReader Reader => _core.Reader;

		/// <inheritdoc/>
		public IComputeBufferWriter Writer => _core.Writer;

		/// <inheritdoc cref="IComputeBuffer.AddRef"/>
		public PooledBuffer AddRef()
		{
			_core.AddRef();
			return new PooledBuffer(_core);
		}

		/// <inheritdoc/>
		IComputeBuffer IComputeBuffer.AddRef() => AddRef();

		/// <inheritdoc/>
		public void Dispose()
		{
			if (_core != null)
			{
				_core.Release();
				_core = null!;
			}
		}
	}

	/// <summary>
	/// In-process buffer used to store compute messages
	/// </summary>
	sealed class PooledBufferCore : ComputeBufferBase
	{
		unsafe class PinnedBuffer : IDisposable
		{
			readonly byte[] _data;
			readonly GCHandle _handle;

			public Memory<byte> Data => _data;
			public void* Ptr => _handle.AddrOfPinnedObject().ToPointer();

			public PinnedBuffer(int length)
			{
				_data = new byte[length];
				_handle = GCHandle.Alloc(_data, GCHandleType.Pinned);
			}

			public void Dispose()
			{
				_handle.Free();
			}
		}

		readonly PinnedBuffer[] _buffers;
		readonly AsyncEvent _writerEvent = new AsyncEvent();
		readonly AsyncEvent _readerEvent = new AsyncEvent();

		private PooledBufferCore(HeaderPtr headerPtr, Memory<byte>[] chunks, PinnedBuffer[] buffers)
			: base(headerPtr, chunks)
		{
			_buffers = buffers;
		}

		public static unsafe PooledBufferCore Create(int numChunks, int chunkLength, int numReaders)
		{
			Memory<byte>[] chunks = new Memory<byte>[numChunks];

			PinnedBuffer[] buffers = new PinnedBuffer[numChunks + 1];
			for (int idx = 0; idx < numChunks; idx++)
			{
				buffers[idx] = new PinnedBuffer(chunkLength);
				chunks[idx] = buffers[idx].Data;
			}

			buffers[numChunks] = new PinnedBuffer(HeaderSize);

			HeaderPtr headerPtr = new HeaderPtr((ulong*)buffers[numChunks].Ptr, numReaders, numChunks, chunkLength);
			return new PooledBufferCore(headerPtr, chunks, buffers);
		}

		/// <inheritdoc/>
		protected override void Dispose(bool disposing)
		{
			base.Dispose(disposing);

			if (disposing)
			{
				for (int idx = 0; idx < _buffers.Length; idx++)
				{
					_buffers[idx].Dispose();
				}
			}
		}

		/// <inheritdoc/>
		protected override void SetReadEvent(int readerIdx) => _readerEvent.Set();

		/// <inheritdoc/>
		protected override void ResetReadEvent(int readerIdx) => _readerEvent.Reset();

		/// <inheritdoc/>
		protected override Task WaitForReadEvent(int readerIdx, CancellationToken cancellationToken) => _readerEvent.Task.WaitAsync(cancellationToken);

		/// <inheritdoc/>
		protected override void SetWriteEvent() => _writerEvent.Set();

		/// <inheritdoc/>
		protected override void ResetWriteEvent() => _writerEvent.Reset();

		/// <inheritdoc/>
		protected override Task WaitForWriteEvent(CancellationToken cancellationToken) => _writerEvent.Task.WaitAsync(cancellationToken);
	}
}
