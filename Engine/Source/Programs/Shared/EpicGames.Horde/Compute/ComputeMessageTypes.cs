// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

#pragma warning disable CS1591
#pragma warning disable CA1819

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Terminate the current connection
	/// </summary>
	[ComputeMessage("close"), CbObject]
	public class CloseMessage
	{
	}

	/// <summary>
	/// XOR a block of data with a value
	/// </summary>
	[ComputeMessage("xor-req")]
	public class XorRequestMessage
	{
		[CbField]
		public byte Value { get; set; }

		[CbField]
		public byte[] Payload { get; set; } = Array.Empty<byte>();
	}

	/// <summary>
	/// Response from an XOR message
	/// </summary>
	[ComputeMessage("xor-rsp")]
	public class XorResponseMessage
	{
		[CbField]
		public byte[] Payload { get; set; } = Array.Empty<byte>();
	}

	/// <summary>
	/// Requests a C++ compilation
	/// </summary>
	[ComputeMessage("cpp")]
	public class CppComputeMessage
	{
		[CbField]
		public NodeLocator Locator { get; set; }
	}

	/// <summary>
	/// Output from a C++ build job
	/// </summary>
	[ComputeMessage("cpp-out")]
	public class CppComputeOutputMessage
	{
		[CbField]
		public NodeLocator Locator { get; set; }
	}

	/// <summary>
	/// Indicates that a C++ compute job is complete, and the sandbox can be discarded.
	/// </summary>
	[ComputeMessage("cpp-finish"), CbObject]
	public class CppComputeFinishMessage
	{
	}

	/// <summary>
	/// Reads a blob from the remote
	/// </summary>
	[ComputeMessage("blob-read")]
	public class BlobReadMessage
	{
		[CbField("loc")]
		public BlobLocator Locator { get; set; }

		[CbField("ofs")]
		public int Offset { get; set; }

		[CbField("len")]
		public int Length { get; set; }
	}

	/// <summary>
	/// Reads a blob from the remote
	/// </summary>
	[ComputeMessage("blob-data")]
	public class BlobDataMessage
	{
		[CbField]
		public ReadOnlyMemory<byte> Data { get; set; }
	}

	/// <summary>
	/// Extension methods for sending compute messages
	/// </summary>
	public static class ComputeMessageExtensions
	{
		/// <summary>
		/// Reads data for a blob from a remote
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="locator">Locator for the blob to send</param>
		/// <param name="offset">Starting offset of the data</param>
		/// <param name="length">Length of the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Data for the blob</returns>
		public static async Task<ReadOnlyMemory<byte>> ReadBlobDataAsync(this IComputeChannel channel, BlobLocator locator, int offset, int length, CancellationToken cancellationToken)
		{
			BlobReadMessage message = new BlobReadMessage { Locator = locator, Offset = offset, Length = length };
			await channel.WriteCbMessageAsync(message, cancellationToken);

			BlobDataMessage response = await channel.ReadCbMessageAsync<BlobDataMessage>(cancellationToken);
			return response.Data;
		}

		/// <summary>
		/// Writes blob data to a compute channel
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="request">The request to read blob data</param>
		/// <param name="storage">Storage client to retrieve the blob from</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public static async Task WriteBlobDataAsync(this IComputeChannel channel, BlobReadMessage request, IStorageClient storage, CancellationToken cancellationToken)
		{
			await WriteBlobDataAsync(channel, request.Locator, request.Offset, request.Length, storage, cancellationToken);
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
		public static async Task WriteBlobDataAsync(this IComputeChannel channel, BlobLocator locator, int offset, int length, IStorageClient storage, CancellationToken cancellationToken)
		{
			BlobDataMessage response = new BlobDataMessage();
			if (offset == 0 && length == 0)
			{
				using (Stream stream = await storage.ReadBlobAsync(locator, cancellationToken))
				{
					using MemoryStream target = new MemoryStream();
					await stream.CopyToAsync(target, cancellationToken);
					response.Data = target.ToArray();
				}
			}
			else
			{
				using (Stream stream = await storage.ReadBlobRangeAsync(locator, offset, length, cancellationToken))
				{
					using MemoryStream target = new MemoryStream();
					await stream.CopyToAsync(target, cancellationToken);
					response.Data = target.ToArray();
				}
			}
			await channel.WriteCbMessageAsync(response, cancellationToken);
		}
	}
}

