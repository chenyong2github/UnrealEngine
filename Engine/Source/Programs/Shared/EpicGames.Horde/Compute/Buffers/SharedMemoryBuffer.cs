// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Core;
using System.IO.MemoryMappedFiles;
using System.IO;
using System.Threading.Tasks;
using System.Threading;
using System.Buffers.Binary;
using System.Runtime.InteropServices;

namespace EpicGames.Horde.Compute.Buffers
{
	/// <summary>
	/// Implementation of <see cref="IComputeBuffer"/> suitable for cross-process communication
	/// </summary>
	public sealed class SharedMemoryBuffer : ComputeBufferBase
	{
		const int HeaderLength = sizeof(int) + sizeof(int); // num chunks, chunk length

		sealed unsafe class Chunk : ChunkBase
		{
			readonly ulong* _statePtr;

			protected override ref ulong StateValue => ref *_statePtr;

			public Chunk(ulong* statePtr, Memory<byte> memory)
				: base(memory)
			{
				_statePtr = statePtr;
			}
		}

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
		private unsafe SharedMemoryBuffer(string name, MemoryMappedFile memoryMappedFile, MemoryMappedViewAccessor memoryMappedViewAccessor, MemoryMappedView memoryMappedView, Chunk[] chunks, Native.EventHandle readerEvent, Native.EventHandle writerEvent)
			: base(chunks, 1)
		{
			Name = name;

			_memoryMappedFile = memoryMappedFile;
			_memoryMappedViewAccessor = memoryMappedViewAccessor;
			_memoryMappedView = memoryMappedView;

			_readerEvent = readerEvent;
			_writerEvent = writerEvent;
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
			long capacity = HeaderLength + (numChunks * sizeof(ulong)) + (numChunks * chunkLength);

			name ??= $"Local\\COMPUTE_{Guid.NewGuid()}";

			MemoryMappedFile memoryMappedFile = MemoryMappedFile.CreateNew($"{name}_M", capacity, MemoryMappedFileAccess.ReadWrite, MemoryMappedFileOptions.None, HandleInheritability.Inheritable);
			MemoryMappedViewAccessor memoryMappedViewAccessor = memoryMappedFile.CreateViewAccessor();
			MemoryMappedView memoryMappedView = new MemoryMappedView(memoryMappedViewAccessor);

			Span<byte> header = memoryMappedView.GetMemory(0, 8).Span;
			BinaryPrimitives.WriteInt32LittleEndian(header, numChunks);
			BinaryPrimitives.WriteInt32LittleEndian(header.Slice(4), chunkLength);

			Native.EventHandle writerEvent = Native.EventHandle.CreateNew($"{name}_W", EventResetMode.ManualReset, true, HandleInheritability.Inheritable);
			Native.EventHandle readerEvent = Native.EventHandle.CreateNew($"{name}_R", EventResetMode.ManualReset, true, HandleInheritability.Inheritable);

			Chunk[] chunks = CreateChunks(numChunks, chunkLength, memoryMappedView);
			return new SharedMemoryBuffer(name, memoryMappedFile, memoryMappedViewAccessor, memoryMappedView, chunks, readerEvent, writerEvent);
		}

		static unsafe Chunk[] CreateChunks(int numChunks, int chunkLength, MemoryMappedView memoryMappedView)
		{
			Chunk[] chunks = new Chunk[numChunks];
			for (int chunkIdx = 0; chunkIdx < numChunks; chunkIdx++)
			{
				byte* statePtr = memoryMappedView.GetPointer() + HeaderLength + (chunkIdx * sizeof(ulong));
				int chunkOffset = HeaderLength + (numChunks * sizeof(ulong)) + (chunkLength * chunkIdx);

				Chunk chunk = new Chunk((ulong*)statePtr, memoryMappedView.GetMemory(chunkOffset, chunkLength));
				chunks[chunkIdx] = chunk;
			}
			return chunks;
		}

		/// <summary>
		/// Open an existing buffer by name
		/// </summary>
		/// <param name="name">Name of the buffer to open</param>
		public static SharedMemoryBuffer OpenExisting(string name)
		{
			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				throw new NotSupportedException();
			}

			MemoryMappedFile memoryMappedFile = MemoryMappedFile.OpenExisting($"{name}_M");
			MemoryMappedViewAccessor memoryMappedViewAccessor = memoryMappedFile.CreateViewAccessor();
			MemoryMappedView memoryMappedView = new MemoryMappedView(memoryMappedViewAccessor);

			ReadOnlySpan<byte> header = memoryMappedView.GetMemory(0, HeaderLength).Span;
			int numChunks = BinaryPrimitives.ReadInt32LittleEndian(header);
			int chunkLength = BinaryPrimitives.ReadInt32LittleEndian(header.Slice(4));

			Native.EventHandle readerEvent = Native.EventHandle.OpenExisting($"{name}_R");
			Native.EventHandle writerEvent = Native.EventHandle.OpenExisting($"{name}_W");

			Chunk[] chunks = CreateChunks(numChunks, chunkLength, memoryMappedView);
			return new SharedMemoryBuffer(name, memoryMappedFile, memoryMappedViewAccessor, memoryMappedView, chunks, readerEvent, writerEvent);
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
