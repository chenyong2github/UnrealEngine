// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Buffers;
using EpicGames.Horde.Compute.Transports;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Horde.Agent.Services;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Leases.Handlers
{
	/// <summary>
	/// Handler for compute tasks
	/// </summary>
	class ComputeHandler : LeaseHandler<ComputeTask>
	{
		readonly ComputeListenerService _listenerService;
		readonly IMemoryCache _memoryCache;
		readonly DirectoryReference _sandboxDir;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeHandler(ComputeListenerService listenerService, IMemoryCache memoryCache, ILogger<ComputeHandler> logger)
		{
			_listenerService = listenerService;
			_memoryCache = memoryCache;
			_sandboxDir = DirectoryReference.Combine(Program.DataDir, "Sandbox");
			_logger = logger;
		}

		/// <inheritdoc/>
		public override async Task<LeaseResult> ExecuteAsync(ISession session, string leaseId, ComputeTask computeTask, CancellationToken cancellationToken)
		{
			_logger.LogInformation("Starting compute task (lease {LeaseId}). Waiting for connection...", leaseId);

			TcpClient? tcpClient = null;
			try
			{
				const int TimeoutSeconds = 30;

				tcpClient = await _listenerService.WaitForClientAsync(new ByteString(computeTask.Nonce.Memory), TimeSpan.FromSeconds(TimeoutSeconds), cancellationToken);
				if (tcpClient == null)
				{
					_logger.LogInformation("Timed out waiting for connection after {Time}s.", TimeoutSeconds); 
					return LeaseResult.Success;
				}

				await using (ClientComputeSocket socket = new ClientComputeSocket(new TcpTransport(tcpClient.Client), _logger))
				{
					await RunAsync(socket, cancellationToken);
					await socket.CloseAsync(cancellationToken);
					return LeaseResult.Success;
				}
			}
			catch (Exception ex)
			{
				_logger.LogError(ex, "Exception while executing compute task: {Message}", ex.Message);
				return LeaseResult.Failed;
			}
			finally
			{
				tcpClient?.Dispose();
			}
		}

		public async Task RunAsync(IComputeSocket socket, CancellationToken cancellationToken)
		{
			await RunAsync(socket, 0, cancellationToken);
		}

		async Task RunAsync(IComputeSocket socket, int channelId, CancellationToken cancellationToken)
		{
			await using (IComputeMessageChannel channel = socket.CreateMessageChannel(channelId, 30 * 1024 * 1024, _logger))
			{
				List<Task> childTasks = new List<Task>();
				for (; ; )
				{
					using IComputeMessage message = await channel.ReceiveAsync(cancellationToken);
					_logger.LogTrace("Compute Channel {ChannelId}: {MessageType}", channelId, message.Type);

					switch (message.Type)
					{
						case ComputeMessageType.None:
							await Task.WhenAll(childTasks);
							return;
						case ComputeMessageType.Fork:
							{
								ForkMessage fork = message.ParseForkMessage();
								childTasks.Add(RunAsync(socket, fork.ChannelId, cancellationToken));
							}
							break;
						case ComputeMessageType.WriteFiles:
							{
								UploadFilesMessage writeFiles = message.ParseUploadFilesMessage();
								await WriteFilesAsync(channel, writeFiles.Name, writeFiles.Locator, cancellationToken);
							}
							break;
						case ComputeMessageType.DeleteFiles:
							{
								DeleteFilesMessage deleteFiles = message.ParseDeleteFilesMessage();
								DeleteFiles(deleteFiles.Filter);
							}
							break;
						case ComputeMessageType.Execute:
							{
								ExecuteProcessMessage executeProcess = message.ParseExecuteProcessMessage();
								await ExecuteProcessAsync(socket, channel, executeProcess.Executable, executeProcess.Arguments, executeProcess.WorkingDir, executeProcess.EnvVars, cancellationToken);  
							}
							break;
						case ComputeMessageType.XorRequest:
							{
								XorRequestMessage xorRequest = message.AsXorRequest();
								RunXor(channel, xorRequest.Data, xorRequest.Value);
							}
							break;
						default:
							throw new ComputeInvalidMessageException(message);
					}
				}
			}
		}

		static void RunXor(IComputeMessageChannel channel, ReadOnlyMemory<byte> source, byte value)
		{
			using IComputeMessageBuilder response = channel.CreateMessage(ComputeMessageType.XorResponse, source.Length);
			XorData(source.Span, response.GetSpanAndAdvance(source.Length), value);
			response.Send();
		}

		static void XorData(ReadOnlySpan<byte> source, Span<byte> target, byte value)
		{
			for (int idx = 0; idx < source.Length; idx++)
			{
				target[idx] = (byte)(source[idx] ^ value);
			}
		}

		async Task WriteFilesAsync(IComputeMessageChannel channel, string path, NodeLocator locator, CancellationToken cancellationToken)
		{
			using ComputeStorageClient store = new ComputeStorageClient(channel);
			TreeReader reader = new TreeReader(store, _memoryCache, _logger);

			DirectoryNode directoryNode = await reader.ReadNodeAsync<DirectoryNode>(locator, cancellationToken);

			DirectoryReference outputDir = DirectoryReference.Combine(_sandboxDir, path);
			if (!outputDir.IsUnderDirectory(_sandboxDir))
			{
				throw new InvalidOperationException("Cannot write files outside sandbox");
			}

			await directoryNode.CopyToDirectoryAsync(reader, outputDir.ToDirectoryInfo(), _logger, cancellationToken);

			using (IComputeMessageBuilder message = channel.CreateMessage(ComputeMessageType.WriteFilesResponse))
			{
				message.Send();
			}
		}

		void DeleteFiles(IReadOnlyList<string> deleteFiles)
		{
			FileFilter filter = new FileFilter(deleteFiles);

			List<FileReference> files = filter.ApplyToDirectory(_sandboxDir, false);
			foreach (FileReference file in files)
			{
				FileUtils.ForceDeleteFile(file);
			}
		}

		async Task ExecuteProcessAsync(IComputeSocket socket, IComputeMessageChannel channel, string executable, IReadOnlyList<string> arguments, string? workingDir, IReadOnlyDictionary<string, string?>? envVars, CancellationToken cancellationToken)
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				await ExecuteProcessWindowsAsync(socket, channel, executable, arguments, workingDir, envVars, cancellationToken);
			}
			else
			{
				await ExecuteProcessInternalAsync(channel, executable, arguments, workingDir, envVars, cancellationToken);
			}
		}

		async Task ExecuteProcessWindowsAsync(IComputeSocket socket, IComputeMessageChannel channel, string executable, IReadOnlyList<string> arguments, string? workingDir, IReadOnlyDictionary<string, string?>? envVars, CancellationToken cancellationToken)
		{
			await using IpcComputeMessageChannel ipcChannel = new IpcComputeMessageChannel(4096, _logger);

			Dictionary<string, string?> newEnvVars = new Dictionary<string, string?>();
			if (envVars != null)
			{
				foreach ((string name, string? value) in envVars)
				{
					newEnvVars.Add(name, value);
				}
			}

			newEnvVars[ComputeSocket.WorkerIpcEnvVar] = ipcChannel.GetStringHandle();

			Task processTask = ExecuteProcessInternalAsync(channel, executable, arguments, workingDir, newEnvVars, cancellationToken);
			processTask = processTask.ContinueWith(x => ipcChannel.ForceComplete(), cancellationToken, TaskContinuationOptions.None, TaskScheduler.Default);

			List<IDisposable> buffers = new List<IDisposable>();
			try
			{
				for (; ; )
				{
					using IComputeMessage message = await ipcChannel.ReceiveAsync(cancellationToken);
					switch (message.Type)
					{
						case ComputeMessageType.None:
							await processTask;
							return;
						case ComputeMessageType.AttachRecvBuffer:
							{
								AttachRecvBufferRequest attachRecvBuffer = IpcComputeMessageChannel.ParseAttachRecvBuffer(message);
#pragma warning disable CA2000 // Dispose objects before losing scope
								(IComputeBufferReader reader, IComputeBufferWriter writer) = SharedMemoryBuffer.OpenIpcHandle(attachRecvBuffer.Handle).ToShared();
								reader.Dispose();
								socket.AttachRecvBuffer(attachRecvBuffer.ChannelId, writer);
#pragma warning restore CA2000 // Dispose objects before losing scope
							}
							break;
						case ComputeMessageType.AttachSendBuffer:
							{
								AttachSendBufferRequest attachSendBuffer = IpcComputeMessageChannel.ParseAttachSendBuffer(message);
#pragma warning disable CA2000 // Dispose objects before losing scope
								(IComputeBufferReader reader, IComputeBufferWriter writer) = SharedMemoryBuffer.OpenIpcHandle(attachSendBuffer.Handle).ToShared();
								writer.Dispose();
								socket.AttachSendBuffer(attachSendBuffer.ChannelId, reader);
#pragma warning restore CA2000 // Dispose objects before losing scope
							}
							break;
						default:
							throw new ComputeInvalidMessageException(message);
					}
				}
			}
			finally
			{
				foreach (SharedMemoryBuffer buffer in buffers)
				{
					buffer.Dispose();
				}
			}
		}

		async Task ExecuteProcessInternalAsync(IComputeMessageChannel channel, string executable, IReadOnlyList<string> arguments, string? workingDir, IReadOnlyDictionary<string, string?>? envVars, CancellationToken cancellationToken)
		{
			string resolvedExecutable = FileReference.Combine(_sandboxDir, executable).FullName;
			string resolvedCommandLine = CommandLineArguments.Join(arguments);
			string resolvedWorkingDir = DirectoryReference.Combine(_sandboxDir, workingDir ?? String.Empty).FullName;

			Dictionary<string, string> resolvedEnvVars = ManagedProcess.GetCurrentEnvVars();
			if (envVars != null)
			{
				foreach ((string key, string? value) in envVars)
				{
					if (value == null)
					{
						resolvedEnvVars.Remove(key);
					}
					else
					{
						resolvedEnvVars[key] = value;
					}
				}
			}

			using (ManagedProcessGroup group = new ManagedProcessGroup())
			{
				using (ManagedProcess process = new ManagedProcess(group, resolvedExecutable, resolvedCommandLine, resolvedWorkingDir, resolvedEnvVars, null, ProcessPriorityClass.Normal))
				{
					byte[] buffer = new byte[1024];
					for (; ; )
					{
						int length = await process.ReadAsync(buffer, 0, buffer.Length, cancellationToken);
						if (length == 0)
						{
							channel.SendExecuteResult(process.ExitCode);
							return;
						}
						channel.SendExecuteOutput(buffer.AsMemory(0, length));
					}
				}
			}
		}
	}
}

