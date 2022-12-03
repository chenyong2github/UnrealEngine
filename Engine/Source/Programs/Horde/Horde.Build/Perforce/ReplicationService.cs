// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Horde.Build.Storage;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.AspNetCore.SignalR.Protocol;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Perforce
{
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Exception triggered during content replication
	/// </summary>
	public sealed class ReplicationException : Exception
	{
		internal ReplicationException(string message) : base(message)
		{
		}
	}

	/// <summary>
	/// Service which replicates content from Perforce
	/// </summary>
	sealed class ReplicationService : IHostedService
	{
		readonly IStreamCollection _streamCollection;
		readonly IPerforceService _perforceService;
		readonly PerforceReplicator _replicator;
		readonly StorageService _storageService;
		readonly IMemoryCache _memoryCache;
		readonly ITicker _ticker;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ReplicationService(IStreamCollection streamCollection, IPerforceService perforceService, PerforceReplicator replicator, StorageService storageService, IMemoryCache memoryCache, IClock clock, ILogger<ReplicationService> logger)
		{
			_streamCollection = streamCollection;
			_perforceService = perforceService;
			_replicator = replicator;
			_storageService = storageService;
			_memoryCache = memoryCache;
			_ticker = clock.AddSharedTicker<ReplicationService>(TimeSpan.FromSeconds(20.0), TickSharedAsync, logger);
			_logger = logger;
		}

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken cancellationToken)
		{
			await _ticker.StartAsync();
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken cancellationToken)
		{
			await _ticker.StopAsync();
		}

		async ValueTask TickSharedAsync(CancellationToken cancellationToken)
		{
			Dictionary<StreamId, BackgroundTask> streamIdToTask = new Dictionary<StreamId, BackgroundTask>();
			try
			{
				for (; ; )
				{
					List<IStream> streams = await _streamCollection.FindAllAsync();

					HashSet<StreamId> removeStreams = new HashSet<StreamId>(streamIdToTask.Keys);
					foreach (IStream stream in streams)
					{
						if (stream.Config.ReplicationMode == ContentReplicationMode.Full)
						{
							removeStreams.Remove(stream.Id);
							if (!streamIdToTask.ContainsKey(stream.Id))
							{
								streamIdToTask.Add(stream.Id, BackgroundTask.StartNew(ctx => RunReplicationGuardedAsync(stream, ctx)));
								_logger.LogInformation("Started replication of {StreamId}", stream.Id);
							}
						}
					}

					foreach (StreamId removeStreamId in removeStreams)
					{
						if (streamIdToTask.Remove(removeStreamId, out BackgroundTask? task))
						{
							await task.DisposeAsync();
							_logger.LogInformation("Stopped replication of {StreamId}", removeStreamId);
						}
					}

					_logger.LogInformation("Replicating {NumStreams} streams", streamIdToTask.Count);
					await Task.Delay(TimeSpan.FromMinutes(1.0), cancellationToken);
				}
			}
			finally
			{
				await Parallel.ForEachAsync(streamIdToTask.Values, (x, ctx) => x.DisposeAsync());
			}
		}

		async Task RunReplicationGuardedAsync(IStream stream, CancellationToken cancellationToken)
		{
			try
			{
				await RunReplicationAsync(stream, cancellationToken);
			}
			catch (Exception ex)
			{
				_logger.LogError(ex, "Exception running replication for {StreamId}: {Message}", stream.Id, ex.Message);
			}
		}

		async Task RunReplicationAsync(IStream stream, CancellationToken cancellationToken)
		{
			RefName refName = new RefName(stream.Id.ToString());

			IStorageClientImpl store = await _storageService.GetClientAsync(Namespace.Perforce, cancellationToken);
			TreeReader reader = new TreeReader(store, _memoryCache, _logger);

			CommitNode? lastCommitNode = await reader.TryReadNodeAsync<CommitNode>(refName, cancellationToken: cancellationToken);
			ICommitCollection commits = _perforceService.GetCommits(stream);

			PerforceReplicationOptions options = new PerforceReplicationOptions();

			ICommit commit;
			if (lastCommitNode == null)
			{
				commit = await commits.GetLatestAsync(cancellationToken);
			}
			else
			{
				commit = await commits.SubscribeAsync(lastCommitNode.Number, cancellationToken: cancellationToken).FirstAsync(cancellationToken);
			}

			for (; ; )
			{
				_logger.LogInformation("Replicating {StreamId} change {Change}", stream.Id, commit.Number);
				await _replicator.WriteAsync(stream, commit.Number, options, cancellationToken);
				commit = await commits.SubscribeAsync(commit.Number, cancellationToken: cancellationToken).FirstAsync(cancellationToken);
			}
		}
	}
}
