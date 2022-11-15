// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Perforce.Fixture;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicGames.Perforce.Managed.Tests;

[TestClass]
public class ManagedWorkspaceTest : BasePerforceFixtureTest
{
	[TestMethod]
	public async Task SimpleSync()
	{
		ILogger<ManagedWorkspace> logger = LoggerFactory.CreateLogger<ManagedWorkspace>();
		ManagedWorkspace workspace = await ManagedWorkspace.LoadOrCreateAsync(Environment.MachineName, TempDir, true, logger, CancellationToken.None);
		
		await workspace.SetupAsync(PerforceConnection, "//Foo/Main", CancellationToken.None);

		await workspace.SyncAsync(PerforceConnection, "//Foo/Main", 6, Array.Empty<string>(), true,
			false, null, CancellationToken.None);

		Assert.IsTrue(File.Exists(Path.Join(TempDir.FullName, "Sync", "main.cpp")));
		Assert.IsTrue(File.Exists(Path.Join(TempDir.FullName, "Sync", "main.h")));
		Assert.IsTrue(File.Exists(Path.Join(TempDir.FullName, "Sync", "shared.h")));
		Assert.IsTrue(File.Exists(Path.Join(TempDir.FullName, "Sync", "Data", "data.txt")));
	}
}