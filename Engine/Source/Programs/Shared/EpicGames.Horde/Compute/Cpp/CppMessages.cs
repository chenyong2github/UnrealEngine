// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute.Cpp
{
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
	/// Extension methods for C++ builds
	/// </summary>
	public static class ComputeChannelCppExtensions
	{
		/// <summary>
		/// Starts a C++ build task
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="locator">Locator for the node containing the task definition</param>
		/// <param name="replyChannelId">Channel to reply to with future messages</param>
		public static void CppStart(this IComputeChannel channel, NodeLocator locator, int replyChannelId)
		{
			using (IComputeMessageBuilder writer = channel.CreateMessage(ComputeMessageType.CppBegin))
			{
				writer.WriteNodeLocator(locator);
				writer.WriteUnsignedVarInt(replyChannelId);
				writer.Send();
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
		public static NodeLocator AsCppSuccess(this IComputeMessage message)
		{
			MemoryReader reader = new MemoryReader(message.Data);
			return reader.ReadNodeLocator();
		}

		/// <summary>
		/// Sends the result for a C++ compute task
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="locator">Locator for the result</param>
		public static void CppSuccess(this IComputeChannel channel, NodeLocator locator)
		{
			using (IComputeMessageBuilder writer = channel.CreateMessage(ComputeMessageType.CppSuccess))
			{
				writer.WriteNodeLocator(locator);
				writer.Send();
			}
		}

		/// <summary>
		/// Sends the result for a C++ compute task
		/// </summary>
		/// <param name="message"></param>
		public static string AsCppFailure(this IComputeMessage message)
		{
			MemoryReader reader = new MemoryReader(message.Data);
			return reader.ReadString();
		}

		/// <summary>
		/// Sends the failure error for a C++ compute task
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="error">Error string to send to the caller</param>
		public static void CppFailure(this IComputeChannel channel, string error)
		{
			using (IComputeMessageBuilder writer = channel.CreateMessage(ComputeMessageType.CppFailure))
			{
				writer.WriteString(error);
				writer.Send();
			}
		}

		/// <summary>
		/// Completes a C++ build task
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		public static void CppFinish(this IComputeChannel channel)
		{
			using (IComputeMessageBuilder writer = channel.CreateMessage(ComputeMessageType.CppEnd))
			{
				writer.Send();
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
		/// <returns></returns>
		public static void CppBlobRead(this IComputeChannel channel, BlobLocator locator, int offset, int length)
		{
			using (IComputeMessageBuilder writer = channel.CreateMessage(ComputeMessageType.CppBlobRead))
			{
				writer.WriteBlobLocator(locator);
				writer.WriteUnsignedVarInt(offset);
				writer.WriteUnsignedVarInt(length);
				writer.Send();
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

			using (IComputeMessageBuilder writer = channel.CreateMessage(ComputeMessageType.CppBlobData, length + 128))
			{
				writer.WriteFixedLengthBytes(data);
				writer.Send();
			}
		}
	}
}
