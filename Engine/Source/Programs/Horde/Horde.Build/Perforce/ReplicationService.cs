// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using Horde.Build.Server;
using Horde.Build.Streams;
using Horde.Build.Utilities;
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
		readonly RedisService _redisService;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ReplicationService(RedisService redisService, ILogger<ReplicationService> logger)
		{
			_redisService = redisService;
			_logger = logger;
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken cancellationToken) => Task.CompletedTask;

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;
	}
}
