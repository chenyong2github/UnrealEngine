// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Implementation of <see cref="IComputeChannel"/> which marshals data over a loopback connection to a method running on a background task.
	/// </summary>
	public sealed class LoopbackComputeClient : IComputeClient
	{
		readonly Task _listenerTask;
		readonly SocketComputeChannel _inner;
		readonly Socket _socket;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="serverFunc">Callback to process the server side of the connection</param>
		/// <param name="port">Port to connect on</param>
		public LoopbackComputeClient(Func<IComputeChannel, CancellationToken, Task> serverFunc, int port = 2000)
		{
			using Aes aes = Aes.Create();
			_listenerTask = RunListenerAsync(aes.Key, aes.IV, serverFunc, port, CancellationToken.None);

			_socket = new Socket(SocketType.Stream, ProtocolType.IP);
			_socket.Connect(IPAddress.Loopback, port);

			_inner = new SocketComputeChannel(_socket, aes.Key, aes.IV);
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			_inner.Dispose();
			_socket.Dispose();
			await _listenerTask;
		}

		/// <summary>
		/// Sets up the loopback listener and calls the server method
		/// </summary>
		static async Task RunListenerAsync(ReadOnlyMemory<byte> aesKey, ReadOnlyMemory<byte> aesIv, Func<IComputeChannel, CancellationToken, Task> serverFunc, int port, CancellationToken cancellationToken)
		{
			using Socket listener = new Socket(SocketType.Stream, ProtocolType.IP);
			listener.Bind(new IPEndPoint(IPAddress.Loopback, port));
			listener.Listen();

			using Socket socket = await listener.AcceptAsync(cancellationToken);
			using SocketComputeChannel channel = new SocketComputeChannel(socket, aesKey, aesIv);

			await serverFunc(channel, cancellationToken);
		}

		/// <inheritdoc/>
		public async Task<TResult> ExecuteAsync<TResult>(ClusterId clusterId, Requirements? requirements, Func<IComputeChannel, CancellationToken, Task<TResult>> handler, CancellationToken cancellationToken)
		{
			return await handler(_inner, cancellationToken);
		}
	}
}
