// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Authorization;
using Horde.Common.Rpc;
using System.Threading.Tasks;
using Grpc.Core;
using Horde.Build.Utilities;
using EpicGames.Horde.Storage;
using Horde.Build.Storage;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Logs
{
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Implements the Horde gRPC service for bots updating their status and dequeing work
	/// </summary>
	[Authorize]
	public class LogRpcService : LogRpc.LogRpcBase
	{
		readonly ILogFileService _logFileService;
		readonly StorageService _storageService;
		readonly ILogger<LogRpcService> _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public LogRpcService(ILogFileService logFileService, StorageService storageService, ILogger<LogRpcService> logger)
		{
			_logFileService = logFileService;
			_storageService = storageService;
			_logger = logger;
		}

		/// <inheritdoc/>
		public override async Task<UpdateLogResponse> UpdateLog(UpdateLogRequest request, ServerCallContext context)
		{
			ILogFile? logFile = await _logFileService.GetCachedLogFileAsync(new LogId(request.LogId), context.CancellationToken);
			if (logFile == null)
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Resource not found");
			}
			if (!LogFileService.AuthorizeForSession(logFile, context.GetHttpContext().User))
			{
				throw new StructuredRpcException(StatusCode.PermissionDenied, "Access denied");
			}

			IStorageClient store = await _storageService.GetClientAsync(new NamespaceId("default"), context.CancellationToken);
			_logger.LogInformation("Updating {LogId} to node {Locator}", request.LogId, request.BlobLocator);
			await store.WriteRefTargetAsync(new RefName(request.LogId), NodeLocator.Parse(request.BlobLocator));
			return new UpdateLogResponse();
		}

		/// <inheritdoc/>
		public override async Task UpdateLogTail(IAsyncStreamReader<UpdateLogTailRequest> requestStream, IServerStreamWriter<UpdateLogTailResponse> responseStream, ServerCallContext context)
		{
			while (await requestStream.MoveNext())
			{
				// TODO: request tail data if log has been read recently
			}
			await responseStream.WriteAsync(new UpdateLogTailResponse { TailNext = -1 }); 
		}
	}
}
