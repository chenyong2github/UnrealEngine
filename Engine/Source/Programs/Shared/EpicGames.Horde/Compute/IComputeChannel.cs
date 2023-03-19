// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute.Buffers;

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
		class RentedBufferChannel : ComputeChannel
		{
			readonly RentedLocalBuffer _receiveBuffer;
			readonly RentedLocalBuffer _sendBuffer;

			public RentedBufferChannel(RentedLocalBuffer receiveBuffer, RentedLocalBuffer sendBuffer)
				: base(receiveBuffer.Reader, sendBuffer.Writer)
			{
				_sendBuffer = sendBuffer;
				_receiveBuffer = receiveBuffer;
			}

			/// <inheritdoc/>
			protected override async ValueTask DisposeAsync(bool disposing)
			{
				await base.DisposeAsync(disposing);

				if (disposing)
				{
					_sendBuffer.Dispose();
					_receiveBuffer.Dispose();
				}
			}
		}

		/// <summary>
		/// Creates a compute channel with the given identifier
		/// </summary>
		/// <param name="lease">The lease to create a channel for</param>
		/// <param name="id">Identifier for the channel</param>
		public static IComputeChannel CreateChannel(this IComputeLease lease, int id)
		{
			const int MaxMessageSize = 64 * 1024;
			const int BufferSize = MaxMessageSize * 3;

			RentedLocalBuffer receiveBuffer = new RentedLocalBuffer(BufferSize);
			lease.AttachReceiveBuffer(id, receiveBuffer.Writer);

			RentedLocalBuffer sendBuffer = new RentedLocalBuffer(BufferSize);
			lease.AttachSendBuffer(id, sendBuffer.Reader);

			return new RentedBufferChannel(receiveBuffer, sendBuffer);
		}
	}
}
