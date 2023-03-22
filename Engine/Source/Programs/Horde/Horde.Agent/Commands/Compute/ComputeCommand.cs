// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Amazon.EC2.Model;
using EpicGames.Core;
using EpicGames.Horde.Common;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Clients;
using EpicGames.Horde.Compute.Transports;
using Horde.Agent.Leases.Handlers;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Polly;

namespace Horde.Agent.Commands.Compute
{
	abstract class ComputeCommand : Command
	{
		[CommandLine("-Cluster")]
		public string ClusterId { get; set; } = "default";

		[CommandLine("-Requirements=", Description = "Match the agent to run on")]
		public string? Requirements { get; set; }

		[CommandLine("-Local")]
		public bool Local { get; set; }

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
			if (Local)
			{
				return new LocalComputeClient((socket, ctx) => ComputeWorkerCommand.RunWorkerAsync(socket, _serviceProvider, ctx), 2000, loggerFactory);
			}
			else if (Loopback)
			{
				return new LoopbackComputeClient(2000, loggerFactory);
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

		class LoopbackComputeClient : IComputeClient
		{
			readonly int _port;
			readonly ILoggerFactory _loggerFactory;

			public LoopbackComputeClient(int port, ILoggerFactory loggerFactory)
			{
				_port = port;
				_loggerFactory = loggerFactory;
			}

			public ValueTask DisposeAsync() => new ValueTask();

			public async Task<TResult> ExecuteAsync<TResult>(ClusterId clusterId, Requirements? requirements, Func<IComputeLease, CancellationToken, Task<TResult>> handler, CancellationToken cancellationToken)
			{
				ILogger logger = _loggerFactory.CreateLogger<LoopbackComputeClient>();
				logger.LogInformation("** INITIATOR **");

				TResult result;
				using (Socket listener = new Socket(SocketType.Stream, ProtocolType.IP))
				{
					listener.Bind(new IPEndPoint(IPAddress.Loopback, _port));
					listener.Listen();

					using (CancellationTokenSource cancellationSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken))
					{
						Task<TResult> listenTask = ListenAsync<TResult>(listener, handler, cancellationSource.Token);
						try
						{
							using ManagedProcessGroup group = new ManagedProcessGroup();

							using Process process = new Process();
							process.StartInfo.FileName = "dotnet";
							process.StartInfo.ArgumentList.Add(Assembly.GetExecutingAssembly().Location);
							process.StartInfo.ArgumentList.Add("computeworker");
							process.StartInfo.ArgumentList.Add($"-port={_port}");
							process.StartInfo.UseShellExecute = true;
							process.Start();

							group.AddProcess(process.Handle);

							await process.WaitForExitAsync(cancellationToken);
						}
						finally
						{
							cancellationSource.Cancel();
						}
						result = await listenTask;
					}
				}
				return result;
			}

			async Task<TResult> ListenAsync<TResult>(Socket listener, Func<IComputeLease, CancellationToken, Task<TResult>> handler, CancellationToken cancellationToken)
			{
				TResult result;
				using (Socket tcpSocket = await listener.AcceptAsync(cancellationToken))
				{
					await using (ComputeSocket socket = new ComputeSocket(new TcpTransport(tcpSocket), _loggerFactory))
					{
						await using ComputeLease lease = new ComputeLease(new List<string>(), new Dictionary<string, int>(), socket);
						result = await handler(lease, cancellationToken);
						await socket.CloseAsync(cancellationToken);
					}
				}
				return result;
			}
		}
	}

	/// <summary>
	/// Helper command for hosting a local compute worker in a separate process
	/// </summary>
	[Command("computeworker", "Runs the agent as a local compute host, accepting incoming connections on the loopback adapter with a given port")]
	class ComputeWorkerCommand : Command
	{
		readonly IServiceProvider _serviceProvider;

		[CommandLine("-Port=")]
		int Port { get; set; } = 2000;

		public ComputeWorkerCommand(IServiceProvider serviceProvider)
		{
			_serviceProvider = serviceProvider;
		}

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			logger.LogInformation("** WORKER **");

			ILoggerFactory loggerFactory = _serviceProvider.GetRequiredService<ILoggerFactory>();

			using Socket tcpSocket = new Socket(SocketType.Stream, ProtocolType.IP);
			await tcpSocket.ConnectAsync(IPAddress.Loopback, Port);

			await using (ComputeSocket socket = new ComputeSocket(new TcpTransport(tcpSocket), loggerFactory))
			{
				logger.LogInformation("Running worker...");
				await RunWorkerAsync(socket, _serviceProvider, CancellationToken.None);
				logger.LogInformation("Worker complete");
				await socket.CloseAsync(CancellationToken.None);
			}

			logger.LogInformation("Stopping");
			return 0;
		}

		public static async Task RunWorkerAsync(IComputeSocket socket, IServiceProvider serviceProvider, CancellationToken cancellationToken)
		{
			ComputeHandler handler = ActivatorUtilities.CreateInstance<ComputeHandler>(serviceProvider);
			await handler.RunAsync(socket, cancellationToken);
		}
	}
}
