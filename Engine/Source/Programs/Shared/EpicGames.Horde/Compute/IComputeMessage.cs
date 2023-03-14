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
		CppSuccess = 0x21,

		/// <summary>
		/// Information about a failed C++ task
		/// </summary>
		CppFailure = 0x22,

		/// <summary>
		/// Finish a C++ build and close the current channel
		/// </summary>
		CppEnd = 0x23,

		/// <summary>
		/// Reads a blob pertaining to a C++ build
		/// </summary>
		CppBlobRead = 0x24,

		/// <summary>
		/// Response to a <see cref="CppBlobRead"/> request.
		/// </summary>
		CppBlobData = 0x25,

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
	}
}
