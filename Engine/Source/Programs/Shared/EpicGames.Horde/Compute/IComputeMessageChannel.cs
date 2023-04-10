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
	public interface IComputeMessageChannel : IDisposable
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
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New builder for messages</returns>
		ValueTask<IComputeMessageBuilder> CreateMessageAsync(ComputeMessageType type, int maxSize, CancellationToken cancellationToken);
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
	public static class ComputeMessageChannelExtensions
	{
		/// <summary>
		/// Waits for the remote machine to send a 'ready' response
		/// </summary>
		/// <param name="channel">Channel to receive on</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async ValueTask WaitForAttachAsync(this IComputeMessageChannel channel, CancellationToken cancellationToken = default)
		{
			IComputeMessage message = await channel.ReceiveAsync(cancellationToken);
			if (message.Type != ComputeMessageType.Ready)
			{
				throw new ComputeInvalidMessageException(message);
			}
		}

		/// <summary>
		/// Creates a new builder for a message
		/// </summary>
		/// <param name="channel">Channel to send on</param>
		/// <param name="type">Type of the message</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New builder for messages</returns>
		public static ValueTask<IComputeMessageBuilder> CreateMessageAsync(this IComputeMessageChannel channel, ComputeMessageType type, CancellationToken cancellationToken)
		{
			return channel.CreateMessageAsync(type, 1024, cancellationToken);
		}

		/// <summary>
		/// Forwards an existing message across a channel
		/// </summary>
		/// <param name="channel">Channel to send on</param>
		/// <param name="message">The message to be sent</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async ValueTask SendAsync(this IComputeMessageChannel channel, IComputeMessage message, CancellationToken cancellationToken)
		{
			using (IComputeMessageBuilder builder = await channel.CreateMessageAsync(message.Type, message.Data.Length, cancellationToken))
			{
				builder.WriteFixedLengthBytes(message.Data.Span);
				builder.Send();
			}
		}
	}
}
