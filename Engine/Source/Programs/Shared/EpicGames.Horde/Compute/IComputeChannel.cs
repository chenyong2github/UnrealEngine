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
	public interface IComputeChannel : IAsyncDisposable
	{
		/// <summary>
		/// Reads a message from the channel
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Data for a message that was read. Must be disposed.</returns>
		ValueTask<IComputeMessage> ReceiveAsync(CancellationToken cancellationToken);

		/// <summary>
		/// Creates a new builder for a message
		/// </summary>
		/// <param name="type">Type of the message</param>
		/// <param name="sizeHint">Hint for the expected message size</param>
		/// <returns>New builder for messages</returns>
		IComputeMessageBuilder CreateMessage(ComputeMessageType type, int sizeHint = 0);
	}

	/// <summary>
	/// Writer for compute messages
	/// </summary>
	public interface IComputeMessageBuilder : IMemoryWriter, IDisposable
	{
		/// <summary>
		/// Sends the current message
		/// </summary>
		void Send();
	}

	/// <summary>
	/// Extension methods to allow creating channels from leases
	/// </summary>
	public static class ComputeChannelExtensions
	{
		/// <summary>
		/// Forwards an existing message across a channel
		/// </summary>
		/// <param name="channel">Channel to send on</param>
		/// <param name="message">The message to be sent</param>
		public static void Send(this IComputeChannel channel, IComputeMessage message)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(message.Type, message.Data.Length))
			{
				builder.WriteFixedLengthBytes(message.Data.Span);
				builder.Send();
			}
		}
	}
}
