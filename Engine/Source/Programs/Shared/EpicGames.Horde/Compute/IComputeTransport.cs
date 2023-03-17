// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Interface for transferring data on a compute channel
	/// </summary>
	public interface IComputeTransport
	{
		/// <summary>
		/// Reads data from the underlying transport into an output buffer
		/// </summary>
		/// <param name="buffer">Buffer to read into</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken);

		/// <summary>
		/// Writes data to the underlying transport
		/// </summary>
		/// <param name="buffer">Buffer to be written</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public ValueTask WriteAsync(ReadOnlySequence<byte> buffer, CancellationToken cancellationToken);
	}

	/// <summary>
	/// Extension methods for <see cref="IComputeTransport"/>
	/// </summary>
	public static class ComputeTransportExtensions
	{
		/// <summary>
		/// Writes data to the underlying transport
		/// </summary>
		/// <param name="transport">Transport instance</param>
		/// <param name="buffer">Buffer to be written</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static ValueTask WriteAsync(this IComputeTransport transport, ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken)
			=> transport.WriteAsync(new ReadOnlySequence<byte>(buffer), cancellationToken);
	}
}

