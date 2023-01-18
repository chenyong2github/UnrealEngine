// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
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
	[Command("ComputeV2", "Executes a command through the Horde Compute API")]
	class ComputeCommandV2 : Command
	{
		public class AddComputeTaskRequest
		{
			public Requirements? Requirements { get; set; }
			public string RemoteIp { get; set; } = String.Empty;
			public int RemotePort { get; set; } = 4000;
		}

		/// <summary>
		/// Condition for 
		/// </summary>
		[CommandLine("-Condition")]
		public string? Condition { get; set; }

		readonly AgentSettings _settings;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeCommandV2(IOptions<AgentSettings> settings)
		{
			_settings = settings.Value;
		}

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			ServerProfile profile = _settings.GetCurrentServerProfile();
			logger.LogInformation("Server: {Server}", profile.Name);

			int port = 9001;

			using CancellationTokenSource source = new CancellationTokenSource();
			Task task = Task.Run(() => ListenerProcess(port, logger, source.Token));

			using (HttpClient client = new HttpClient())
			{
				AddComputeTaskRequest request = new AddComputeTaskRequest();
				request.RemoteIp = "127.0.0.1";
				request.RemotePort = port;

				Uri uri = new Uri(profile.Url, "api/v2/compute");
				using (HttpResponseMessage response = await client.PostAsync(uri.AbsoluteUri, request, source.Token))
				{
					response.EnsureSuccessStatusCode();
				}
			}

			await task;
			return 0;
		}

		static async Task ListenerProcess(int port, ILogger logger, CancellationToken cancellationToken)
		{
			TcpListener listener = new TcpListener(IPAddress.Loopback, port);
			listener.Start();

			List<Task> tasks = new List<Task>();
			try
			{
				for (; ; )
				{
					TcpClient client = await listener.AcceptTcpClientAsync(cancellationToken);
					logger.LogInformation("Received connection");

					Task task = ServerProcess(client, logger, cancellationToken);
					tasks.Add(task);
				}
			}
			finally
			{
				await Task.WhenAll(tasks);
				listener.Stop();
			}
		}

		static async Task ServerProcess(TcpClient client, ILogger logger, CancellationToken cancellationToken)
		{
			MessageChannel channel = new MessageChannel(client.Client);
			try
			{
				await channel.WriteAsync(new XorRequestMessage { Value = 123, Payload = new byte[] { 1, 2, 3, 4, 5 } }, cancellationToken);

				XorResponseMessage response = await channel.ReadAsync<XorResponseMessage>(cancellationToken);
				logger.LogInformation("Received response: {Message}", String.Join(", ", response.Payload.Select(x => x.ToString())));

				await channel.WriteAsync(new CloseMessage(), cancellationToken);
			}
			finally
			{
				client.Dispose();
			}
		}
	}
}
