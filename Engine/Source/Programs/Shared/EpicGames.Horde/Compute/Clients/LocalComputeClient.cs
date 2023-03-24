// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
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
	/// Implementation of <see cref="IComputeClient"/> which marshals data over a loopback connection to a method running on a background task in the same process.
	/// </summary>
	public sealed class LocalComputeClient : IComputeClient
	{
		class LeaseImpl : IComputeLease
		{
			public IReadOnlyList<string> Properties { get; } = new List<string>();
			public IReadOnlyDictionary<string, int> AssignedResources => new Dictionary<string, int>();
			public IComputeSocket Socket => _socket;

			readonly ClientComputeSocket _socket;

			public LeaseImpl(ClientComputeSocket socket) => _socket = socket;
			public ValueTask DisposeAsync() => _socket.DisposeAsync();
		}

		readonly BackgroundTask _listenerTask;
		readonly Socket _listener;
		readonly Socket _socket;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="serverFunc">Callback to process the server side of the connection</param>
		/// <param name="port">Port to connect on</param>
		/// <param name="logger">Logger for diagnostic output</param>
		public LocalComputeClient(Func<IComputeSocket, CancellationToken, Task> serverFunc, int port, ILogger logger)
		{
			_logger = logger;

			_listener = new Socket(SocketType.Stream, ProtocolType.IP);
			_listener.Bind(new IPEndPoint(IPAddress.Loopback, port));
			_listener.Listen();

			_listenerTask = BackgroundTask.StartNew(ctx => RunListenerAsync(_listener, serverFunc, logger, ctx));

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
		static async Task RunListenerAsync(Socket listener, Func<IComputeSocket, CancellationToken, Task> serverFunc, ILogger logger, CancellationToken cancellationToken)
		{
			using Socket tcpSocket = await listener.AcceptAsync(cancellationToken);

			await using (ClientComputeSocket socket = new ClientComputeSocket(new TcpTransport(tcpSocket), logger))
			{
				await serverFunc(socket, cancellationToken);
				await socket.CloseAsync(cancellationToken);
			}
		}

		/// <inheritdoc/>
		public Task<IComputeLease?> TryAssignWorkerAsync(ClusterId clusterId, Requirements? requirements, CancellationToken cancellationToken)
		{
#pragma warning disable CA2000 // Dispose objects before losing scope
			ClientComputeSocket socket = new ClientComputeSocket(new TcpTransport(_socket), _logger);
			return Task.FromResult<IComputeLease?>(new LeaseImpl(socket));
#pragma warning restore CA2000 // Dispose objects before losing scope
		}
	}
}
