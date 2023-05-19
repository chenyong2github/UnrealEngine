// Copyright Epic Games, Inc. All Rights Reserved.

using System;
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
		/// Reader for this buffer
		/// </summary>
		ComputeBufferReader Reader { get; }

		/// <summary>
		/// Writer for this buffer
		/// </summary>
		ComputeBufferWriter Writer { get; }

		/// <summary>
		/// Creates a new reference to the underlying buffer. The underlying resources will only be destroyed once all instances are disposed of.
		/// </summary>
		IComputeBuffer AddRef();

		#region Reader interface

		/// <summary>
		/// Whether this buffer is complete (no more data will be added)
		/// </summary>
		/// <param name="readerIdx">Index of the reader</param>
		bool IsComplete(int readerIdx);

		/// <summary>
		/// Updates the read position
		/// </summary>
		/// <param name="readerIdx">Index of the reader</param>
		/// <param name="size">Size of data that was read</param>
		void AdvanceReadPosition(int readerIdx, int size);

		/// <summary>
		/// Gets the next data to read
		/// </summary>
		/// <param name="readerIdx">Index of the reader</param>
		/// <returns>Memory to read from</returns>
		ReadOnlyMemory<byte> GetReadBuffer(int readerIdx);

		/// <summary>
		/// Wait for data to be available, or for the buffer to be marked as complete
		/// </summary>
		/// <param name="readerIdx">Index of the reader</param>
		/// <param name="minLength">Minimum amount of data to read</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>True if new data is available, false if the buffer is complete</returns>
		ValueTask<bool> WaitToReadAsync(int readerIdx, int minLength, CancellationToken cancellationToken = default);

		#endregion

		#region Writer interface

		/// <summary>
		/// Mark the output to this buffer as complete
		/// </summary>
		/// <returns>Whether the writer was marked as complete. False if the writer has already been marked as complete.</returns>
		bool MarkComplete();

		/// <summary>
		/// Updates the current write position within the buffer
		/// </summary>
		void AdvanceWritePosition(int size);

		/// <summary>
		/// Gets memory to write to
		/// </summary>
		/// <returns>Memory to be written to</returns>
		Memory<byte> GetWriteBuffer();

		/// <summary>
		/// Gets memory to write to
		/// </summary>
		/// <param name="minLength">Minimum size of the desired write buffer</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Memory to be written to</returns>
		ValueTask WaitToWriteAsync(int minLength, CancellationToken cancellationToken = default);

		#endregion
	}

	/// <summary>
	/// Wraps the read methods for a compute buffer
	/// </summary>
	public sealed class ComputeBufferReader
	{
		readonly IComputeBuffer _buffer;
		readonly int _readerIdx;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeBufferReader(IComputeBuffer buffer, int readerIdx)
		{
			_buffer = buffer;
			_readerIdx = readerIdx;
		}

		/// <inheritdoc cref="IComputeBuffer.IsComplete(Int32)"/>
		public bool IsComplete => _buffer.IsComplete(_readerIdx);

		/// <inheritdoc cref="IComputeBuffer.AdvanceReadPosition(Int32, Int32)"/>
		public void AdvanceReadPosition(int size) => _buffer.AdvanceReadPosition(_readerIdx, size);

		/// <inheritdoc cref="IComputeBuffer.GetReadBuffer(Int32)"/>
		public ReadOnlyMemory<byte> GetReadBuffer() => _buffer.GetReadBuffer(_readerIdx);

		/// <inheritdoc cref="IComputeBuffer.WaitToReadAsync(Int32, Int32, CancellationToken)"/>
		public ValueTask<bool> WaitToReadAsync(int minLength, CancellationToken cancellationToken = default) => _buffer.WaitToReadAsync(_readerIdx, minLength, cancellationToken);
	}

	/// <summary>
	/// Wraps the write methods for a compute buffer
	/// </summary>
	public sealed class ComputeBufferWriter
	{
		readonly IComputeBuffer _buffer;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeBufferWriter(IComputeBuffer buffer) => _buffer = buffer;

		/// <inheritdoc cref="IComputeBuffer.MarkComplete"/>
		public bool MarkComplete() => _buffer.MarkComplete();

		/// <inheritdoc cref="IComputeBuffer.AdvanceWritePosition(Int32)"/>
		public void AdvanceWritePosition(int size) => _buffer.AdvanceWritePosition(size);

		/// <inheritdoc cref="IComputeBuffer.GetWriteBuffer()"/>
		public Memory<byte> GetWriteBuffer() => _buffer.GetWriteBuffer();

		/// <inheritdoc cref="IComputeBuffer.WaitToWriteAsync(Int32, CancellationToken)"/>
		public ValueTask WaitToWriteAsync(int minLength, CancellationToken cancellationToken = default) => _buffer.WaitToWriteAsync(minLength, cancellationToken);
	}

	/// <summary>
	/// Extension methods for <see cref="IComputeBuffer"/>
	/// </summary>
	public static class ComputeBufferExtensions
	{
		/// <summary>
		/// Read from a buffer into another buffer
		/// </summary>
		/// <param name="reader">Buffer to read from</param>
		/// <param name="buffer">Memory to receive the read data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Number of bytes read</returns>
		public static async ValueTask<int> ReadAsync(this ComputeBufferReader reader, Memory<byte> buffer, CancellationToken cancellationToken = default)
		{
			for (; ; )
			{
				ReadOnlyMemory<byte> readMemory = reader.GetReadBuffer();
				if (reader.IsComplete || readMemory.Length > 0)
				{
					int length = Math.Min(readMemory.Length, buffer.Length);
					readMemory.Slice(0, length).CopyTo(buffer);
					reader.AdvanceReadPosition(length);
					return length;
				}
				await reader.WaitToReadAsync(1, cancellationToken);
			}
		}

		/// <summary>
		/// Writes data into a buffer from a memory block
		/// </summary>
		/// <param name="writer">Writer to output the data to</param>
		/// <param name="buffer">The data to write</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async ValueTask WriteAsync(this ComputeBufferWriter writer, ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken = default)
		{
			while (buffer.Length > 0)
			{
				Memory<byte> writeMemory = writer.GetWriteBuffer();
				if (writeMemory.Length >= buffer.Length)
				{
					buffer.CopyTo(writeMemory);
					writer.AdvanceWritePosition(buffer.Length);
					break;
				}
				await writer.WaitToWriteAsync(writeMemory.Length, cancellationToken);
			}
		}
	}
}
