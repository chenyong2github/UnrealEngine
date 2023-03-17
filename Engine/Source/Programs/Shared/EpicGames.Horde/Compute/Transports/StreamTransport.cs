// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute.Transports
{
	/// <summary>
	/// Compute transport which wraps an underlying stream
	/// </summary>
	class StreamTransport : IComputeTransport
	{
		readonly Stream _stream;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="stream">Stream to use for the transferring data</param>
		public StreamTransport(Stream stream) => _stream = stream;

		/// <inheritdoc/>
		public async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken) => await _stream.ReadAsync(buffer, cancellationToken);

		/// <inheritdoc/>
		public async ValueTask WriteAsync(ReadOnlySequence<byte> buffer, CancellationToken cancellationToken)
		{
			foreach (ReadOnlyMemory<byte> memory in buffer)
			{
				await _stream.WriteAsync(memory, cancellationToken);
			}
		}
	}
}
