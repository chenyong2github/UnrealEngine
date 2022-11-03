// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using EpicGames.Core;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicGames.Perforce.Managed.Tests;

[TestClass]
public class DepotStreamTreeBuilderTest
{
	private readonly StreamFile _file1 = new ("//UE5/Main/main.cpp", 100, new FileContentId(Md5Hash.Zero, "text"), 10);
	private readonly StreamFile _file2 = new ("//UE5/Main/Data/data1.bin", 200, new FileContentId(Md5Hash.Zero, "binary"), 20);
	private readonly StreamFile _file3 = new ("//UE5/Main/Data/data2.bin", 300, new FileContentId(Md5Hash.Zero, "binary"), 30);
	
	[TestMethod]
	public void Basic()
	{
		DepotStreamTreeBuilder builder = new ("//UE5/");
		builder.AddDepotFile(_file1);
		builder.AddDepotFile(_file2);
		builder.AddDepotFile(_file3);
		StreamSnapshotFromMemory snapshot = new (builder);

		StreamTree ue5 = snapshot.Lookup(snapshot.Root);
		{
			Assert.AreEqual(0, ue5.NameToFile.Count);
			Assert.AreEqual(1, ue5.NameToTree.Count);
		}

		StreamTree main = snapshot.Lookup(ue5.NameToTree["Main"]);
		{
			Assert.AreEqual(1, main.NameToFile.Count);
			Assert.AreEqual(1, main.NameToTree.Count);
			
			Assert.AreEqual(100, main.NameToFile["main.cpp"].Length);
		}
		
		StreamTree data = snapshot.Lookup(main.NameToTree["Data"]);
		{
			Assert.AreEqual(2, data.NameToFile.Count);
			Assert.AreEqual(0, data.NameToTree.Count);
			
			Assert.AreEqual(200, data.NameToFile["data1.bin"].Length);
			Assert.AreEqual(300, data.NameToFile["data2.bin"].Length);
		}
	}

	[TestMethod]
	public void InstantiateWithInvalidPrefix()
	{
		Assert.ThrowsException<ArgumentException>(() => new DepotStreamTreeBuilder("//UE5"));
	}

	[TestMethod]
	public void AddFileWithInvalidPrefix()
	{
		DepotStreamTreeBuilder builder = new("//Foo/");
		Assert.ThrowsException<InvalidDataException>(() => builder.AddDepotFile(_file1));
	}
}