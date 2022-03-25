// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles.Nodes;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Buffers;
using System.Collections.Generic;
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
			DirectoryNode node = new DirectoryNode();
			{
				DirectoryNode foo = node.AddDirectory("Foo");

				FileNode file1 = foo.AddFile("first file.txt", 0);
				file1.Append(Encoding.UTF8.GetBytes("first file file contents"), new ChunkingOptions());

				node.AddDirectory("Bar");

				FileNode file2 = foo.AddFile("second file.txt", 0);
				file2.Append(Encoding.UTF8.GetBytes("second file's contents"), new ChunkingOptions());
			}

			Dictionary<IoHash, ReadOnlyMemory<byte>> hashToData = new Dictionary<IoHash, ReadOnlyMemory<byte>>();
			IoHash hash = Serialize(node, hashToData);

			DirectoryNode node2 = DeserializeDirectory(hash, hashToData);
			{
				Assert.AreEqual(2, node2.Directories.Count);
				Assert.AreEqual(0, node2.Files.Count);

				Assert.IsTrue(node2.TryGetDirectoryEntry("Foo", out DirectoryEntry? fooEntry));
				Assert.IsNotNull(fooEntry);
				DirectoryNode? foo = fooEntry!.Node!;
				Assert.IsNotNull(foo);

				Assert.AreEqual(0, foo.Directories.Count);
				Assert.AreEqual(2, foo.Files.Count);

				Assert.IsTrue(foo.TryGetFileEntry("first file.txt", out FileEntry? _));
				Assert.IsTrue(foo.TryGetFileEntry("second file.txt", out FileEntry? _));

				Assert.IsTrue(node2.TryGetDirectoryEntry("Bar", out DirectoryEntry? barEntry));
				Assert.IsNotNull(barEntry);
				DirectoryNode? bar = barEntry!.Node!;
				Assert.IsNotNull(bar);
			}
		}

		static IoHash Serialize(BundleNode node, Dictionary<IoHash, ReadOnlyMemory<byte>> hashToData)
		{
			foreach (BundleNodeRef @ref in node.GetReferences())
			{
				if (@ref.Node != null)
				{
					IoHash refHash = Serialize(@ref.Node, hashToData);
					@ref.MarkAsClean(refHash);
				}
			}

			byte[] data = node.Serialize().ToArray();
			IoHash hash = IoHash.Compute(data);
			hashToData[hash] = data;
			return hash;
		}

		static DirectoryNode DeserializeDirectory(IoHash hash, Dictionary<IoHash, ReadOnlyMemory<byte>> hashToData)
		{
			ReadOnlyMemory<byte> data = hashToData[hash];
			DirectoryNode node = DirectoryNode.Deserialize(data);

			foreach (FileEntry fileEntry in node.Files)
			{
				fileEntry.Node = DeserializeFile(fileEntry.Hash, hashToData);
			}

			foreach (DirectoryEntry directoryEntry in node.Directories)
			{
				directoryEntry.Node = DeserializeDirectory(directoryEntry.Hash, hashToData);
			}

			return node;
		}

		static FileNode DeserializeFile(IoHash hash, Dictionary<IoHash, ReadOnlyMemory<byte>> hashToData)
		{
			ReadOnlyMemory<byte> data = hashToData[hash];
			FileNode node = FileNode.Deserialize(data);

			foreach (BundleNodeRef<FileNode> childRef in node.Children)
			{
				childRef.Node = DeserializeFile(hash, hashToData);
			}

			return node;
		}
	}
}
