// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Binary;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

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
		/// Fork a new request channel
		/// </summary>
		Fork = 0x01,

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
	/// Message for forking the message loop
	/// </summary>
	/// <param name="Id">New channel id</param>
	public record struct ForkMessage(int Id);

	/// <summary>
	/// Message for running an XOR command
	/// </summary>
	/// <param name="Data">Data to xor</param>
	/// <param name="Value">Value to XOR with</param>
	public record struct XorRequestMessage(ReadOnlyMemory<byte> Data, byte Value);

	/// <summary>
	/// Wraps various requests across compute channels
	/// </summary>
	public static class ComputeMessageExtensions
	{
		/// <summary>
		/// Creates a new remote message loop using the given channel id
		/// </summary>
		/// <param name="channel">Current channel</param>
		/// <param name="id">New channel id</param>
		public static void Fork(this IComputeChannel channel, int id)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(ComputeMessageType.Fork))
			{
				builder.WriteInt32(id);
				builder.Send();
			}
		}

		/// <summary>
		/// Parses a fork message from the given compute message
		/// </summary>
		public static ForkMessage AsForkMessage(this IComputeMessage message)
		{
			ReadOnlyMemory<byte> data = message.Data;
			return new ForkMessage(BinaryPrimitives.ReadInt32LittleEndian(data.Span));
		}

		/// <summary>
		/// Send a message to request that a byte string be xor'ed with a particular value
		/// </summary>
		public static void XorRequest(this IComputeChannel channel, ReadOnlyMemory<byte> data, byte value)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(ComputeMessageType.XorRequest))
			{
				builder.WriteFixedLengthBytes(data.Span);
				builder.WriteUInt8(value);
				builder.Send();
			}
		}

		/// <summary>
		/// Parse a message as an XOR request
		/// </summary>
		public static XorRequestMessage AsXorRequest(this IComputeMessage message)
		{
			ReadOnlyMemory<byte> data = message.Data;
			return new XorRequestMessage(data[0..^1], data.Span[^1]);
		}
	}
}
