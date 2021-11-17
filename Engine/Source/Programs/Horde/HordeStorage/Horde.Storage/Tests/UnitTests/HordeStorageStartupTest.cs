// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.IO;
using System.Linq;
using Horde.Storage.Implementation;
using Microsoft.AspNetCore.TestHost;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Serilog;

namespace Horde.Storage.UnitTests
{
    [TestClass]
    public class HordeStorageStartupTest
    {
        private string localTestDir;
        
        public HordeStorageStartupTest()
        {
            localTestDir = Path.Combine(Path.GetTempPath(), "HordeStorageStartupTest", Path.GetRandomFileName());
        }

        [TestMethod]
        public void StorageBackendImplConfiguration()
        {
            // No configuration set
            IBlobStore blobStore = GetBlobStoreForConfig(new Dictionary<string, string>());
            Assert.IsTrue(blobStore is MemoryCacheBlobStore);
            
            // A single blob store configuration should yield the store itself without a hierarchical wrapper store
            blobStore = GetBlobStoreForConfig(new Dictionary<string, string> {{"Horde.Storage:StorageImplementations:0", "FileSystem"}});
            Assert.IsTrue(blobStore is FileSystemStore);
            
            // Should not be case-sensitive
            blobStore = GetBlobStoreForConfig(new Dictionary<string, string> {{"Horde.Storage:StorageImplementations:0", "FiLeSYsTEm"}});
            Assert.IsTrue(blobStore is FileSystemStore);
            
            // Two or more blob stores returns a hierarchical store
            blobStore = GetBlobStoreForConfig(new Dictionary<string, string>
            {
                {"Horde.Storage:StorageImplementations:0", "FileSystem"},
                {"Horde.Storage:StorageImplementations:1", "Memory"},
            });
            Assert.IsTrue(blobStore is HierarchicalBlobStore);
            Assert.IsTrue((blobStore as HierarchicalBlobStore)!.BlobStores.ToList()[0] is FileSystemStore);
            Assert.IsTrue((blobStore as HierarchicalBlobStore)!.BlobStores.ToList()[1] is MemoryCacheBlobStore);
        }

        private IBlobStore GetBlobStoreForConfig(Dictionary<string, string> configDict)
        {
            IConfigurationRoot configuration = new ConfigurationBuilder()
                .AddJsonFile("appsettings.Testing.json", true)
                .AddInMemoryCollection(new Dictionary<string, string> {{"Filesystem:RootDir", localTestDir}})
                .AddInMemoryCollection(configDict)
                .Build();
            TestServer server = new TestServer(new WebHostBuilder()
                .UseConfiguration(configuration)
                .UseEnvironment("Testing")
                .UseSerilog(new LoggerConfiguration().ReadFrom.Configuration(configuration).CreateLogger())
                .UseStartup<HordeStorageStartup>()
            );

            return server.Services.GetService<IBlobStore>()!;
        }
    }
}
