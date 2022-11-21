// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce.Fixture;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicGames.Perforce.Managed.Tests;

[TestClass]
public class ManagedWorkspaceTest : BasePerforceFixtureTest
{
	private string SyncDir => Path.Join(TempDir.FullName, "Sync");
	private string StreamName => Fixture.StreamFooMain.Root;

	private readonly ILogger<ManagedWorkspace> _mwLogger;

	public ManagedWorkspaceTest()
	{
		_mwLogger = LoggerFactory.CreateLogger<ManagedWorkspace>();		
	}

	[TestMethod]
	public async Task SyncFromScratch()
	{
		ManagedWorkspace workspace = await GetManagedWorkspace();
		await workspace.SetupAsync(PerforceConnection, StreamName, CancellationToken.None);

		await workspace.SyncAsync(PerforceConnection, StreamName, 6, Array.Empty<string>(), true,
			false, null, CancellationToken.None);

		Fixture.StreamFooMain.Changelists[6].AssertDepotFiles(SyncDir);
	}
	
	[TestMethod]
	public async Task SyncIncrementally()
	{
		ManagedWorkspace ws = await GetManagedWorkspace();
		await ws.SetupAsync(PerforceConnection, StreamName, CancellationToken.None);

		await ws.SyncAsync(PerforceConnection, StreamName, 6, Array.Empty<string>(), true, false, null, CancellationToken.None);
		Fixture.StreamFooMain.Changelists[6].AssertDepotFiles(SyncDir);
		
		await ws.SyncAsync(PerforceConnection, StreamName, 7, Array.Empty<string>(), true, false, null, CancellationToken.None);
		Fixture.StreamFooMain.Changelists[7].AssertDepotFiles(SyncDir);
		
		// Go back one change number
		await ws.SyncAsync(PerforceConnection, StreamName, 6, Array.Empty<string>(), true, false, null, CancellationToken.None);
		Fixture.StreamFooMain.Changelists[6].AssertDepotFiles(SyncDir);
	}
	
	[TestMethod]
	public async Task SyncIncrementallyWithoutHaveTable()
	{
		ManagedWorkspace ws = await GetManagedWorkspace();
		await ws.SetupAsync(PerforceConnection, StreamName, CancellationToken.None);

		await ws.SyncWithoutHaveTableAsync(PerforceConnection, StreamName, 6, Array.Empty<string>(), true, false, null, CancellationToken.None);
		Fixture.StreamFooMain.Changelists[6].AssertDepotFiles(SyncDir);
		
		await ws.SyncWithoutHaveTableAsync(PerforceConnection, StreamName, 7, Array.Empty<string>(), true, false, null, CancellationToken.None);
		Fixture.StreamFooMain.Changelists[7].AssertDepotFiles(SyncDir);
		
		// Go back one change number
		await ws.SyncWithoutHaveTableAsync(PerforceConnection, StreamName, 6, Array.Empty<string>(), true, false, null, CancellationToken.None);
		Fixture.StreamFooMain.Changelists[6].AssertDepotFiles(SyncDir);
	}
	
	[TestMethod]
	public async Task SyncUsingCacheFiles()
	{
		ManagedWorkspace ws = await GetManagedWorkspace();
		await ws.SetupAsync(PerforceConnection, StreamName, CancellationToken.None);

		FileReference GetCacheFilePath(int changeNumber)
		{
			return new FileReference(Path.Join(TempDir.FullName, $"CacheFile-{changeNumber}.bin")); 
		}
		
		// Sync and create a new cache file per change number
		for (int i = 2; i < Fixture.StreamFooMain.Changelists.Count; i++)
		{
			Console.WriteLine("Syncing CL " + i);
			await ws.SyncAsync(PerforceConnection, StreamName, i, Array.Empty<string>(), true, false, GetCacheFilePath(i), CancellationToken.None);
			Fixture.StreamFooMain.Changelists[i].AssertDepotFiles(SyncDir);
		}
		
		// Sync again but using the cache files created above
		for (int i = 2; i < Fixture.StreamFooMain.Changelists.Count; i++)
		{
			Console.WriteLine("Syncing CL " + i);
			await ws.SyncAsync(PerforceConnection, StreamName, i, Array.Empty<string>(), true, false, GetCacheFilePath(i), CancellationToken.None);
			Fixture.StreamFooMain.Changelists[i].AssertDepotFiles(SyncDir);
		}
	}

	private async Task<ManagedWorkspace> GetManagedWorkspace(bool overwrite = true)
	{
		return await ManagedWorkspace.LoadOrCreateAsync(Environment.MachineName, TempDir, overwrite, _mwLogger, CancellationToken.None);
	}
}