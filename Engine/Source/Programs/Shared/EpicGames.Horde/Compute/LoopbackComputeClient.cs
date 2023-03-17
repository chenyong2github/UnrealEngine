// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute.Transports;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Implementation of <see cref="IComputeChannel"/> which marshals data over a loopback connection to a method running on a background task.
	/// </summary>
	public sealed class LoopbackComputeClient : IComputeClient
	{
		readonly BackgroundTask _listenerTask;
		readonly Socket _listener;
		readonly Socket _socket;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="serverFunc">Callback to process the server side of the connection</param>
		/// <param name="port">Port to connect on</param>
		public LoopbackComputeClient(Func<IComputeLease, CancellationToken, Task> serverFunc, int port = 2000)
		{
			_listener = new Socket(SocketType.Stream, ProtocolType.IP);
			_listener.Bind(new IPEndPoint(IPAddress.Loopback, port));
			_listener.Listen();

			_listenerTask = BackgroundTask.StartNew(ctx => RunListenerAsync(_listener, serverFunc, ctx));

			_socket = new Socket(SocketType.Stream, ProtocolType.IP);
			_socket.Connect(IPAddress.Loopback, port);
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			_socket.Dispose();
			await _listenerTask.DisposeAsync();
			_listener.Dispose();
		}

		/// <summary>
		/// Sets up the loopback listener and calls the server method
		/// </summary>
		static async Task RunListenerAsync(Socket listener, Func<IComputeLease, CancellationToken, Task> serverFunc, CancellationToken cancellationToken)
		{
			using Socket socket = await listener.AcceptAsync(cancellationToken);

			await using ComputeLease lease = new ComputeLease(new TcpTransport(socket), new Dictionary<string, int>());
			await serverFunc(lease, cancellationToken);
			await lease.CloseAsync(cancellationToken);
		}

		/// <inheritdoc/>
		public async Task<TResult> ExecuteAsync<TResult>(ClusterId clusterId, Requirements? requirements, Func<IComputeLease, CancellationToken, Task<TResult>> handler, CancellationToken cancellationToken)
		{
			await using ComputeLease lease = new ComputeLease(new TcpTransport(_socket), new Dictionary<string, int>());
			TResult result = await handler(lease, cancellationToken);
			await lease.CloseAsync(cancellationToken);
			return result;
		}
	}
}
