// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Implementation of <see cref="IComputeChannel"/> which marshals data over a loopback connection to a method running on a background task.
	/// </summary>
	public sealed class LoopbackComputeClient : IComputeClient
	{
		readonly byte[] _key;
		readonly byte[] _nonce;
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
			_key = ComputeLease.CreateKey();
			_nonce = RandomNumberGenerator.GetBytes(ComputeLease.NonceLength);

			_listener = new Socket(SocketType.Stream, ProtocolType.IP);
			_listener.Bind(new IPEndPoint(IPAddress.Loopback, port));
			_listener.Listen();

			_listenerTask = BackgroundTask.StartNew(ctx => RunListenerAsync(_listener, _key, _nonce, serverFunc, ctx));

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
		static async Task RunListenerAsync(Socket listener, ReadOnlyMemory<byte> key, ReadOnlyMemory<byte> nonce, Func<IComputeLease, CancellationToken, Task> serverFunc, CancellationToken cancellationToken)
		{
			using Socket socket = await listener.AcceptAsync(cancellationToken);

			await using ComputeLease lease = new ComputeLease(socket, key.Span, nonce.Span, new Dictionary<string, int>());
			await serverFunc(lease, cancellationToken);
		}

		/// <inheritdoc/>
		public async Task<TResult> ExecuteAsync<TResult>(ClusterId clusterId, Requirements? requirements, Func<IComputeLease, CancellationToken, Task<TResult>> handler, CancellationToken cancellationToken)
		{
			await using ComputeLease lease = new ComputeLease(_socket, _key, _nonce, new Dictionary<string, int>());
			return await handler(lease, cancellationToken);
		}
	}
}
