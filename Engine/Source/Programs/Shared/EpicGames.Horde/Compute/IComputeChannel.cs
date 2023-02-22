// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Full-duplex channel for sending and reciving messages
	/// </summary>
	public interface IComputeChannel
	{
		/// <summary>
		/// Reads a message from the channel
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Data for the message that was read. This buffer is ephemeral, and may be overwritten on the next read call.</returns>
		Task<ReadOnlyMemory<byte>> ReadMessageAsync(CancellationToken cancellationToken);

		/// <summary>
		/// Writes a message to the channel
		/// </summary>
		/// <param name="buffer">Buffer containing the data to be written</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		Task WriteMessageAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken);
	}
}
