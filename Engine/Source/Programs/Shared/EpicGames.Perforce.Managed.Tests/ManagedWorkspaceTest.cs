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
	private string SyncDir => Path.Join(TempDir.FullName, "Sync");
	private string StreamName => Fixture.StreamFooMain.Root;
	
	[TestMethod]
	public async Task SyncFromScratch()
	{
		ManagedWorkspace workspace = await GetManagedWorkspace();
		await workspace.SetupAsync(PerforceConnection, StreamName, CancellationToken.None);

		await workspace.SyncAsync(PerforceConnection, StreamName, 6, Array.Empty<string>(), true,
			false, null, CancellationToken.None);

		Fixture.StreamFooMain.Changelists[6].AssertAllFilesOnDisk(SyncDir);
		Fixture.StreamFooMain.Changelists[7].AssertNotAllFilesOnDisk(SyncDir); // Contains an extra file so should fail
	}
	
	[TestMethod]
	public async Task SyncIncrementally()
	{
		ManagedWorkspace ws = await GetManagedWorkspace();
		await ws.SetupAsync(PerforceConnection, StreamName, CancellationToken.None);

		await ws.SyncAsync(PerforceConnection, StreamName, 6, Array.Empty<string>(), true,
			false, null, CancellationToken.None);

		Fixture.StreamFooMain.Changelists[6].AssertAllFilesOnDisk(SyncDir);
		Fixture.StreamFooMain.Changelists[7].AssertNotAllFilesOnDisk(SyncDir); // Contains an extra file so should fail
		
		await ws.SyncAsync(PerforceConnection, StreamName, 7, Array.Empty<string>(), true,
			false, null, CancellationToken.None);

		Fixture.StreamFooMain.Changelists[6].AssertNotAllFilesOnDisk(SyncDir);
		Fixture.StreamFooMain.Changelists[7].AssertAllFilesOnDisk(SyncDir);
	}

	private async Task<ManagedWorkspace> GetManagedWorkspace(bool overwrite = true)
	{
		ILogger<ManagedWorkspace> logger = LoggerFactory.CreateLogger<ManagedWorkspace>();
		return await ManagedWorkspace.LoadOrCreateAsync(Environment.MachineName, TempDir, overwrite, logger, CancellationToken.None);
	}
}