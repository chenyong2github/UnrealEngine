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
using EpicGames.Horde.Compute.Sockets;
using EpicGames.Horde.Compute.Transports;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.VisualStudio.TestPlatform.CrossPlatEngine;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicGames.Horde.Tests
{
	[TestClass]
	public class BufferTests
	{
		[TestMethod]
		public async Task TestPooledBuffer()
		{
			await TestProducerConsumerAsync(length => new PooledBuffer(length), CancellationToken.None);
		}

		[TestMethod]
		public async Task TestSharedMemoryBuffer()
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				await TestProducerConsumerAsync(length => new SharedMemoryBuffer(length), CancellationToken.None);
			}
		}

		static async Task TestProducerConsumerAsync(Func<int, IComputeBuffer> createBuffer, CancellationToken cancellationToken)
		{
			const int Length = 8000;

			Pipe sourceToTargetPipe = new Pipe();
			Pipe targetToSourcePipe = new Pipe();

			await using IComputeSocket sourceSocket = new ClientComputeSocket(new PipeTransport(targetToSourcePipe.Reader, sourceToTargetPipe.Writer), NullLogger.Instance);
			await using IComputeSocket targetSocket = new ClientComputeSocket(new PipeTransport(sourceToTargetPipe.Reader, targetToSourcePipe.Writer), NullLogger.Instance);

			using IComputeBufferWriter sourceWriter = await sourceSocket.AttachSendBufferAsync(0, createBuffer(Length), cancellationToken);
			using IComputeBufferReader targetReader = await targetSocket.AttachRecvBufferAsync(0, createBuffer(Length), cancellationToken);

			byte[] input = RandomNumberGenerator.GetBytes(Length);
			Task producerTask = RunProducerAsync(sourceWriter, input);

			byte[] output = new byte[Length];
			await RunConsumerAsync(targetReader, output);

			await producerTask;
			Assert.IsTrue(input.SequenceEqual(output));
		}

		static async Task RunProducerAsync(IComputeBufferWriter writer, ReadOnlyMemory<byte> input)
		{
			int offset = 0;
			while (offset < input.Length)
			{
				int length = Math.Min(input.Length - offset, 100);
				input.Slice(offset, length).CopyTo(writer.GetMemory());
				writer.Advance(length);
				await Task.Delay(10);
				offset += length;
			}
			writer.MarkComplete();
		}

		static async Task RunConsumerAsync(IComputeBufferReader reader, Memory<byte> output)
		{
			int offset = 0;
			while (!reader.IsComplete)
			{
				ReadOnlyMemory<byte> memory = reader.GetMemory();
				int length = Math.Min(memory.Length, 7);
				memory.Slice(0, length).CopyTo(output.Slice(offset));
				reader.Advance(length);
				await reader.WaitForDataAsync(memory.Length - length, CancellationToken.None);
				offset += length;
			}
		}
	}
}
