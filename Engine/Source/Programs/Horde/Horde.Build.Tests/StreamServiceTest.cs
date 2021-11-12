// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Globalization;
using System.Threading.Tasks;
using HordeServer.Models;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;

namespace HordeServerTests
{
    [TestClass]
    public class StreamServiceTests : TestSetup
    {
        [TestMethod]
        public async Task Pausing()
        {
			Fixture Fixture = await CreateFixtureAsync();

	        IStream Stream = (await StreamService.GetStreamAsync(Fixture!.Stream!.Id))!;
	        Assert.IsFalse(Stream.IsPaused(DateTime.UtcNow));
	        Assert.IsNull(Stream.PausedUntil);
	        Assert.IsNull(Stream.PauseComment);

	        DateTime PausedUntil = DateTime.UtcNow.AddHours(1);
	        await StreamService.UpdatePauseStateAsync(Stream, NewPausedUntil: PausedUntil, NewPauseComment: "mycomment");
	        Stream = (await StreamService.GetStreamAsync(Fixture!.Stream!.Id))!;
	        // Comparing by string to avoid comparing exact milliseconds as those are not persisted in MongoDB fields
	        Assert.IsTrue(Stream.IsPaused(DateTime.UtcNow));
	        Assert.AreEqual(PausedUntil.ToString(CultureInfo.InvariantCulture), Stream.PausedUntil!.Value.ToString(CultureInfo.InvariantCulture));
	        Assert.AreEqual("mycomment", Stream.PauseComment);
	        
	        await StreamService.UpdatePauseStateAsync(Stream, NewPausedUntil: null, NewPauseComment: null);
	        Stream = (await StreamService.GetStreamAsync(Fixture!.Stream!.Id))!;
	        Assert.IsFalse(Stream.IsPaused(DateTime.UtcNow));
	        Assert.IsNull(Stream.PausedUntil);
	        Assert.IsNull(Stream.PauseComment);
        }
    }
}