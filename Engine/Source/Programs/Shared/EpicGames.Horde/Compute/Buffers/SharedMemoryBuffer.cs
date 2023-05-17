// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Core;
using System.IO.MemoryMappedFiles;
using System.IO;
using System.Threading.Tasks;
using System.Threading;
using System.Runtime.InteropServices;

namespace EpicGames.Horde.Compute.Buffers
{
	/// <summary>
	/// Implementation of <see cref="IComputeBuffer"/> suitable for cross-process communication
	/// </summary>
	public sealed class SharedMemoryBuffer : IComputeBuffer
	{
		SharedMemoryBufferCore _core;

		/// <summary>
		/// Name of this shared memory buffer
		/// </summary>
		public string Name => _core.Name;

		private SharedMemoryBuffer(SharedMemoryBufferCore core)
		{
			_core = core;
		}

		/// <inheritdoc/>
		public IComputeBufferReader Reader => _core.Reader;

		/// <inheritdoc/>
		public IComputeBufferWriter Writer => _core.Writer;

		/// <inheritdoc cref="IComputeBuffer.AddRef"/>
		public SharedMemoryBuffer AddRef()
		{
			_core.AddRef();
			return new SharedMemoryBuffer(_core);
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

		/// <summary>
		/// Create a new shared memory buffer
		/// </summary>
		/// <param name="name">Name of the buffer</param>
		/// <param name="capacity">Capacity of the buffer</param>
		public static SharedMemoryBuffer CreateNew(string? name, long capacity)
		{
			int numChunks = (int)Math.Max(4, capacity / Int32.MaxValue);
			return CreateNew(name, numChunks, (int)(capacity / numChunks));
		}

		/// <summary>
		/// Create a new shared memory buffer
		/// </summary>
		/// <param name="name">Name of the buffer</param>
		/// <param name="numChunks">Number of chunks in the buffer</param>
		/// <param name="chunkLength">Length of each chunk</param>
		public static unsafe SharedMemoryBuffer CreateNew(string? name, int numChunks, int chunkLength)
		{
			SharedMemoryBufferCore core = SharedMemoryBufferCore.CreateNew(name, numChunks, chunkLength);
			return new SharedMemoryBuffer(core);
		}

		/// <summary>
		/// Open an existing buffer by name
		/// </summary>
		/// <param name="name">Name of the buffer to open</param>
		public static SharedMemoryBuffer OpenExisting(string name)
		{
			SharedMemoryBufferCore core = SharedMemoryBufferCore.OpenExisting(name);
			return new SharedMemoryBuffer(core);
		}
	}

	/// <summary>
	/// Core implementation of <see cref="SharedMemoryBuffer"/>
	/// </summary>
	sealed class SharedMemoryBufferCore : ComputeBufferBase
	{
		readonly MemoryMappedFile _memoryMappedFile;
		readonly MemoryMappedViewAccessor _memoryMappedViewAccessor;
		readonly MemoryMappedView _memoryMappedView;

		readonly Native.EventHandle _readerEvent;
		readonly Native.EventHandle _writerEvent;

		/// <inheritdoc/>
		public string Name { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		private unsafe SharedMemoryBufferCore(HeaderPtr headerPtr, string name, MemoryMappedFile memoryMappedFile, MemoryMappedViewAccessor memoryMappedViewAccessor, MemoryMappedView memoryMappedView, Native.EventHandle readerEvent, Native.EventHandle writerEvent)
			: base(headerPtr, CreateChunks(headerPtr.NumChunks, headerPtr.ChunkLength, memoryMappedView))
		{
			Name = name;

			_memoryMappedFile = memoryMappedFile;
			_memoryMappedViewAccessor = memoryMappedViewAccessor;
			_memoryMappedView = memoryMappedView;

			_readerEvent = readerEvent;
			_writerEvent = writerEvent;
		}

		public static unsafe SharedMemoryBufferCore CreateNew(string? name, int numChunks, int chunkLength)
		{
			long capacity = HeaderSize + (numChunks * sizeof(ulong)) + (numChunks * chunkLength);

			name ??= $"Local\\COMPUTE_{Guid.NewGuid()}";

			MemoryMappedFile memoryMappedFile = MemoryMappedFile.CreateNew($"{name}_M", capacity, MemoryMappedFileAccess.ReadWrite, MemoryMappedFileOptions.None, HandleInheritability.Inheritable);
			MemoryMappedViewAccessor memoryMappedViewAccessor = memoryMappedFile.CreateViewAccessor();
			MemoryMappedView memoryMappedView = new MemoryMappedView(memoryMappedViewAccessor);

			Native.EventHandle writerEvent = Native.EventHandle.CreateNew($"{name}_W", EventResetMode.ManualReset, true, HandleInheritability.Inheritable);
			Native.EventHandle readerEvent = Native.EventHandle.CreateNew($"{name}_R", EventResetMode.ManualReset, true, HandleInheritability.Inheritable);

			HeaderPtr headerPtr = new HeaderPtr((ulong*)memoryMappedView.GetPointer(), 1, numChunks, chunkLength);
			return new SharedMemoryBufferCore(headerPtr, name, memoryMappedFile, memoryMappedViewAccessor, memoryMappedView, readerEvent, writerEvent);
		}

		/// <summary>
		/// Open an existing buffer by name
		/// </summary>
		/// <param name="name">Name of the buffer to open</param>
		public static unsafe SharedMemoryBufferCore OpenExisting(string name)
		{
			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				throw new NotSupportedException();
			}

			MemoryMappedFile memoryMappedFile = MemoryMappedFile.OpenExisting($"{name}_M");
			MemoryMappedViewAccessor memoryMappedViewAccessor = memoryMappedFile.CreateViewAccessor();
			MemoryMappedView memoryMappedView = new MemoryMappedView(memoryMappedViewAccessor);

			Native.EventHandle readerEvent = Native.EventHandle.OpenExisting($"{name}_R");
			Native.EventHandle writerEvent = Native.EventHandle.OpenExisting($"{name}_W");

			HeaderPtr headerPtr = new HeaderPtr((ulong*)memoryMappedView.GetPointer());
			return new SharedMemoryBufferCore(headerPtr, name, memoryMappedFile, memoryMappedViewAccessor, memoryMappedView, readerEvent, writerEvent);
		}

		static unsafe Memory<byte>[] CreateChunks(int numChunks, int chunkLength, MemoryMappedView memoryMappedView)
		{
			Memory<byte>[] chunks = new Memory<byte>[numChunks];
			for (int chunkIdx = 0; chunkIdx < numChunks; chunkIdx++)
			{
				int chunkOffset = HeaderSize + (chunkLength * chunkIdx);
				chunks[chunkIdx] = memoryMappedView.GetMemory(chunkOffset, chunkLength);
			}
			return chunks;
		}

		/// <inheritdoc/>
		protected override void Dispose(bool disposing)
		{
			base.Dispose(disposing);

			if (disposing)
			{
				_readerEvent.Dispose();
				_writerEvent.Dispose();

				_memoryMappedView.Dispose();
				_memoryMappedViewAccessor.Dispose();
				_memoryMappedFile.Dispose();
			}
		}

		/// <inheritdoc/>
		protected override void SetReadEvent(int readerIdx) => _readerEvent.Set();

		/// <inheritdoc/>
		protected override void ResetReadEvent(int readerIdx) => _readerEvent.Reset();

		/// <inheritdoc/>
		protected override Task WaitForReadEvent(int readerIdx, CancellationToken cancellationToken) => _readerEvent.WaitOneAsync(cancellationToken);

		/// <inheritdoc/>
		protected override void SetWriteEvent() => _writerEvent.Set();

		/// <inheritdoc/>
		protected override void ResetWriteEvent() => _writerEvent.Reset();

		/// <inheritdoc/>
		protected override Task WaitForWriteEvent(CancellationToken cancellationToken) => _writerEvent.WaitOneAsync(cancellationToken);
	}
}
