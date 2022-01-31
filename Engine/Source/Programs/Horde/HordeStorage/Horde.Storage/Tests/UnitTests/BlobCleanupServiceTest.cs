// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Dasync.Collections;
using Horde.Storage.Implementation;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

namespace Horde.Storage.UnitTests
{
    [TestClass]
    public class BlobCleanupServiceTest
    {
        [TestMethod]
        public async Task CleanupOnPoll()
        {
            GCSettings gcSettings = new GCSettings()
            {
                CleanOldBlobs = false
            };
            TestOptionsMonitor<GCSettings> gcSettingsMon = new TestOptionsMonitor<GCSettings>(gcSettings);
            BlobCleanupService blobCleanupService = new BlobCleanupService(Mock.Of<IServiceProvider>(), gcSettingsMon);

            Mock<IBlobCleanup> store1 = new Mock<IBlobCleanup>();
            store1.Setup(cleanup => cleanup.ShouldRun()).Returns(true);
            Mock<IBlobCleanup> store2 = new Mock<IBlobCleanup>();
            store2.Setup(cleanup => cleanup.ShouldRun()).Returns(true);
            blobCleanupService.RegisterCleanup(store1.Object);
            blobCleanupService.RegisterCleanup(store2.Object);

            await blobCleanupService.OnPoll(blobCleanupService.State, new CancellationTokenSource().Token);
            
            store1.Verify(m => m.Cleanup(It.IsAny<CancellationToken>()), Times.Once);
            store2.Verify(m => m.Cleanup(It.IsAny<CancellationToken>()), Times.Once);
        }
    }
}
