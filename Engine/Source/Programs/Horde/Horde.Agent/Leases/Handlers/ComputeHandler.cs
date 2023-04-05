// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Sockets;
using EpicGames.Horde.Compute.Transports;
using Horde.Agent.Services;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Leases.Handlers
{
	/// <summary>
	/// Handler for compute tasks
	/// </summary>
	class ComputeHandler : LeaseHandler<ComputeTask>
	{
		readonly ComputeListenerService _listenerService;
		readonly IMemoryCache _memoryCache;
		readonly DirectoryReference _sandboxDir;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeHandler(ComputeListenerService listenerService, IMemoryCache memoryCache, ILogger<ComputeHandler> logger)
		{
			_listenerService = listenerService;
			_memoryCache = memoryCache;
			_sandboxDir = DirectoryReference.Combine(Program.DataDir, "Sandbox");
			_logger = logger;
		}

		/// <inheritdoc/>
		public override async Task<LeaseResult> ExecuteAsync(ISession session, string leaseId, ComputeTask computeTask, CancellationToken cancellationToken)
		{
			_logger.LogInformation("Starting compute task (lease {LeaseId}). Waiting for connection with nonce {Nonce}...", leaseId, StringUtils.FormatHexString(computeTask.Nonce.Span));

			TcpClient? tcpClient = null;
			try
			{
				const int TimeoutSeconds = 30;

				tcpClient = await _listenerService.WaitForClientAsync(new ByteString(computeTask.Nonce.Memory), TimeSpan.FromSeconds(TimeoutSeconds), cancellationToken);
				if (tcpClient == null)
				{
					_logger.LogInformation("Timed out waiting for connection after {Time}s.", TimeoutSeconds); 
					return LeaseResult.Success;
				}

				_logger.LogInformation("Matched connection for {Nonce}", StringUtils.FormatHexString(computeTask.Nonce.Span));

				await using (ClientComputeSocket socket = new ClientComputeSocket(new TcpTransport(tcpClient.Client), _logger))
				{
					ComputeWorker worker = new ComputeWorker(_sandboxDir, _memoryCache, _logger);
					await worker.RunAsync(socket, cancellationToken);
					await socket.CloseAsync(cancellationToken);
					return LeaseResult.Success;
				}
			}
			catch (Exception ex)
			{
				_logger.LogError(ex, "Exception while executing compute task: {Message}", ex.Message);
				return LeaseResult.Failed;
			}
			finally
			{
				tcpClient?.Dispose();
			}
		}
	}
}

