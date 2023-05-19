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
	/// Core implementation of <see cref="SharedMemoryBuffer"/>
	/// </summary>
	public sealed class SharedMemoryBuffer : ComputeBufferBase
	{
		class Resources : ResourcesBase
		{
			readonly MemoryMappedFile _memoryMappedFile;
			readonly MemoryMappedViewAccessor _memoryMappedViewAccessor;
			readonly MemoryMappedView _memoryMappedView;

			readonly Native.EventHandle _readerEvent;
			readonly Native.EventHandle _writerEvent;

			public string Name { get; }
			public HeaderPtr HeaderPtr { get; }
			public Memory<byte>[] Chunks { get; }

			public Resources(string name, HeaderPtr headerPtr, MemoryMappedFile memoryMappedFile, MemoryMappedViewAccessor memoryMappedViewAccessor, MemoryMappedView memoryMappedView, Native.EventHandle readerEvent, Native.EventHandle writerEvent)
			{
				Name = name;
				HeaderPtr = headerPtr;

				_memoryMappedFile = memoryMappedFile;
				_memoryMappedViewAccessor = memoryMappedViewAccessor;
				_memoryMappedView = memoryMappedView;

				_readerEvent = readerEvent;
				_writerEvent = writerEvent;

				Chunks = new Memory<byte>[headerPtr.NumChunks];
				for (int chunkIdx = 0; chunkIdx < headerPtr.NumChunks; chunkIdx++)
				{
					int chunkOffset = HeaderSize + (headerPtr.ChunkLength * chunkIdx);
					Chunks[chunkIdx] = memoryMappedView.GetMemory(chunkOffset, headerPtr.ChunkLength);
				}
			}

			public override void Dispose()
			{
				_readerEvent.Dispose();
				_writerEvent.Dispose();

				_memoryMappedView.Dispose();
				_memoryMappedViewAccessor.Dispose();
				_memoryMappedFile.Dispose();
			}

			/// <inheritdoc/>
			public override void SetReadEvent(int readerIdx) => _readerEvent.Set();

			/// <inheritdoc/>
			public override void ResetReadEvent(int readerIdx) => _readerEvent.Reset();

			/// <inheritdoc/>
			public override Task WaitForReadEvent(int readerIdx, CancellationToken cancellationToken) => _readerEvent.WaitOneAsync(cancellationToken);

			/// <inheritdoc/>
			public override void SetWriteEvent() => _writerEvent.Set();

			/// <inheritdoc/>
			public override void ResetWriteEvent() => _writerEvent.Reset();

			/// <inheritdoc/>
			public override Task WaitForWriteEvent(CancellationToken cancellationToken) => _writerEvent.WaitOneAsync(cancellationToken);
		}

		/// <inheritdoc/>
		public string Name => _resources.Name;

		readonly Resources _resources;

		/// <summary>
		/// Constructor
		/// </summary>
		private unsafe SharedMemoryBuffer(Resources resources)
			: base(resources.HeaderPtr, resources.Chunks, resources)
		{
			_resources = resources;
		}

		/// <inheritdoc/>
		public override IComputeBuffer AddRef()
		{
			_resources.AddRef();
			return new SharedMemoryBuffer(_resources);
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
			long capacity = HeaderSize + (numChunks * sizeof(ulong)) + (numChunks * chunkLength);

			name ??= $"Local\\COMPUTE_{Guid.NewGuid()}";

			MemoryMappedFile memoryMappedFile = MemoryMappedFile.CreateNew($"{name}_M", capacity, MemoryMappedFileAccess.ReadWrite, MemoryMappedFileOptions.None, HandleInheritability.Inheritable);
			MemoryMappedViewAccessor memoryMappedViewAccessor = memoryMappedFile.CreateViewAccessor();
			MemoryMappedView memoryMappedView = new MemoryMappedView(memoryMappedViewAccessor);

			Native.EventHandle writerEvent = Native.EventHandle.CreateNew($"{name}_W", EventResetMode.ManualReset, true, HandleInheritability.Inheritable);
			Native.EventHandle readerEvent = Native.EventHandle.CreateNew($"{name}_R", EventResetMode.ManualReset, true, HandleInheritability.Inheritable);

			HeaderPtr headerPtr = new HeaderPtr((ulong*)memoryMappedView.GetPointer(), 1, numChunks, chunkLength);
			return new SharedMemoryBuffer(new Resources(name, headerPtr, memoryMappedFile, memoryMappedViewAccessor, memoryMappedView, readerEvent, writerEvent));
		}

		/// <summary>
		/// Open an existing buffer by name
		/// </summary>
		/// <param name="name">Name of the buffer to open</param>
		public static unsafe SharedMemoryBuffer OpenExisting(string name)
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
			return new SharedMemoryBuffer(new Resources(name, headerPtr, memoryMappedFile, memoryMappedViewAccessor, memoryMappedView, readerEvent, writerEvent));
		}
	}
}
