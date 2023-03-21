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
		/// Reader from the buffer
		/// </summary>
		IComputeBufferReader Reader { get; }

		/// <summary>
		/// Writer to the buffer
		/// </summary>
		IComputeBufferWriter Writer { get; }
	}

	/// <summary>
	/// Read interface for a compute buffer
	/// </summary>
	public interface IComputeBufferReader
	{
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
		ValueTask WaitAsync(int currentLength, CancellationToken cancellationToken);
	}

	/// <summary>
	/// Buffer that can receive data from a remote machine.
	/// </summary>
	public interface IComputeBufferWriter
	{
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
	/// Default environment variable names for compute channels
	/// </summary>
	public static class ComputeEnvVarNames
	{
		/// <summary>
		/// Default name for a channel that can be used to send messages to the initiator
		/// </summary>
		public const string DefaultSendBuffer = "UE_HORDE_SEND_BUFFER";

		/// <summary>
		/// Default name for a channel that can be used to receive messages from the initiator
		/// </summary>
		public const string DefaultRecvBuffer = "UE_HORDE_RECV_BUFFER";
	}
}
