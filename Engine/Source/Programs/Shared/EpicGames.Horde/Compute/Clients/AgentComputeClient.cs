// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute.Transports;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute.Clients
{
	/// <summary>
	/// Runs a local Horde Agent process to process compute requests without communicating with a server
	/// </summary>
	public sealed class AgentComputeClient : IComputeClient
	{
		readonly string _hordeAgentAssembly;
		readonly int _port;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="hordeAgentFile">Path to the Horde Agent assembly</param>
		/// <param name="port">Loopback port to connect on</param>
		/// <param name="logger">Factory for logger instances</param>
		public AgentComputeClient(string hordeAgentFile, int port, ILogger logger)
		{
			_hordeAgentAssembly = hordeAgentFile;
			_port = port;
			_logger = logger;
		}

		/// <inheritdoc/>
		public ValueTask DisposeAsync() => new ValueTask();

		/// <inheritdoc/>
		public async Task<TResult> ExecuteAsync<TResult>(ClusterId clusterId, Requirements? requirements, Func<IComputeLease, CancellationToken, Task<TResult>> handler, CancellationToken cancellationToken)
		{
			_logger.LogInformation("** CLIENT **");
			_logger.LogInformation("Launching {Path} to handle remote", _hordeAgentAssembly);

			TResult result;
			using (Socket listener = new Socket(SocketType.Stream, ProtocolType.IP))
			{
				listener.Bind(new IPEndPoint(IPAddress.Loopback, _port));
				listener.Listen();

				using CancellationTokenSource cancellationSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);

				Task<TResult> listenTask = ListenAsync<TResult>(listener, handler, cancellationSource.Token);
				using (ManagedProcessGroup group = new ManagedProcessGroup())
				{
					if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
					{
						List<string> arguments = new List<string>();
						arguments.Add(_hordeAgentAssembly);
						arguments.Add("computeworker");
						arguments.Add($"-port={_port}");

						using ManagedProcess process = new ManagedProcess(group, "dotnet", CommandLineArguments.Join(arguments), null, null, ProcessPriorityClass.Normal);

						string? line;
						while ((line = await process.ReadLineAsync(cancellationToken)) != null)
						{
							_logger.LogInformation("Output: {Line}", line);
						}

						await process.WaitForExitAsync(cancellationToken);
					}
					else
					{
						using Process process = new Process();
						process.StartInfo.FileName = "dotnet";
						process.StartInfo.ArgumentList.Add(_hordeAgentAssembly);
						process.StartInfo.ArgumentList.Add("computeworker");
						process.StartInfo.ArgumentList.Add($"-port={_port}");
						process.StartInfo.UseShellExecute = true;
						process.Start();

						await process.WaitForExitAsync(cancellationToken);
					}
				}

				cancellationSource.Cancel();
				result = await listenTask;
			}

			_logger.LogInformation("Client terminated.");
			return result;
		}

		async Task<TResult> ListenAsync<TResult>(Socket listener, Func<IComputeLease, CancellationToken, Task<TResult>> handler, CancellationToken cancellationToken)
		{
			TResult result;
			using (Socket tcpSocket = await listener.AcceptAsync(cancellationToken))
			{
				await using (ClientComputeSocket socket = new ClientComputeSocket(new TcpTransport(tcpSocket), _logger))
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
