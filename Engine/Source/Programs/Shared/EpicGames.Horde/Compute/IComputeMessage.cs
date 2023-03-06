// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Type of a compute message
	/// </summary>
	public enum ComputeMessageType
	{
		/// <summary>
		/// No message was received (end of stream)
		/// </summary>
		None = 0x00,

		/// <summary>
		/// Gracefully close the current connection
		/// </summary>
		Close = 0x01,

		#region Test Requests

		/// <summary>
		/// Xor a block of data with a value
		/// </summary>
		XorRequest = 0x10,

		/// <summary>
		/// Result from an <see cref="XorRequest"/> request.
		/// </summary>
		XorResponse = 0x11,

		#endregion

		#region C++ Builds

		/// <summary>
		/// Initiate a C++ build
		/// </summary>
		CppBegin = 0x20,

		/// <summary>
		/// Gives output from a C++ build
		/// </summary>
		CppResult = 0x21,

		/// <summary>
		/// Finish a C++ build and close the current channel
		/// </summary>
		CppEnd = 0x22,

		/// <summary>
		/// Reads a blob pertaining to a C++ build
		/// </summary>
		CppBlobRead = 0x23,

		/// <summary>
		/// Response to a <see cref="CppBlobRead"/> request.
		/// </summary>
		CppBlobData = 0x24,

		#endregion
	}

	/// <summary>
	/// Information about a received compute message
	/// </summary>
	public interface IComputeMessage : IDisposable
	{
		/// <summary>
		/// Type of the message
		/// </summary>
		public ComputeMessageType Type { get; }

		/// <summary>
		/// Data that was read
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }
	}

	/// <summary>
	/// Message for running an XOR command
	/// </summary>
	/// <param name="Data">Data to xor</param>
	/// <param name="Value">Value to XOR with</param>
	public record struct XorRequestMessage(ReadOnlyMemory<byte> Data, byte Value);

	/// <summary>
	/// Message for starting a C++ build
	/// </summary>
	/// <param name="Locator">Locator for the task description</param>
	/// <param name="ReplyChannelId">Channel to reply on</param>
	public record struct CppBeginMessage(NodeLocator Locator, int ReplyChannelId);

	/// <summary>
	/// Creates a blob read request
	/// </summary>
	public record struct CppBlobReadMessage(BlobLocator Locator, int Offset, int Length);

	/// <summary>
	/// Output from a C++ build request
	/// </summary>
	public record struct CppOutputMessage(NodeLocator Locator);

	/// <summary>
	/// Wraps various requests across compute channels
	/// </summary>
	public static class ComputeChannelExtensions
	{
		/// <summary>
		/// Send a close message to the remote
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task CloseAsync(this IComputeChannel channel, CancellationToken cancellationToken)
		{
			using (IComputeMessageWriter writer = channel.CreateMessage(ComputeMessageType.Close))
			{
				await writer.SendAsync(cancellationToken);
			}
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="channel"></param>
		/// <param name="data"></param>
		/// <param name="value"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public static async Task XorRequestAsync(this IComputeChannel channel, ReadOnlyMemory<byte> data, byte value, CancellationToken cancellationToken)
		{
			using (IComputeMessageWriter writer = channel.CreateMessage(ComputeMessageType.XorRequest))
			{
				writer.WriteFixedLengthBytes(data.Span);
				writer.WriteUInt8(value);
				await writer.SendAsync(cancellationToken);
			}
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="message"></param>
		/// <returns></returns>
		public static XorRequestMessage AsXorRequest(this IComputeMessage message)
		{
			ReadOnlyMemory<byte> data = message.Data;
			return new XorRequestMessage(data[0..^1], data.Span[^1]);
		}

		/// <summary>
		/// Starts a C++ build task
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="locator">Locator for the node containing the task definition</param>
		/// <param name="replyChannelId">Channel to reply to with future messages</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task CppStartAsync(this IComputeChannel channel, NodeLocator locator, int replyChannelId, CancellationToken cancellationToken)
		{
			using (IComputeMessageWriter writer = channel.CreateMessage(ComputeMessageType.CppBegin))
			{
				writer.WriteNodeLocator(locator);
				writer.WriteUnsignedVarInt(replyChannelId);
				await writer.SendAsync(cancellationToken);
			}
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="message"></param>
		/// <returns></returns>
		public static CppBeginMessage AsCppBegin(this IComputeMessage message)
		{
			MemoryReader reader = new MemoryReader(message.Data);
			NodeLocator locator = reader.ReadNodeLocator();
			int replyChannelId = (int)reader.ReadUnsignedVarInt();
			return new CppBeginMessage(locator, replyChannelId);
		}

		/// <summary>
		/// Sends the result for a C++ compute task
		/// </summary>
		/// <param name="message"></param>
		public static NodeLocator AsCppResult(this IComputeMessage message)
		{
			MemoryReader reader = new MemoryReader(message.Data);
			return reader.ReadNodeLocator();
		}

		/// <summary>
		/// Sends the result for a C++ compute task
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="locator">Locator for the result</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task CppResultAsync(this IComputeChannel channel, NodeLocator locator, CancellationToken cancellationToken)
		{
			using (IComputeMessageWriter writer = channel.CreateMessage(ComputeMessageType.CppResult))
			{
				writer.WriteNodeLocator(locator);
				await writer.SendAsync(cancellationToken);
			}
		}

		/// <summary>
		/// Completes a C++ build task
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task CppFinishAsync(this IComputeChannel channel, CancellationToken cancellationToken)
		{
			using (IComputeMessageWriter writer = channel.CreateMessage(ComputeMessageType.CppEnd))
			{
				await writer.SendAsync(cancellationToken);
			}
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="message"></param>
		/// <returns></returns>
		public static CppBlobReadMessage AsCppBlobRead(this IComputeMessage message)
		{
			MemoryReader reader = new MemoryReader(message.Data);
			BlobLocator locator = reader.ReadBlobLocator();
			int offset = (int)reader.ReadUnsignedVarInt();
			int length = (int)reader.ReadUnsignedVarInt();
			return new CppBlobReadMessage(locator, offset, length);
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="channel"></param>
		/// <param name="locator"></param>
		/// <param name="offset"></param>
		/// <param name="length"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public static async Task CppBlobReadAsync(this IComputeChannel channel, BlobLocator locator, int offset, int length, CancellationToken cancellationToken)
		{
			using (IComputeMessageWriter writer = channel.CreateMessage(ComputeMessageType.CppBlobRead))
			{
				writer.WriteBlobLocator(locator);
				writer.WriteUnsignedVarInt(offset);
				writer.WriteUnsignedVarInt(length);
				await writer.SendAsync(cancellationToken);
			}
		}

		/// <summary>
		/// Writes blob data to a compute channel
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="message">The read request</param>
		/// <param name="storage">Storage client to retrieve the blob from</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static Task CppBlobDataAsync(this IComputeChannel channel, CppBlobReadMessage message, IStorageClient storage, CancellationToken cancellationToken)
		{
			return CppBlobDataAsync(channel, message.Locator, message.Offset, message.Length, storage, cancellationToken);
		}

		/// <summary>
		/// Writes blob data to a compute channel
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="locator">Locator for the blob to send</param>
		/// <param name="offset">Starting offset of the data</param>
		/// <param name="length">Length of the data</param>
		/// <param name="storage">Storage client to retrieve the blob from</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task CppBlobDataAsync(this IComputeChannel channel, BlobLocator locator, int offset, int length, IStorageClient storage, CancellationToken cancellationToken)
		{
			byte[] data;
			if (offset == 0 && length == 0)
			{
				using (Stream stream = await storage.ReadBlobAsync(locator, cancellationToken))
				{
					using MemoryStream target = new MemoryStream();
					await stream.CopyToAsync(target, cancellationToken);
					data = target.ToArray();
				}
			}
			else
			{
				using (Stream stream = await storage.ReadBlobRangeAsync(locator, offset, length, cancellationToken))
				{
					using MemoryStream target = new MemoryStream();
					await stream.CopyToAsync(target, cancellationToken);
					data = target.ToArray();
				}
			}

			using (IComputeMessageWriter writer = channel.CreateMessage(ComputeMessageType.CppBlobData, length + 128))
			{
				writer.WriteFixedLengthBytes(data);
				await writer.SendAsync(cancellationToken);
			}
		}
	}
}
