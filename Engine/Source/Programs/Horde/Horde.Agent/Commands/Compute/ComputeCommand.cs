// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Common;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Clients;
using Horde.Agent.Leases.Handlers;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Agent.Commands.Compute
{
	abstract class ComputeCommand : Command
	{
		[CommandLine("-Cluster")]
		public string ClusterId { get; set; } = "default";

		[CommandLine("-Requirements=", Description = "Match the agent to run on")]
		public string? Requirements { get; set; }

		[CommandLine("-Loopback")]
		public bool Loopback { get; set; }

		readonly IServiceProvider _serviceProvider;
		readonly IHttpClientFactory _httpClientFactory;
		readonly IOptions<AgentSettings> _settings;

		public ComputeCommand(IServiceProvider serviceProvider)
		{
			_serviceProvider = serviceProvider;
			_httpClientFactory = serviceProvider.GetRequiredService<IHttpClientFactory>();
			_settings = serviceProvider.GetRequiredService<IOptions<AgentSettings>>();
		}

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			await using IComputeClient client = CreateClient();

			Requirements? requirements = null;
			if (Requirements != null)
			{
				requirements = new Requirements(Condition.Parse(Requirements));
			}

			bool result = await client.ExecuteAsync(new ClusterId(ClusterId), requirements, HandleRequestAsync, CancellationToken.None);

			return result ? 0 : 1;
		}

		/// <summary>
		/// Callback for executing a request
		/// </summary>
		/// <param name="lease"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		protected abstract Task<bool> HandleRequestAsync(IComputeLease lease, CancellationToken cancellationToken);

		IComputeClient CreateClient()
		{
			ILoggerFactory loggerFactory = _serviceProvider.GetRequiredService<ILoggerFactory>();
			if (Loopback)
			{
				return new LoopbackComputeClient(RunListenerAsync, 2000, loggerFactory);
			}
			else
			{
				return new ServerComputeClient(CreateHttpClient, loggerFactory);
			}
		}

		HttpClient CreateHttpClient()
		{
			ServerProfile profile = _settings.Value.GetCurrentServerProfile();

			HttpClient client = _httpClientFactory.CreateClient();
			client.BaseAddress = profile.Url;
			client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", profile.Token);
			return client;
		}

		async Task RunListenerAsync(IComputeSocket socket, CancellationToken cancellationToken)
		{
			ComputeHandler handler = ActivatorUtilities.CreateInstance<ComputeHandler>(_serviceProvider);
			await handler.RunAsync(socket, cancellationToken);
		}
	}
}
