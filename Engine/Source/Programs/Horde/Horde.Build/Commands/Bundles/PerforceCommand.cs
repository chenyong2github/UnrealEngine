// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Configuration;
using Horde.Build.Perforce;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Commands.Bundles
{
	using StreamId = StringId<IStream>;

	[Command("bundle", "perforce", "Replicates commits for a particular change of changes from Perforce")]
	class PerforceCommand : Command
	{
		[CommandLine("-Stream=", Required = true)]
		public string StreamId { get; set; } = String.Empty;

		[CommandLine(Required = true)]
		public int Change { get; set; }

		readonly IConfiguration _configuration;
		readonly ILoggerProvider _loggerProvider;

		public PerforceCommand(IConfiguration configuration, ILoggerProvider loggerProvider)
		{
			_configuration = configuration;
			_loggerProvider = loggerProvider;
		}

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			using ServiceProvider serviceProvider = Startup.CreateServiceProvider(_configuration, _loggerProvider);

			ConfigUpdateService configUpdateService = serviceProvider.GetRequiredService<ConfigUpdateService>();
			await configUpdateService.ForceUpdateAsync(CancellationToken.None);

			PerforceReplicator replicator = serviceProvider.GetRequiredService<PerforceReplicator>();
			IStreamCollection streamCollection = serviceProvider.GetRequiredService<IStreamCollection>();

			IStream? stream = await streamCollection.GetAsync(new StreamId(StreamId));
			if (stream == null)
			{
				throw new FatalErrorException($"Stream '{stream}' not found");
			}

			PerforceReplicationOptions options = new PerforceReplicationOptions();
			await replicator.WriteAsync(stream, Change, options, default);

			return 0;
		}
	}
}
