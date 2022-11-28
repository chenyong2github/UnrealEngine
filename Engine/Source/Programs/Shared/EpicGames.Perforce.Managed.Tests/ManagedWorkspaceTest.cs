// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
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
	private StreamFixture Stream => Fixture.StreamFooMain;

	private readonly ILogger<ManagedWorkspace> _mwLogger;

	public ManagedWorkspaceTest()
	{
		_mwLogger = LoggerFactory.CreateLogger<ManagedWorkspace>();		
	}

	[TestMethod]
	public async Task SyncSingleChangelist()
	{
		ManagedWorkspace ws = await GetManagedWorkspace();
		await AssertHaveTableFileCount(0);

		await SyncAsync(ws, 6);
		Stream.GetChangelist(6).AssertDepotFiles(SyncDir);
		await Stream.GetChangelist(6).AssertHaveTableAsync(PerforceConnection);
	}
	
	[TestMethod]
	public async Task SyncSingleChangelistWithoutHaveTable()
	{
		ManagedWorkspace ws = await GetManagedWorkspace();
		await AssertHaveTableFileCount(0);

		await SyncAsync(ws, 6, useHaveTable: false);
		Stream.GetChangelist(6).AssertDepotFiles(SyncDir);
		await AssertHaveTableFileCount(0);
	}
	
	[TestMethod]
	public async Task SyncBackwardsToOlderChangelistRemoveUntracked()
	{
		ManagedWorkspace ws = await GetManagedWorkspace();

		await SyncAsync(ws, 6, removeUntracked: false);
		Stream.GetChangelist(6).AssertDepotFiles(SyncDir);
		await Stream.GetChangelist(6).AssertHaveTableAsync(PerforceConnection);
		
		await SyncAsync(ws, 7, removeUntracked: false);
		Stream.GetChangelist(7).AssertDepotFiles(SyncDir);
		await Stream.GetChangelist(7).AssertHaveTableAsync(PerforceConnection);

		await SyncAsync(ws, 6, removeUntracked: false);
		Stream.GetChangelist(6).AssertDepotFiles(SyncDir);
		await Stream.GetChangelist(6).AssertHaveTableAsync(PerforceConnection);
	}
	
	[TestMethod]
	public async Task SyncBackwardsToOlderChangelist()
	{
		ManagedWorkspace ws = await GetManagedWorkspace();

		await SyncAsync(ws, 6);
		Stream.GetChangelist(6).AssertDepotFiles(SyncDir);
		await Stream.GetChangelist(6).AssertHaveTableAsync(PerforceConnection);
		
		await SyncAsync(ws, 7);
		Stream.GetChangelist(7).AssertDepotFiles(SyncDir);
		await Stream.GetChangelist(7).AssertHaveTableAsync(PerforceConnection);
		
		// Go back one changelist
		await SyncAsync(ws, 6);
		Stream.GetChangelist(6).AssertDepotFiles(SyncDir);
		await Stream.GetChangelist(6).AssertHaveTableAsync(PerforceConnection);
	}
	
	[TestMethod]
	public async Task SyncBackwardsToOlderChangelistWithoutHaveTable()
	{
		ManagedWorkspace ws = await GetManagedWorkspace();
		
		await SyncAsync(ws, 6, useHaveTable: false);
		Stream.GetChangelist(6).AssertDepotFiles(SyncDir);
		await AssertHaveTableFileCount(0);

		await SyncAsync(ws, 7, useHaveTable: false);
		Stream.GetChangelist(7).AssertDepotFiles(SyncDir);
		await AssertHaveTableFileCount(0);
		
		// Go back one changelist
		await SyncAsync(ws, 6, useHaveTable: false);
		Stream.GetChangelist(6).AssertDepotFiles(SyncDir);
		await AssertHaveTableFileCount(0);
	}

	[TestMethod]
	public async Task SyncMixingUseOfHaveTable()
	{
		ManagedWorkspace ws = await GetManagedWorkspace();

		await SyncAsync(ws, 1, useHaveTable: false);
		Stream.GetChangelist(1).AssertDepotFiles(SyncDir);
		await AssertHaveTableFileCount(0);
		
		await SyncAsync(ws, 2, useHaveTable: true);
		Stream.GetChangelist(2).AssertDepotFiles(SyncDir);
		await Stream.GetChangelist(2).AssertHaveTableAsync(PerforceConnection);
		
		await SyncAsync(ws, 3, useHaveTable: false);
		Stream.GetChangelist(3).AssertDepotFiles(SyncDir);
		await Stream.GetChangelist(2).AssertHaveTableAsync(PerforceConnection); // Have table should only match what was synced previously
	}

	[TestMethod]
	public async Task SyncUsingCacheFiles()
	{
		ManagedWorkspace ws = await GetManagedWorkspace();

		FileReference GetCacheFilePath(int changeNumber)
		{
			return new FileReference(Path.Join(TempDir.FullName, $"CacheFile-{changeNumber}.bin")); 
		}
		
		// Sync and create a new cache file per change number
		foreach (ChangelistFixture cl in Stream.Changelists)
		{
			await ws.SyncAsync(PerforceConnection, StreamName, cl.Number, Array.Empty<string>(), true, false, true, GetCacheFilePath(cl.Number), CancellationToken.None);
			cl.AssertDepotFiles(SyncDir);
			await cl.AssertHaveTableAsync(PerforceConnection);
		}
		
		// Sync again but using the cache files created above
		foreach (ChangelistFixture cl in Stream.Changelists.Reverse())
		{
			await ws.SyncAsync(PerforceConnection, StreamName, cl.Number, Array.Empty<string>(), true, false, true, GetCacheFilePath(cl.Number), CancellationToken.None);
			cl.AssertDepotFiles(SyncDir);
			await cl.AssertHaveTableAsync(PerforceConnection);
		}
	}
	
	[DataTestMethod]
	[DataRow(true, DisplayName = "With have-table")]
	[DataRow(false, DisplayName = "Without have-table")]
	public async Task Populate(bool useHaveTable)
	{
		ManagedWorkspace ws = await GetManagedWorkspace();

		List<PopulateRequest> populateRequests = new ()
		{
			new PopulateRequest(PerforceConnection, StreamName, new List<string>())
		};
		await ws.PopulateAsync(populateRequests, false, useHaveTable, CancellationToken.None);
		Stream.LatestChangelist.AssertDepotFiles(SyncDir);
		await Stream.LatestChangelist.AssertHaveTableAsync(PerforceConnection, useHaveTable);
	}

	private async Task<ManagedWorkspace> GetManagedWorkspace()
	{
		ManagedWorkspace ws = await ManagedWorkspace.CreateAsync(Environment.MachineName, TempDir, _mwLogger, CancellationToken.None);
		await ws.SetupAsync(PerforceConnection, StreamName, CancellationToken.None);
		return ws;
	}
	
	private async Task SyncAsync(ManagedWorkspace managedWorkspace, int changeNumber, bool useHaveTable = true, FileReference? cacheFile = null, bool removeUntracked = true)
	{
		await managedWorkspace.SyncAsync(PerforceConnection, StreamName, changeNumber, Array.Empty<string>(), removeUntracked, false, useHaveTable, cacheFile, CancellationToken.None);
	}
	
	private async Task AssertHaveTableFileCount(int expected)
	{
		List<HaveRecord> haveRecords = await PerforceConnection.HaveAsync(new FileSpecList(), CancellationToken.None).ToListAsync();

		if (haveRecords.Count != expected)
		{
			Console.WriteLine("Have table contains:");
			foreach (HaveRecord haveRecord in haveRecords)
			{
				Console.WriteLine(haveRecord.DepotFile + "#" + haveRecord.HaveRev);
			}
			Assert.Fail($"Actual have table file count does not match expected count. Actual={haveRecords.Count} Expected={expected}");
		}
	}

	private void DumpMetaDir()
	{
		Console.WriteLine("Meta dir: --------------------------");
		foreach (string path in Directory.GetFiles(TempDir.FullName, "*", SearchOption.AllDirectories))
		{
			Console.WriteLine(Path.GetRelativePath(TempDir.FullName, path));
		}
	}
}