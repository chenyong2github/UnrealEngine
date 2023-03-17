// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Cpp;
using EpicGames.Horde.Compute.Transports;
using EpicGames.Horde.Logs;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
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
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeHandler(ComputeListenerService listenerService, IMemoryCache memoryCache, ILogger<ComputeHandler> logger)
		{
			_listenerService = listenerService;
			_memoryCache = memoryCache;
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

				await using (ComputeLease lease = new ComputeLease(new TcpTransport(tcpClient.Client), computeTask.Resources))
				{
					await RunAsync(lease, cancellationToken);
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

		public async Task RunAsync(IComputeLease lease, CancellationToken cancellationToken)
		{
			using IComputeChannel channel = lease.CreateChannel(0);
			for (; ; )
			{
				using IComputeMessage message = await channel.ReadAsync(cancellationToken);
				switch (message.Type)
				{
					case ComputeMessageType.None:
					case ComputeMessageType.Close:
						return;
					case ComputeMessageType.XorRequest:
						{
							XorRequestMessage xorRequest = message.AsXorRequest();
							await RunXorAsync(channel, xorRequest.Data, xorRequest.Value, cancellationToken);
						}
						break;
					case ComputeMessageType.CppBegin:
						{
							CppBeginMessage cppBegin = message.AsCppBegin();
							using IComputeChannel replyChannel = lease.CreateChannel(cppBegin.ReplyChannelId);
							await RunCppAsync(replyChannel, cppBegin.Locator, cancellationToken);
						}
						break;
					default:
						throw new NotImplementedException();
				}
			}
		}

		static async Task RunXorAsync(IComputeChannel channel, ReadOnlyMemory<byte> source, byte value, CancellationToken cancellationToken)
		{
			using (IComputeMessageWriter writer = channel.CreateMessage(ComputeMessageType.XorResponse, source.Length))
			{
				XorData(source.Span, writer.GetSpanAndAdvance(source.Length), value);
				await writer.SendAsync(cancellationToken);
			}
		}

		static void XorData(ReadOnlySpan<byte> source, Span<byte> target, byte value)
		{
			for (int idx = 0; idx < source.Length; idx++)
			{
				target[idx] = (byte)(source[idx] ^ value);
			}
		}

		public async Task RunCppAsync(IComputeChannel channel, NodeLocator locator, CancellationToken cancellationToken)
		{
			try
			{
				await RunCppInternalAsync(channel, locator, cancellationToken);
			}
			catch (Exception ex)
			{
				await channel.CppFailureAsync(ex.ToString(), cancellationToken);
			}
		}

		async Task RunCppInternalAsync(IComputeChannel channel, NodeLocator locator, CancellationToken cancellationToken)
		{
			DirectoryReference sandboxDir = DirectoryReference.Combine(Program.DataDir, "Sandbox", channel.Id.ToString());

			using ComputeStorageClient store = new ComputeStorageClient(channel);
			TreeReader reader = new TreeReader(store, _memoryCache, _logger);

			CppComputeNode node = await reader.ReadNodeAsync<CppComputeNode>(locator, cancellationToken);
			DirectoryNode directoryNode = await node.Sandbox.ExpandAsync(reader, cancellationToken);

			await directoryNode.CopyToDirectoryAsync(reader, sandboxDir.ToDirectoryInfo(), _logger, cancellationToken);

			MemoryStorageClient storage = new MemoryStorageClient();
			using (TreeWriter writer = new TreeWriter(storage))
			{
				string executable = FileReference.Combine(sandboxDir, node.Executable).FullName;
				string commandLine = CommandLineArguments.Join(node.Arguments);
				string workingDir = DirectoryReference.Combine(sandboxDir, node.WorkingDirectory).FullName;

				Dictionary<string, string>? envVars = null;
				if (node.EnvVars.Count > 0)
				{
					envVars = ManagedProcess.GetCurrentEnvVars();
					foreach ((string key, string value) in node.EnvVars)
					{
						envVars[key] = value;
					}
				}

				int exitCode;

				LogWriter logWriter = new LogWriter(writer);
				using (ManagedProcessGroup group = new ManagedProcessGroup())
				{
					using (ManagedProcess process = new ManagedProcess(group, executable, commandLine, workingDir, envVars, null, ProcessPriorityClass.Normal))
					{
						byte[] buffer = new byte[1024];
						for (; ; )
						{
							int length = await process.ReadAsync(buffer, 0, buffer.Length, cancellationToken);
							if (length == 0)
							{
								process.WaitForExit();
								exitCode = process.ExitCode;
								break;
							}
							await logWriter.AppendAsync(buffer.AsMemory(0, length), cancellationToken);
						}
					}
				}

				TreeNodeRef<LogNode> logNodeRef = await logWriter.CompleteAsync(cancellationToken);

				FileFilter filter = new FileFilter(node.OutputPaths.Select(x => x.ToString()));
				List<FileReference> outputFiles = filter.ApplyToDirectory(sandboxDir, false);

				DirectoryNode outputTree = new DirectoryNode();
				await outputTree.CopyFilesAsync(sandboxDir, outputFiles, new ChunkingOptions(), writer, null, cancellationToken);

				CppComputeOutputNode outputNode = new CppComputeOutputNode(exitCode, logNodeRef, new TreeNodeRef<DirectoryNode>(outputTree));
				NodeHandle outputHandle = await writer.FlushAsync(outputNode, cancellationToken);

				await channel.CppSuccessAsync(outputHandle.Locator, cancellationToken);
			}

			for (; ; )
			{
				using IComputeMessage message = await channel.ReadAsync(cancellationToken);
				switch (message.Type)
				{
					case ComputeMessageType.CppEnd:
						FileUtils.ForceDeleteDirectory(sandboxDir);
						return;
					case ComputeMessageType.CppBlobRead:
						{
							CppBlobReadMessage cppBlobRead = message.AsCppBlobRead();
							await channel.CppBlobDataAsync(cppBlobRead, storage, cancellationToken);
						}
						break;
					default:
						throw new NotImplementedException();
				}
			}
		}

		class LogWriter
		{
			const int MaxChunkSize = 128 * 1024;
			const int FlushChunkSize = 100 * 1024;

			readonly TreeWriter _writer;
			int _lineCount;
			long _offset;
			readonly LogChunkBuilder _chunkBuilder = new LogChunkBuilder(MaxChunkSize);
			readonly List<LogChunkRef> _chunks = new List<LogChunkRef>();

			public IReadOnlyList<LogChunkRef> Chunks => _chunks;

			public LogWriter(TreeWriter writer)
			{
				_writer = writer;
			}

			public async ValueTask AppendAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken)
			{
				_chunkBuilder.Append(data.Span);

				if (_chunkBuilder.Length > FlushChunkSize)
				{
					await FlushAsync(cancellationToken);
				}
			}

			async Task FlushAsync(CancellationToken cancellationToken)
			{
				LogChunkNode chunkNode = _chunkBuilder.ToLogChunk();
				LogChunkRef chunkRef = new LogChunkRef(_lineCount, _offset, chunkNode);
				await _writer.WriteAsync(chunkRef, cancellationToken);

				_chunks.Add(chunkRef);
				_lineCount += chunkNode.LineCount;
				_offset += chunkNode.Length;
			}

			public async Task<TreeNodeRef<LogNode>> CompleteAsync(CancellationToken cancellationToken)
			{
				await FlushAsync(cancellationToken);
				LogIndexNode index = new LogIndexNode(new NgramSet(Array.Empty<ushort>()), 0, Array.Empty<LogChunkRef>());

				TreeNodeRef<LogNode> logNodeRef = new TreeNodeRef<LogNode>(new LogNode(LogFormat.Text, _lineCount, _offset, _chunks, new TreeNodeRef<LogIndexNode>(index), true));
				await _writer.WriteAsync(logNodeRef, cancellationToken);

				return logNodeRef;
			}
		}
	}
}

