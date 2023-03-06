// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Full-duplex channel for sending and receiving messages
	/// </summary>
	public interface IComputeChannel
	{
		/// <summary>
		/// Identifier for the current channel
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
}
