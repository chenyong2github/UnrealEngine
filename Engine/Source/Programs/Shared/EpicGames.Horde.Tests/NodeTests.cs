// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles.Nodes;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;

namespace EpicGames.Horde.Tests
{
	[TestClass]
	public class NodeTests
	{
		[TestMethod]
		public void TestDirectoryNode()
		{
			DirectoryNode Node = new DirectoryNode();
			{
				DirectoryNode Foo = Node.AddDirectory("Foo");

				FileNode File1 = Foo.AddFile("first file.txt", 0);
				File1.Append(Encoding.UTF8.GetBytes("first file file contents"), new ChunkingOptions());

				DirectoryNode Bar = Node.AddDirectory("Bar");

				FileNode File2 = Foo.AddFile("second file.txt", 0);
				File2.Append(Encoding.UTF8.GetBytes("second file's contents"), new ChunkingOptions());
			}

			Dictionary<IoHash, ReadOnlyMemory<byte>> HashToData = new Dictionary<IoHash, ReadOnlyMemory<byte>>();
			IoHash Hash = Serialize(Node, HashToData);

			DirectoryNode Node2 = DeserializeDirectory(Hash, HashToData);
			{
				Assert.AreEqual(2, Node2.Directories.Count);
				Assert.AreEqual(0, Node2.Files.Count);

				Assert.IsTrue(Node2.TryGetDirectoryEntry("Foo", out DirectoryEntry? FooEntry));
				Assert.IsNotNull(FooEntry);
				DirectoryNode? Foo = FooEntry!.Node!;
				Assert.IsNotNull(Foo);

				Assert.AreEqual(0, Foo.Directories.Count);
				Assert.AreEqual(2, Foo.Files.Count);

				Assert.IsTrue(Foo.TryGetFileEntry("first file.txt", out FileEntry? File1));
				Assert.IsTrue(Foo.TryGetFileEntry("second file.txt", out FileEntry? File2));

				Assert.IsTrue(Node2.TryGetDirectoryEntry("Bar", out DirectoryEntry? BarEntry));
				Assert.IsNotNull(BarEntry);
				DirectoryNode? Bar = BarEntry!.Node!;
				Assert.IsNotNull(Bar);
			}
		}

		static IoHash Serialize(BundleNode Node, Dictionary<IoHash, ReadOnlyMemory<byte>> HashToData)
		{
			foreach (BundleNodeRef Ref in Node.GetReferences())
			{
				if (Ref.Node != null)
				{
					IoHash RefHash = Serialize(Ref.Node, HashToData);
					Ref.MarkAsClean(RefHash);
				}
			}

			byte[] Data = Node.Serialize().ToArray();
			IoHash Hash = IoHash.Compute(Data);
			HashToData[Hash] = Data;
			return Hash;
		}

		static DirectoryNode DeserializeDirectory(IoHash Hash, Dictionary<IoHash, ReadOnlyMemory<byte>> HashToData)
		{
			ReadOnlyMemory<byte> Data = HashToData[Hash];
			DirectoryNode Node = DirectoryNode.Deserialize(Data);

			foreach (FileEntry FileEntry in Node.Files)
			{
				FileEntry.Node = DeserializeFile(FileEntry.Hash, HashToData);
			}

			foreach (DirectoryEntry DirectoryEntry in Node.Directories)
			{
				DirectoryEntry.Node = DeserializeDirectory(DirectoryEntry.Hash, HashToData);
			}

			return Node;
		}

		static FileNode DeserializeFile(IoHash Hash, Dictionary<IoHash, ReadOnlyMemory<byte>> HashToData)
		{
			ReadOnlyMemory<byte> Data = HashToData[Hash];
			FileNode Node = FileNode.Deserialize(Data);

			foreach (BundleNodeRef<FileNode> ChildRef in Node.Children)
			{
				ChildRef.Node = DeserializeFile(Hash, HashToData);
			}

			return Node;
		}
	}
}
