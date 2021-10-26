// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Logs;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Collections.Generic;
using System.Text;

namespace HordeServerTests
{
	[TestClass]
	public class LogChunkTests
	{
		string[] Text =
		{
			@"{ ""id"": 1, ""message"": ""foo"" }",
			@"{ ""id"": 2, ""message"": ""bar"" }",
			@"{ ""id"": 3, ""message"": ""baz"" }",
		};

		[TestMethod]
		public void TestSerialization()
		{
			byte[] TextData = Encoding.UTF8.GetBytes(String.Join("\n", Text) + "\n");

			List<LogSubChunkData> SubChunks = new List<LogSubChunkData>();
			SubChunks.Add(new LogSubChunkData(LogType.Text, 0, 0, new LogText(TextData, TextData.Length)));
			SubChunks.Add(new LogSubChunkData(LogType.Json, TextData.Length, 3, new LogText(TextData, TextData.Length)));

			LogChunkData OldChunkData = new LogChunkData(0, 0, SubChunks);
			byte[] Data = OldChunkData.ToByteArray();
			LogChunkData NewChunkData = LogChunkData.FromMemory(Data, 0, 0);

			Assert.AreEqual(OldChunkData.Length, NewChunkData.Length);
			Assert.AreEqual(OldChunkData.SubChunks.Count, NewChunkData.SubChunks.Count);
			Assert.IsTrue(OldChunkData.SubChunkOffset.AsSpan(0, OldChunkData.SubChunkOffset.Length).SequenceEqual(NewChunkData.SubChunkOffset.AsSpan(0, NewChunkData.SubChunkOffset.Length)));
			Assert.IsTrue(OldChunkData.SubChunkLineIndex.AsSpan(0, OldChunkData.SubChunkLineIndex.Length).SequenceEqual(NewChunkData.SubChunkLineIndex.AsSpan(0, NewChunkData.SubChunkLineIndex.Length)));
			for (int Idx = 0; Idx < OldChunkData.SubChunks.Count; Idx++)
			{
				LogSubChunkData OldSubChunkData = OldChunkData.SubChunks[Idx];
				LogSubChunkData NewSubChunkData = NewChunkData.SubChunks[Idx];
				Assert.IsTrue(OldSubChunkData.InflateText().Data.Span.SequenceEqual(NewSubChunkData.InflateText().Data.Span));
			}
		}
	}
}
