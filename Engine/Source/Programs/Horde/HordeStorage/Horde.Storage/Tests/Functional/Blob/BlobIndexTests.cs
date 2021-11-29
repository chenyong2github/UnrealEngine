// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Mime;
using System.Text;
using System.Threading.Tasks;
using Horde.Storage.Implementation.Blob;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Serilog;
using Serilog.Core;

namespace Horde.Storage.FunctionalTests.Storage
{
    public abstract class BlobIndexTests
    {
        protected TestServer? _server;
        protected HttpClient? _httpClient;

        protected readonly NamespaceId TestNamespaceName = new NamespaceId("testbucket");
        protected const string SmallFileContents = "Small file contents";
        protected const string AnotherFileContents = "Another file with contents";
        protected const string DeletableFileContents = "Delete Me";
        protected const string OldFileContents = "a old blob used for testing cutoff filtering";

        protected readonly BlobIdentifier _smallFileHash =
            BlobIdentifier.FromBlob(Encoding.ASCII.GetBytes(SmallFileContents));

        protected readonly BlobIdentifier _anotherFileHash =
            BlobIdentifier.FromBlob(Encoding.ASCII.GetBytes(AnotherFileContents));

        protected readonly BlobIdentifier _deleteFileHash =
            BlobIdentifier.FromBlob(Encoding.ASCII.GetBytes(DeletableFileContents));

        protected readonly BlobIdentifier _oldBlobFileHash =
            BlobIdentifier.FromBlob(Encoding.ASCII.GetBytes(OldFileContents));

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
        protected abstract Task Teardown();


        [TestCleanup]
        public async Task MyTeardown()
        {
            await Teardown();
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
            return new[] { new KeyValuePair<string, string>("Horde.Storage:BlobIndexImplementation", HordeStorageSettings.BlobIndexImplementations.Memory.ToString()) };
        }

        protected override Task Seed(IServiceProvider serverServices)
        {
            return Task.CompletedTask;
        }

        protected override Task Teardown()
        {
            return Task.CompletedTask;
        }
    }

    [TestClass]
    public class ScyllaBlobIndexTests : BlobIndexTests
    {
        protected override IEnumerable<KeyValuePair<string, string>> GetSettings()
        {
            return new[] { new KeyValuePair<string, string>("Horde.Storage:BlobIndexImplementation", HordeStorageSettings.BlobIndexImplementations.Scylla.ToString()) };
        }

        protected override Task Seed(IServiceProvider serverServices)
        {
            return Task.CompletedTask;
        }

        protected override Task Teardown()
        {
            return Task.CompletedTask;
        }
    }
}
