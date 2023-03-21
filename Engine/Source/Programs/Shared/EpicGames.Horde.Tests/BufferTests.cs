// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Buffers;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicGames.Horde.Tests
{
	[TestClass]
	public class BufferTests
	{
		[TestMethod]
		public async Task TestHeapBuffer()
		{
			using HeapBuffer buffer = new HeapBuffer(8000);
			await TestProducerConsumerAsync(buffer.Writer, buffer.Reader);
		}

		[TestMethod]
		public async Task TestIpcBuffer()
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				using IpcBuffer buffer1 = IpcBuffer.CreateNew(16384);
				string info = buffer1.DuplicateIntoProcess(Process.GetCurrentProcess().Handle);
				using IpcBuffer buffer2 = IpcBuffer.OpenExisting(info);
				await TestProducerConsumerAsync(buffer1.Writer, buffer2.Reader);
			}
		}

		static async Task TestProducerConsumerAsync(IComputeBufferWriter writer, IComputeBufferReader reader)
		{
			const int Length = 8000;

			byte[] input = RandomNumberGenerator.GetBytes(Length);
			Task producerTask = RunProducerAsync(writer, input);

			byte[] output = new byte[Length];
			await RunConsumerAsync(reader, output);

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
				await reader.WaitAsync(memory.Length - length, CancellationToken.None);
				offset += length;
			}
		}
	}
}
