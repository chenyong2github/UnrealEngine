// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Net;
using System.Net.Http;
using System.Net.Http.Json;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Helper class to enlist remote resources to perform compute-intensive tasks.
	/// </summary>
	public sealed class ServerComputeClient : IComputeClient
	{
		/// <summary>
		/// Length of the nonce sent as part of handshaking between initiator and remote
		/// </summary>
		public const int NonceLength = 64;

		class AssignComputeRequest
		{
			public Requirements? Requirements { get; set; }
		}

		class AssignComputeResponse
		{
			public string Ip { get; set; } = String.Empty;
			public int Port { get; set; }
			public string Nonce { get; set; } = String.Empty;
			public string AesKey { get; set; } = String.Empty;
			public string AesIv { get; set; } = String.Empty;
		}

		readonly HttpClient? _defaultHttpClient;
		readonly Func<HttpClient> _createHttpClient;
		readonly CancellationTokenSource _cancellationSource = new CancellationTokenSource();
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="serverUri">Uri of the server to connect to</param>
		/// <param name="logger">Logger for diagnostic messages</param>
		public ServerComputeClient(Uri serverUri, ILogger logger)
		{
#pragma warning disable CA2000 // Dispose objects before losing scope
			// This is disposed via HttpClient
			SocketsHttpHandler handler = new SocketsHttpHandler();
			handler.PooledConnectionLifetime = TimeSpan.FromMinutes(2.0);

			_defaultHttpClient = new HttpClient(handler, true);
			_defaultHttpClient.BaseAddress = serverUri;
#pragma warning restore CA2000 // Dispose objects before losing scope

			_createHttpClient = GetDefaultHttpClient;
			_logger = logger;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="createHttpClient">Creates an HTTP client with the correct base address for the server</param>
		/// <param name="logger">Logger for diagnostic messages</param>
		public ServerComputeClient(Func<HttpClient> createHttpClient, ILogger logger)
		{
			_createHttpClient = createHttpClient;
			_logger = logger;
		}

		/// <summary>
		/// Gets the default http client
		/// </summary>
		/// <returns></returns>
		HttpClient GetDefaultHttpClient() => _defaultHttpClient!;

		/// <inheritdoc/>
		public ValueTask DisposeAsync()
		{
			Dispose();
			return new ValueTask();
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_cancellationSource.Dispose();
			_defaultHttpClient?.Dispose();
		}

		/// <inheritdoc/>
		public async Task<TResult> ExecuteAsync<TResult>(ClusterId clusterId, Requirements? requirements, Func<IComputeChannel, CancellationToken, Task<TResult>> handler, CancellationToken cancellationToken)
		{
			// Assign a compute worker
			HttpClient client = _createHttpClient();

			AssignComputeRequest request = new AssignComputeRequest();
			request.Requirements = requirements;

			AssignComputeResponse? responseMessage;
			using (HttpResponseMessage response = await client.PostAsync($"api/v2/compute/{clusterId}", request, _cancellationSource.Token))
			{
				response.EnsureSuccessStatusCode();
				responseMessage = await response.Content.ReadFromJsonAsync<AssignComputeResponse>(cancellationToken: cancellationToken);
				if (responseMessage == null)
				{
					throw new InvalidOperationException();
				}
			}

			// Connect to the remote machine
			using Socket socket = new Socket(SocketType.Stream, ProtocolType.Tcp);
			await socket.ConnectAsync(IPAddress.Parse(responseMessage.Ip), responseMessage.Port, cancellationToken);

			// Send the nonce
			byte[] nonce = StringUtils.ParseHexString(responseMessage.Nonce);
			await socket.SendFullAsync(nonce, SocketFlags.None, cancellationToken);
			_logger.LogInformation("Connected to {Ip} with nonce {Nonce}", responseMessage.Ip, responseMessage.Nonce);

			// Pass the rest of the call over to the handler
			byte[] aesKey = StringUtils.ParseHexString(responseMessage.AesKey);
			byte[] aesIv = StringUtils.ParseHexString(responseMessage.AesIv);

			using SocketComputeChannel channel = new SocketComputeChannel(socket, aesKey, aesIv);
			return await handler(channel, cancellationToken);
		}
	}
}
