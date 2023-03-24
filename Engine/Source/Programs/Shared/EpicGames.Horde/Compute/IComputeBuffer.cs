// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Base interface for input and output buffers
	/// </summary>
	public interface IComputeBuffer : IDisposable
	{
		/// <summary>
		/// Total length of this buffer
		/// </summary>
		long Length { get; }

		/// <summary>
		/// Reader for this buffer
		/// </summary>
		IComputeBufferReader Reader { get; }

		/// <summary>
		/// Writer for this buffer
		/// </summary>
		IComputeBufferWriter Writer { get; }

		/// <summary>
		/// Access the underlying memory for the buffer
		/// </summary>
		Memory<byte> GetMemory(long offset, int length);
	}

	/// <summary>
	/// Read interface for a compute buffer
	/// </summary>
	public interface IComputeBufferReader : IDisposable
	{
		/// <summary>
		/// Accessor for the buffer this is reading from
		/// </summary>
		IComputeBuffer Buffer { get; }

		/// <summary>
		/// Whether this buffer is complete (no more data will be added)
		/// </summary>
		bool IsComplete { get; }

		/// <summary>
		/// Updates the read position
		/// </summary>
		/// <param name="size">Size of data that was read</param>
		void Advance(int size);

		/// <summary>
		/// Gets the next data to read
		/// </summary>
		/// <returns>Memory to read from</returns>
		ReadOnlyMemory<byte> GetMemory();

		/// <summary>
		/// Wait for data to be available, or for the buffer to be marked as complete
		/// </summary>
		/// <param name="currentLength">Current length of the buffer</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask WaitForDataAsync(int currentLength, CancellationToken cancellationToken);
	}

	/// <summary>
	/// Buffer that can receive data from a remote machine.
	/// </summary>
	public interface IComputeBufferWriter : IDisposable
	{
		/// <summary>
		/// Accessor for the buffer we're writing to
		/// </summary>
		IComputeBuffer Buffer { get; }

		/// <summary>
		/// Mark the output to this buffer as complete
		/// </summary>
		void MarkComplete();

		/// <summary>
		/// Updates the current write position within the buffer
		/// </summary>
		void Advance(int size);

		/// <summary>
		/// Gets memory to write to
		/// </summary>
		/// <returns>Memory to be written to</returns>
		Memory<byte> GetMemory();

		/// <summary>
		/// Waits until all data in this buffer has been written
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask FlushAsync(CancellationToken cancellationToken);
	}

	/// <summary>
	/// Extension methods for <see cref="IComputeBuffer"/>
	/// </summary>
	public static class ComputeBufferExtensions
	{
		sealed class RefCountedBuffer
		{
			int _refFlags = 1 | 2;

			public IComputeBuffer Buffer { get; }

			public RefCountedBuffer(IComputeBuffer buffer) => Buffer = buffer;

			public void Release(int flag)
			{
				for (; ; )
				{
					int initialRefFlags = _refFlags;
					if (Interlocked.CompareExchange(ref _refFlags, initialRefFlags & ~flag, initialRefFlags) != initialRefFlags)
					{
						continue;
					}
					if (initialRefFlags == flag)
					{
						Buffer.Dispose();
					}
					break;
				}
			}
		}

		class RefCountedReader : IComputeBufferReader
		{
			public readonly RefCountedBuffer _buffer;
			public readonly IComputeBufferReader _reader;

			public IComputeBuffer Buffer => _reader.Buffer;

			public RefCountedReader(RefCountedBuffer refCountedBuffer)
			{
				_buffer = refCountedBuffer;
				_reader = refCountedBuffer.Buffer.Reader;
			}

			public void Dispose() => _buffer.Release(1);
			public bool IsComplete => _reader.IsComplete;
			public void Advance(int size) => _reader.Advance(size);
			public ReadOnlyMemory<byte> GetMemory() => _reader.GetMemory();
			public ValueTask WaitForDataAsync(int currentLength, CancellationToken cancellationToken) => _reader.WaitForDataAsync(currentLength, cancellationToken);
		}

		class RefCountedWriter : IComputeBufferWriter
		{
			public readonly RefCountedBuffer _buffer;
			public readonly IComputeBufferWriter _writer;

			public IComputeBuffer Buffer => _writer.Buffer;

			public RefCountedWriter(RefCountedBuffer refCountedBuffer)
			{
				_buffer = refCountedBuffer;
				_writer = refCountedBuffer.Buffer.Writer;
			}

			public void Dispose() => _buffer.Release(2);
			public void Advance(int size) => _writer.Advance(size);
			public ValueTask FlushAsync(CancellationToken cancellationToken) => _writer.FlushAsync(cancellationToken);
			public Memory<byte> GetMemory() => _writer.GetMemory();
			public void MarkComplete() => _writer.MarkComplete();
		}

		/// <summary>
		/// Converts a compute buffer to be disposable through its reader/writer 
		/// </summary>
		/// <param name="buffer"></param>
		/// <returns></returns>
		public static (IComputeBufferReader, IComputeBufferWriter) ToShared(this IComputeBuffer buffer)
		{
			RefCountedBuffer refCountedBuffer = new RefCountedBuffer(buffer);
			return (new RefCountedReader(refCountedBuffer), new RefCountedWriter(refCountedBuffer));
		}

		/// <summary>
		/// Waits until there is a block of the certain size in the buffer, and returns it
		/// </summary>
		/// <param name="reader">Instance to read from</param>
		/// <param name="minLength">Minimum length of the memory</param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public static async ValueTask<ReadOnlyMemory<byte>> GetMemoryAsync(this IComputeBufferReader reader, int minLength, CancellationToken cancellationToken)
		{
			for (; ; )
			{
				bool complete = reader.IsComplete;

				ReadOnlyMemory<byte> memory = reader.GetMemory();
				if (memory.Length >= minLength)
				{
					return memory;
				}
				else if (complete)
				{
					throw new EndOfStreamException();
				}
				await reader.WaitForDataAsync(memory.Length, cancellationToken);
			}
		}
	}
}
