// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using Horde.Agent.Execution;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Agent.Tests
{
	public static class FakeCompressor
	{
		private static readonly byte[] s_prefix = Encoding.UTF8.GetBytes("FAKE-COMPRESSED-");
		
		/// <summary>
		/// Decompress data that was fake compressed
		/// <see cref="Compress(byte[])" />
		/// </summary>
		/// <param name="input">Data to decompress</param>
		/// <returns>Decompressed data</returns>
		public static byte[] Decompress(byte[] input)
		{
			bool isPrefixCorrect = input.AsSpan(0, input.Length).SequenceEqual(s_prefix.AsSpan());
			if (isPrefixCorrect)
			{
				throw new ArgumentException("Compressed data is missing magic prefix");
			}

			return input.AsSpan(s_prefix.Length).ToArray();
		}
		
		/// <summary>
		/// Decompress data that was fake compressed
		/// <see cref="Compress(byte[])" />
		/// </summary>
		/// <param name="input">Data to decompress</param>
		/// <returns>Decompressed data</returns>
		public static string Decompress(string input)
		{
			return Encoding.UTF8.GetString(Decompress(Encoding.UTF8.GetBytes(input)));
		}
		
		/// <summary>
		/// Fake compression of input data by appending a prefix
		/// Won't make the data smaller, but it will modify it leading to a changed hash
		/// </summary>
		/// <param name="input">Data to compress</param>
		/// <returns>Compressed data</returns>
		public static byte[] Compress(byte[] input)
		{
			byte[] compressedInput = new byte[input.Length + s_prefix.Length];
			Array.Copy(s_prefix, 0, compressedInput, 0, s_prefix.Length);
			Array.Copy(input, 0, compressedInput, s_prefix.Length, input.Length);
			return compressedInput;
		}
		
		/// <summary>
		/// Fake compression of input data by appending a prefix
		/// <see cref="Compress(byte[])"/>
		/// </summary>
		/// <param name="input">Data to compress</param>
		/// <returns>Compressed data</returns>
		public static string Compress(string input)
		{
			return Encoding.UTF8.GetString(Compress(Encoding.UTF8.GetBytes(input)));
		}
	}
	
	[TestClass]
	public class ComputeTaskExecutorTest
	{
		private readonly MemoryStorageClient _storageClient = new ();
		private readonly ComputeTaskExecutor _executor;
		private readonly NamespaceId _namespaceId = new ("my-namespace");
		private readonly DirectoryReference _sandboxDir;
		
		public ComputeTaskExecutorTest()
		{
			_executor = new (_storageClient, NullLogger.Instance);
			_sandboxDir = new (Path.Join(Path.GetTempPath(), "horde-agent", Guid.NewGuid().ToString()));
			DirectoryReference.CreateDirectory(_sandboxDir);
		}

		[TestCleanup]
		public void TestCleanup()
		{
			if (Directory.Exists(_sandboxDir.FullName))
			{
				Directory.Delete(_sandboxDir.FullName, true);
			}
		}
		
		[TestMethod]
		public async Task SetupSandboxWithCompressedFiles()
		{
			DirectoryTree rootDir = new ();
			FileNode file1 = await CreateFileInStorage("file1", "file1Data", false);
			FileNode file2 = await CreateFileInStorage("file2", "file2Data", true);
			rootDir.Files.Add(file1);
			rootDir.Files.Add(file2);

			await _executor.SetupSandboxAsync(_namespaceId, rootDir, _sandboxDir);

			Assert.AreEqual("file1Data", File.ReadAllText(Path.Join(_sandboxDir.FullName, file1.Name.ToString())));
			Assert.AreEqual(FakeCompressor.Compress("file2Data"), File.ReadAllText(Path.Join(_sandboxDir.FullName, file2.Name.ToString())));
		}

		private async Task<FileNode> CreateFileInStorage(string name, string data, bool shouldCompress)
		{
			byte[] uncompressedData = Encoding.UTF8.GetBytes(data);
			IoHash uncompressedHash = IoHash.Compute(uncompressedData);
			byte[] compressedData = FakeCompressor.Compress(uncompressedData);

			if (shouldCompress)
			{
				FileNode fileNode = new (name, uncompressedHash, compressedData.Length, 0, true);
				using MemoryStream stream = new(compressedData);
				await _storageClient.WriteCompressedBlobAsync(_namespaceId, uncompressedHash, stream);
				return fileNode;
			}
			else
			{
				FileNode fileNode = new (name, uncompressedHash, uncompressedData.Length, 0, false);
				await _storageClient.WriteBlobFromMemoryAsync(_namespaceId, uncompressedData);
				return fileNode;				
			}
		}
	}
}
