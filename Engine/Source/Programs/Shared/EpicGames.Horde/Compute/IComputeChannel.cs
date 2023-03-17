// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Full-duplex channel for sending and receiving messages
	/// </summary>
	public interface IComputeChannel : IDisposable
	{
		/// <summary>
		/// Identifier for the channel
		/// </summary>
		int Id { get; }

		/// <summary>
		/// Reads a message from the channel
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Data for a message that was read. Must be disposed.</returns>
		ValueTask<IComputeMessage> ReadAsync(CancellationToken cancellationToken);

		/// <summary>
		/// Creates a new builder for a message
		/// </summary>
		/// <param name="type">Type of the message</param>
		/// <param name="sizeHint">Hint for the expected message size</param>
		/// <returns>New builder for messages</returns>
		IComputeMessageWriter CreateMessage(ComputeMessageType type, int sizeHint = 0);
	}

	/// <summary>
	/// Writer for compute messages
	/// </summary>
	public interface IComputeMessageWriter : IMemoryWriter, IDisposable
	{
		/// <summary>
		/// Sends the current message
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask SendAsync(CancellationToken cancellationToken);
	}

	/// <summary>
	/// Extension methods to allow creating channels from leases
	/// </summary>
	public static class ComputeChannelExtensions
	{
		/// <summary>
		/// Creates a compute channel with the given identifier
		/// </summary>
		/// <param name="lease">The lease to create a channel for</param>
		/// <param name="id">Identifier for the channel</param>
		public static IComputeChannel CreateChannel(this IComputeLease lease, int id)
		{
			const int MaxMessageSize = 64 * 1024;
			const int BufferSize = MaxMessageSize * 3;

			IComputeInputBuffer inputBuffer = lease.CreateInputBuffer(id, MemoryPool<byte>.Shared.Rent(BufferSize));
			IComputeOutputBuffer outputBuffer = lease.CreateOutputBuffer(id, MemoryPool<byte>.Shared.Rent(BufferSize));

			return new ComputeChannel(id, inputBuffer, outputBuffer, MaxMessageSize);
		}
	}
}
