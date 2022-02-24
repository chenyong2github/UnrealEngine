// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Mime;
using System.Text;
using System.Threading.Tasks;
using Cassandra;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Horde.Storage.Implementation;
using Horde.Storage.Implementation.Blob;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Serilog;
using Logger = Serilog.Core.Logger;

namespace Horde.Storage.FunctionalTests.Storage
{
    public abstract class BlobIndexTests
    {
        protected TestServer? _server;
        protected HttpClient? _httpClient;

        protected readonly NamespaceId TestNamespaceName = new NamespaceId("testbucket");

        [TestInitialize]
        public async Task Setup()
        {
            IConfigurationRoot configuration = new ConfigurationBuilder()
                // we are not reading the base appSettings here as we want exact control over what runs in the tests
                .AddJsonFile("appsettings.Testing.json", true)
                .AddInMemoryCollection(GetSettings())
                .AddEnvironmentVariables()
                .Build();

            Logger logger = new LoggerConfiguration()
                .ReadFrom.Configuration(configuration)
                .CreateLogger();

            TestServer server = new TestServer(new WebHostBuilder()
                .UseConfiguration(configuration)
                .UseEnvironment("Testing")
                .UseSerilog(logger)
                .UseStartup<HordeStorageStartup>()
            );
            _httpClient = server.CreateClient();
            _server = server;

            // Seed storage
            await Seed(_server.Services);
        }

        protected abstract IEnumerable<KeyValuePair<string, string>> GetSettings();

        protected abstract Task Seed(IServiceProvider serverServices);
        protected abstract Task Teardown(IServiceProvider serverServices);


        [TestCleanup]
        public async Task MyTeardown()
        {
            await Teardown(_server!.Services);
        }

        
        [TestMethod]
        public async Task PutBlobToIndex()
        {
            byte[] payload = Encoding.ASCII.GetBytes("I am a blob with contents");
            using ByteArrayContent requestContent = new ByteArrayContent(payload);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
            BlobIdentifier contentHash = BlobIdentifier.FromBlob(payload);
            HttpResponseMessage response = await _httpClient!.PutAsync(requestUri: $"api/v1/s/{TestNamespaceName}/{contentHash}", requestContent);
            response.EnsureSuccessStatusCode();

            IBlobIndex? index = _server!.Services.GetService<IBlobIndex>();
            Assert.IsNotNull(index);
            IBlobIndex.BlobInfo? blobInfo = await index.GetBlobInfo(TestNamespaceName, contentHash);

            Assert.IsNotNull(blobInfo);
            Assert.IsTrue(blobInfo.Regions.Contains("test"));
        }

        [TestMethod]
        public async Task UploadRef()
        {
            CbWriter writer = new CbWriter();
            writer.BeginObject();
            writer.WriteString("stringField","thisIsAField");
            writer.EndObject();

            byte[] objectData = writer.ToByteArray();
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(objectData);

            HttpContent requestContent = new ByteArrayContent(objectData);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
            requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());
            IoHashKey putKey = IoHashKey.FromName("newReferenceUploadObject");
            HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespaceName}/bucket/{putKey}.uecb", requestContent);
            result.EnsureSuccessStatusCode();

            IBlobIndex? index = _server!.Services.GetService<IBlobIndex>();
            Assert.IsNotNull(index);
            IBlobIndex.BlobInfo? blobInfo = await index.GetBlobInfo(TestNamespaceName, objectHash);

            Assert.IsNotNull(blobInfo);
            Assert.IsTrue(blobInfo.Regions.Contains("test"));
            Assert.AreEqual(1, blobInfo.References.Count);

            (BucketId bucket, IoHashKey key) = blobInfo.References[0];
            Assert.AreEqual("bucket", bucket.ToString());
            Assert.AreEqual(putKey, key);
        }

        [TestMethod]
        public async Task DeleteBlob()
        {
            // upload a blob
            byte[] payload = Encoding.ASCII.GetBytes("I am a blob with contents");
            BlobIdentifier contentHash = BlobIdentifier.FromBlob(payload);

            {
                using ByteArrayContent requestContent = new ByteArrayContent(payload);
                requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
                HttpResponseMessage response = await _httpClient!.PutAsync(requestUri: $"api/v1/s/{TestNamespaceName}/{contentHash}", requestContent);
                response.EnsureSuccessStatusCode();
            }
            // verify its present in the blob index
            IBlobIndex? index = _server!.Services.GetService<IBlobIndex>();
            Assert.IsNotNull(index);
            IBlobIndex.BlobInfo? blobInfo = await index.GetBlobInfo(TestNamespaceName, contentHash);
            Assert.IsNotNull(blobInfo);

            // delete the blob
            {
                HttpResponseMessage response = await _httpClient!.DeleteAsync(requestUri: $"api/v1/s/{TestNamespaceName}/{contentHash}");
                response.EnsureSuccessStatusCode();
            }

            IBlobIndex.BlobInfo? deletedBlobInfo = await index.GetBlobInfo(TestNamespaceName, contentHash);
            Assert.IsNull(deletedBlobInfo);
        }

     
    }

    [TestClass]
    public class MemoryBlobIndexTests : BlobIndexTests
    {
        protected override IEnumerable<KeyValuePair<string, string>> GetSettings()
        {
            return new[] { new KeyValuePair<string, string>("Horde_Storage:BlobIndexImplementation", HordeStorageSettings.BlobIndexImplementations.Memory.ToString()) };
        }

        protected override Task Seed(IServiceProvider serverServices)
        {
            return Task.CompletedTask;
        }

        protected override Task Teardown(IServiceProvider serverServices)
        {
            return Task.CompletedTask;
        }
    }

    [TestClass]
    public class ScyllaBlobIndexTests : BlobIndexTests
    {
        protected override IEnumerable<KeyValuePair<string, string>> GetSettings()
        {
            return new[] { new KeyValuePair<string, string>("Horde_Storage:BlobIndexImplementation", HordeStorageSettings.BlobIndexImplementations.Scylla.ToString()) };
        }

        protected override Task Seed(IServiceProvider serverServices)
        {
            return Task.CompletedTask;
        }

        protected override async Task Teardown(IServiceProvider provider)
        {
            IScyllaSessionManager scyllaSessionManager = provider.GetService<IScyllaSessionManager>()!;

            ISession replicatedKeyspace = scyllaSessionManager.GetSessionForReplicatedKeyspace();
            await replicatedKeyspace.ExecuteAsync(new SimpleStatement("DROP TABLE IF EXISTS blob_index"));
        }
    }

    [TestClass]
    public class MongoBlobIndexTests : BlobIndexTests
    {
        protected override IEnumerable<KeyValuePair<string, string>> GetSettings()
        {
            return new[] { new KeyValuePair<string, string>("Horde_Storage:BlobIndexImplementation", HordeStorageSettings.BlobIndexImplementations.Mongo.ToString()) };
        }

        protected override Task Seed(IServiceProvider serverServices)
        {
            return Task.CompletedTask;
        }

        protected override async Task Teardown(IServiceProvider provider)
        {
            await Task.CompletedTask;
        }
    }
}
