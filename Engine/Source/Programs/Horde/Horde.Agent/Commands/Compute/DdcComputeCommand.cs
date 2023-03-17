// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Agent.Commands
{
	/// <summary>
	/// Installs the agent as a service
	/// </summary>
	[Command("ddccompute", "Executes a command through the Horde Compute API")]
	class DdcComputeCommand : Command
	{
		[CommandLine("-Cluster=", Description = "Cluster to execute on")]
		public ClusterId ClusterId { get; set; } = new ClusterId("default");

		[CommandLine("-Condition=", Description = "Match the agent to run on")]
		public string? Condition { get; set; }

		readonly AgentSettings _settings;

		/// <summary>
		/// Constructor
		/// </summary>
		public DdcComputeCommand(IOptions<AgentSettings> settings)
		{
			_settings = settings.Value;
		}

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			ServerProfile profile = _settings.GetCurrentServerProfile();
			logger.LogInformation("Connecting to server: {Server}", profile.Name);

			await using ServerComputeClient client = new ServerComputeClient(CreateHttpClient, logger);
			await client.ExecuteAsync(ClusterId, null, TestCommandAsync, CancellationToken.None);

			return 0;
		}

		HttpClient CreateHttpClient()
		{
			HttpClient client = new HttpClient();
			client.BaseAddress = _settings.GetCurrentServerProfile().Url;
			return client;
		}

		async Task<object?> TestCommandAsync(IComputeLease lease, CancellationToken cancellationToken)
		{
			using IComputeChannel channel = lease.CreateChannel(0);

			await channel.XorRequestAsync(new byte[] { 1, 2, 3, 4, 5 }, (byte)123, cancellationToken);
			IComputeMessage response = await channel.ReadAsync(cancellationToken);

			byte[] result = response.Data.ToArray();
			Debug.Assert(result[0] == (byte)(1 ^ 123));
			Debug.Assert(result[1] == (byte)(2 ^ 123));
			Debug.Assert(result[2] == (byte)(3 ^ 123));
			Debug.Assert(result[3] == (byte)(4 ^ 123));
			Debug.Assert(result[4] == (byte)(5 ^ 123));

			await channel.CloseAsync(cancellationToken);
			return null;
		}
	}
}
