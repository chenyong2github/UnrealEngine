// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Storage;
using Google.Protobuf;
using Horde.Agent.Execution;
using Horde.Agent.Services;
using Horde.Agent.Utility;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Leases.Handlers
{
	class ComputeHandler : LeaseHandler<ComputeTaskMessage>
	{
		readonly ILegacyStorageClient _legacyStorageClient;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeHandler(ILegacyStorageClient legacyStorageClient, ILogger<ComputeHandler> logger)
		{
			_legacyStorageClient = legacyStorageClient;
			_logger = logger;
		}

		/// <inheritdoc/>
		public override async Task<LeaseResult> ExecuteAsync(ISession session, string leaseId, ComputeTaskMessage computeTask, CancellationToken cancellationToken)
		{
			_logger.LogInformation("Starting compute task (lease {LeaseId})", leaseId);

			ComputeTaskResultMessage result;
			try
			{
				DateTimeOffset actionTaskStartTime = DateTimeOffset.UtcNow;
				using (IRpcClientRef client = await session.RpcConnection.GetClientRef(new RpcContext(), cancellationToken))
				{
					DirectoryReference leaseDir = DirectoryReference.Combine(session.WorkingDir, "Compute", leaseId);
					DirectoryReference.CreateDirectory(leaseDir);

					ComputeTaskExecutor executor = new ComputeTaskExecutor(_legacyStorageClient, _logger);
					try
					{
						result = await executor.ExecuteAsync(leaseId, computeTask, leaseDir, cancellationToken);
					}
					finally
					{
						try
						{
							DirectoryReference.Delete(leaseDir, true);
						}
						catch
						{
						}
					}
				}
			}
			catch (LegacyBlobNotFoundException ex)
			{
				_logger.LogError(ex, "Blob not found: {Hash}", ex.Hash);
				result = new ComputeTaskResultMessage(ComputeTaskOutcome.BlobNotFound, ex.Hash.ToString());
			}
			catch (Exception ex)
			{
				_logger.LogError(ex, "Exception while executing compute task");
				result = new ComputeTaskResultMessage(ComputeTaskOutcome.Exception, ex.ToString());
			}

			return new LeaseResult(result.ToByteArray());
		}
	}
}

