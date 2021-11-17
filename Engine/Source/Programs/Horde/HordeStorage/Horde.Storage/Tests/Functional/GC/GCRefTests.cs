// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Threading.Tasks;
using Dasync.Collections;
using Horde.Storage.Controllers;
using Horde.Storage.Implementation;
using Jupiter.Implementation;
using Microsoft.AspNetCore.TestHost;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using Serilog;
using Serilog.Core;

namespace Horde.Storage.FunctionalTests.GC
{
    [TestClass]

    public class GCRefTests
    {
        private TestServer? _server;
        private HttpClient? _httpClient;
        private Mock<IRefsStore>? _refMock;

        private readonly NamespaceId TestNamespace = new NamespaceId("test-namespace");
        private readonly BucketId DefaultBucket = new BucketId("default");

        [TestInitialize]
        public void Setup()
        {
            IConfigurationRoot configuration = new ConfigurationBuilder()
                            // we are not reading the base appSettings here as we want exact control over what runs in the tests
                            .AddJsonFile("appsettings.Testing.json", false)
                            .AddEnvironmentVariables()
                            .Build();

            Logger logger = new LoggerConfiguration()
                .ReadFrom.Configuration(configuration)
                .CreateLogger();

            OldRecord[] oldRecords =
            {
                new OldRecord(TestNamespace, DefaultBucket, new KeyId("object2")),
                new OldRecord(TestNamespace, DefaultBucket, new KeyId("object5")),
                new OldRecord(TestNamespace, DefaultBucket, new KeyId("object6")),
            };
            _refMock = new Mock<IRefsStore>();
            _refMock.Setup(store => store.GetOldRecords(TestNamespace, It.IsAny<TimeSpan>())).Returns(oldRecords.ToAsyncEnumerable()).Verifiable();
            _refMock.Setup(store => store.Delete(TestNamespace, DefaultBucket, It.IsAny<KeyId>())).Verifiable();

            TestServer server = new TestServer(new WebHostBuilder()
                .UseConfiguration(configuration)
                .UseEnvironment("Testing")
                .UseSerilog(logger)
                .UseStartup<HordeStorageStartup>()
                .ConfigureTestServices(collection =>
                {
                    // use our refs mock instead of the actual refs store so we can control which records are invalid
                    collection.AddSingleton<IRefsStore>(_refMock.Object);
                })
            );
            _httpClient = server.CreateClient();
            _server = server;
            
        }


        [TestMethod]
        public async Task RunRefCleanup()
        {
            // trigger the cleanup
            var cleanupResponse = await _httpClient!.PostAsync($"api/v1/admin/refCleanup/{TestNamespace}", new StringContent(string.Empty));
            cleanupResponse.EnsureSuccessStatusCode();
            RemovedRefRecordsResponse removedRefRecords = await cleanupResponse.Content.ReadAsAsync<RemovedRefRecordsResponse>();

            KeyId[] expectedRemovedRefRecords = new[]
            {
                new KeyId("object2"),
                new KeyId("object5"),
                new KeyId("object6")
            };
            Assert.AreEqual(3, removedRefRecords.RemovedRecords.Length);
            foreach (RemovedRefRecordsResponse.RemovedRecord removedRecord in removedRefRecords.RemovedRecords)
            {
                Assert.AreEqual("default", removedRecord.Bucket.ToString());
                Assert.IsTrue(expectedRemovedRefRecords.Contains(removedRecord.Name));
            }

            _refMock!.Verify();
            _refMock.VerifyNoOtherCalls();
        }
    }
}
