// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.IO.Pipelines;
using System.IO.Pipes;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Buffers;
using EpicGames.Horde.Compute.Transports;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.VisualStudio.TestPlatform.CrossPlatEngine;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicGames.Horde.Tests
{
	[TestClass]
	public class BufferTests
	{
		const int ChannelId = 0;

		[TestMethod]
		public async Task TestPooledBuffer()
		{
			await TestProducerConsumerAsync(length => new PooledBuffer(length).ToSharedInstance(), CancellationToken.None);
		}

		[TestMethod]
		public async Task TestSharedMemoryBuffer()
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				await TestProducerConsumerAsync(length => SharedMemoryBuffer.CreateNew(null, length).ToSharedInstance(), CancellationToken.None);
			}
		}

		static async Task TestProducerConsumerAsync(Func<int, IComputeBuffer> createBuffer, CancellationToken cancellationToken)
		{
			const int Length = 8000;

			Pipe sourceToTargetPipe = new Pipe();
			Pipe targetToSourcePipe = new Pipe();

			await using IComputeSocket producerSocket = ComputeSocket.Create(new PipeTransport(targetToSourcePipe.Reader, sourceToTargetPipe.Writer), NullLogger.Instance);
			await using IComputeSocket consumerSocket = ComputeSocket.Create(new PipeTransport(sourceToTargetPipe.Reader, targetToSourcePipe.Writer), NullLogger.Instance);

			using IComputeBuffer consumerBuffer = createBuffer(Length);
			consumerSocket.AttachRecvBuffer(ChannelId, consumerBuffer);

			byte[] input = RandomNumberGenerator.GetBytes(Length);
			Task producerTask = RunProducerAsync(producerSocket, input);

			byte[] output = new byte[Length];
			await RunConsumerAsync(consumerBuffer.Reader, output);

			await producerTask;
			Assert.IsTrue(input.SequenceEqual(output));
		}

		static async Task RunProducerAsync(IComputeSocket socket, ReadOnlyMemory<byte> input)
		{
			int offset = 0;
			while (offset < input.Length)
			{
				int length = Math.Min(input.Length - offset, 100);
				await socket.SendAsync(ChannelId, input.Slice(offset, length));
				await Task.Delay(10);
				offset += length;
			}
			await socket.MarkCompleteAsync(ChannelId);
		}

		static async Task RunConsumerAsync(IComputeBufferReader reader, Memory<byte> output)
		{
			int offset = 0;
			while (!reader.IsComplete)
			{
				ReadOnlyMemory<byte> memory = reader.GetMemory();
				if (memory.Length == 0)
				{
					await reader.WaitToReadAsync(0, CancellationToken.None);
					continue;
				}

				int length = Math.Min(memory.Length, 7);
				memory.Slice(0, length).CopyTo(output.Slice(offset));
				reader.Advance(length);
				offset += length;
			}
		}
	}
}
