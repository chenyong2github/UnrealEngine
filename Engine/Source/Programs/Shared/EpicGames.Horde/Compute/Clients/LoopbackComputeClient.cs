// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute.Transports;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute.Clients
{
	/// <summary>
	/// Implementation of <see cref="IComputeChannel"/> which marshals data over a loopback connection to a method running on a background task.
	/// </summary>
	public sealed class LoopbackComputeClient : IComputeClient
	{
		readonly BackgroundTask _listenerTask;
		readonly Socket _listener;
		readonly Socket _socket;
		readonly ILoggerFactory _loggerFactory;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="serverFunc">Callback to process the server side of the connection</param>
		/// <param name="port">Port to connect on</param>
		/// <param name="loggerFactory">Logger for diagnostic output</param>
		public LoopbackComputeClient(Func<IComputeSocket, CancellationToken, Task> serverFunc, int port, ILoggerFactory loggerFactory)
		{
			_loggerFactory = loggerFactory;

			_listener = new Socket(SocketType.Stream, ProtocolType.IP);
			_listener.Bind(new IPEndPoint(IPAddress.Loopback, port));
			_listener.Listen();

			_listenerTask = BackgroundTask.StartNew(ctx => RunListenerAsync(_listener, serverFunc, loggerFactory, ctx));

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
		static async Task RunListenerAsync(Socket listener, Func<IComputeSocket, CancellationToken, Task> serverFunc, ILoggerFactory loggerFactory, CancellationToken cancellationToken)
		{
			using Socket tcpSocket = await listener.AcceptAsync(cancellationToken);

			await using (ComputeSocket socket = new ComputeSocket(new TcpTransport(tcpSocket), loggerFactory))
			{
				await serverFunc(socket, cancellationToken);
				await socket.CloseAsync(cancellationToken);
			}
		}

		/// <inheritdoc/>
		public async Task<TResult> ExecuteAsync<TResult>(ClusterId clusterId, Requirements? requirements, Func<IComputeLease, CancellationToken, Task<TResult>> handler, CancellationToken cancellationToken)
		{
			await using ComputeSocket socket = new ComputeSocket(new TcpTransport(_socket), _loggerFactory);
			await using ComputeLease lease = new ComputeLease(new List<string>(), new Dictionary<string, int>(), socket);
			TResult result = await handler(lease, cancellationToken);
			await socket.CloseAsync(cancellationToken);
			return result;
		}
	}
}
