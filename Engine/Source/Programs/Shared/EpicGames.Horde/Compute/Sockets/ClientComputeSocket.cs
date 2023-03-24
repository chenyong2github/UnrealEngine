// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Horde.Compute.Buffers;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Manages a set of readers and writers to buffers across a transport layer
	/// </summary>
	public sealed class ClientComputeSocket : ComputeSocketBase, IAsyncDisposable
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="transport">Transport to communicate with the remote</param>
		/// <param name="logger">Logger for trace output</param>
		public ClientComputeSocket(IComputeTransport transport, ILogger logger)
			: base(transport, logger)
		{
		}

		/// <inheritdoc/>
		public override IComputeBuffer CreateBuffer(long capacity)
		{
			if (capacity > Int32.MaxValue)
			{
				return new SharedMemoryBuffer(capacity);
			}
			else
			{
				return new PooledBuffer((int)capacity);
			}
		}
	}
}
