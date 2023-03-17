// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Base interface for input and output buffers
	/// </summary>
	public interface IComputeBuffer
	{
		/// <summary>
		/// Identifier for the buffer
		/// </summary>
		int Id { get; }
	}

	/// <summary>
	/// Read interface for a compute buffer
	/// </summary>
	public interface IComputeInputBuffer : IComputeBuffer, IDisposable
	{
		/// <summary>
		/// The shared memory for the buffer
		/// </summary>
		ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Offset within the data that is still to be used. Can be left at zero for a buffer that is only appended to, but must be updated to indicate data
		/// that has been processed for message buffers (in order to coordinate when the buffer contents can be discarded during a reset).
		/// </summary>
		long ReadPosition { get; }

		/// <summary>
		/// Position in the buffer at which new data will be added
		/// </summary>
		long WritePosition { get; }

		/// <summary>
		/// Updates the current read position within the buffer
		/// </summary>
		void AdvanceReadPosition(long size);

		/// <summary>
		/// Waits until new data is written to the buffer
		/// </summary>
		/// <param name="currentWritePosition">Current write position. This is passed as an argument to avoid races with <see cref="WritePosition"/> being updated asynchronously.</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask WaitForWrittenDataAsync(long currentWritePosition, CancellationToken cancellationToken);
	}

	/// <summary>
	/// Read-only view of shared memory that can be incrementally replicated to a remote machine.
	/// </summary>
	public interface IComputeOutputBuffer : IComputeBuffer, IDisposable
	{
		/// <summary>
		/// The shared memory for the buffer
		/// </summary>
		Memory<byte> Data { get; }

		/// <summary>
		/// Current write position/used length of the buffer
		/// </summary>
		long WritePosition { get; }

		/// <summary>
		/// Updates the current write position within the buffer
		/// </summary>
		void AdvanceWritePosition(long size);

		/// <summary>
		/// Flushes all data that needs to be written.
		/// </summary>
		ValueTask FlushAsync(CancellationToken cancellationToken);

		/// <summary>
		/// Flushes all writes, and resets the current write position.
		/// </summary>
		ValueTask ResetWritePositionAsync(CancellationToken cancellationToken);
	}

	/// <summary>
	/// Extension methods for compute buffers
	/// </summary>
	static class ComputeBufferExtensions
	{
		/// <summary>
		/// Gets a span for the next data to read
		/// </summary>
		public static ReadOnlySpan<byte> GetReadSpan(this IComputeInputBuffer buffer) => GetReadMemory(buffer).Span;

		/// <summary>
		/// Gets a span for the next data to read
		/// </summary>
		public static ReadOnlyMemory<byte> GetReadMemory(this IComputeInputBuffer buffer) => buffer.Data.Slice((int)buffer.ReadPosition, (int)(buffer.WritePosition - buffer.ReadPosition));

		/// <summary>
		/// Gets a span to write new data to
		/// </summary>
		public static Span<byte> GetWriteSpan(this IComputeOutputBuffer buffer) => buffer.Data.Slice((int)buffer.WritePosition).Span;
	}
}
