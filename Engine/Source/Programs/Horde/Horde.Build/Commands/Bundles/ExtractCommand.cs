// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Horde.Build.Perforce;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Commands.Bundles
{
	[Command("bundle", "extract", "Extracts data from a bundle to the local hard drive")]
	internal class ExtractCommand : Command
	{
		[CommandLine("-Ref=")]
		public RefName RefName { get; set; } = new RefName("default-ref");

		[CommandLine("-OutputDir=", Required = true)]
		public DirectoryReference OutputDir { get; set; } = null!;

		readonly IConfiguration _configuration;
		readonly ILoggerProvider _loggerProvider;

		public ExtractCommand(IConfiguration configuration, ILoggerProvider loggerProvider)
		{
			_configuration = configuration;
			_loggerProvider = loggerProvider;
		}

		public override Task<int> ExecuteAsync(ILogger logger)
		{
			using ServiceProvider serviceProvider = Startup.CreateServiceProvider(_configuration, _loggerProvider);

			IStorageClient store = serviceProvider.GetRequiredService<IStorageClient<ReplicationService>>();
			TreeReader reader = new TreeReader(store, serviceProvider.GetRequiredService<IMemoryCache>(), serviceProvider.GetRequiredService<ILogger<ExtractCommand>>());

			throw new NotImplementedException();
		}
	}
}
