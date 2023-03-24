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
	public interface IComputeMessageChannel : IAsyncDisposable
	{
		/// <summary>
		/// Reads a message from the channel
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Data for a message that was read. Must be disposed.</returns>
		ValueTask<IComputeMessage> ReceiveAsync(CancellationToken cancellationToken = default);

		/// <summary>
		/// Creates a new builder for a message
		/// </summary>
		/// <param name="type">Type of the message</param>
		/// <param name="maxSize">Maximum size of the message that will be written</param>
		/// <returns>New builder for messages</returns>
		IComputeMessageBuilder CreateMessage(ComputeMessageType type, int maxSize = 1024);
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
		public static void Send(this IComputeMessageChannel channel, IComputeMessage message)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(message.Type, message.Data.Length))
			{
				builder.WriteFixedLengthBytes(message.Data.Span);
				builder.Send();
			}
		}
	}
}
