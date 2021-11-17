// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Mime;
using System.Threading.Tasks;
using Horde.Storage.FunctionalTests.Storage;
using Horde.Storage.Implementation;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.AspNetCore.TestHost;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Serilog;
using Serilog.Core;

namespace Horde.Storage.FunctionalTests.CompressedBlobs
{
    [TestClass]
    public class ScyllaCompressedBlobTests : CompressedBlobTests
    {
        protected override IEnumerable<KeyValuePair<string, string>> GetSettings()
        {
            // Use the S3 storage backend so we can handle large blobs
            return new[]
            {
                new KeyValuePair<string, string>("Horde.Storage:StorageImplementations:0", HordeStorageSettings.StorageBackendImplementations.S3.ToString()),
                new KeyValuePair<string, string>("S3:BucketName", $"tests-{TestNamespace}")
            };
        }

        protected override async Task Seed(IServiceProvider provider)
        {
            await Task.CompletedTask;
        }

        protected override async Task Teardown()
        {
            await Task.CompletedTask;
        }
    }


    public abstract class CompressedBlobTests
    {
        protected TestServer? _server;
        protected HttpClient? _httpClient;

        protected const string TestNamespace = "test-namespace";

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
        [DataTestMethod]
        [DataRow("7835c353d7dc67e8a0531c88fbc75ddfda10dee4", "7835c353d7dc67e8a0531c88fbc75ddfda10dee4")]
        [DataRow("4958689fe783e02fb35b13c14b0c3d7beb91e50c", "4958689fe783e02fb35b13c14b0c3d7beb91e50c")]
        [DataRow("dce31eb416f3dcb4c8250ac545eda3930919d3ff", "dce31eb416f3dcb4c8250ac545eda3930919d3ff")]
        [DataRow("05d7c699a2668efdecbe48f10db0d621d736f449.uecomp", "05D7C699A2668EFDECBE48F10DB0D621D736F449")]
        [DataRow("dce31eb416f3dcb4c8250ac545eda3930919d3ff", "dce31eb416f3dcb4c8250ac545eda3930919d3ff")]
        [DataRow("UncompressedTexture_CAS_dea81b6c3b565bb5089695377c98ce0f1c13b0c3.udd", "DEA81B6C3B565BB5089695377C98CE0F1C13B0C3")]
        [DataRow("Oodle-f895ea954b37217270e88d8b728bd3c09152689c", "F895EA954B37217270E88D8B728BD3C09152689C")]
        [DataRow("Oodle-f0b9c675fe21951ca27699f9baab9f9f5040b202", "F0B9C675FE21951CA27699F9BAAB9F9F5040B202")]
        [DataRow("OodleTexture_CAS_dbda9040e75c4674fcec173f982fddf12b021e24.udd", "DBDA9040E75C4674FCEC173F982FDDF12B021E24")]
        public async Task PutPayloads(string payloadFilename, string uncompressedHash)
        {
            byte[] texturePayload = await File.ReadAllBytesAsync($"ContentId/Payloads/{payloadFilename}");
            BlobIdentifier compressedPayloadIdentifier = BlobIdentifier.FromBlob(texturePayload);
            BlobIdentifier uncompressedPayloadIdentifier = new BlobIdentifier(uncompressedHash);

            ByteArrayContent content = new ByteArrayContent(texturePayload);
            content.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
            HttpResponseMessage result = await _httpClient!.PutAsync($"api/v1/compressed-blobs/{TestNamespace}/{uncompressedPayloadIdentifier}", content);
            result.EnsureSuccessStatusCode();

            InsertResponse response = await result.Content.ReadAsAsync<InsertResponse>();
            Assert.AreNotEqual(compressedPayloadIdentifier, response.Identifier);
            Assert.AreEqual(uncompressedPayloadIdentifier, response.Identifier);
        }

        [TestMethod]
        public async Task PutGetComplexTexture()
        {
            byte[] texturePayload = await File.ReadAllBytesAsync("ContentId/Payloads/UncompressedTexture_CAS_dea81b6c3b565bb5089695377c98ce0f1c13b0c3.udd");
            BlobIdentifier compressedPayloadIdentifier = BlobIdentifier.FromBlob(texturePayload);
            BlobIdentifier uncompressedPayloadIdentifier = new BlobIdentifier("DEA81B6C3B565BB5089695377C98CE0F1C13B0C3");

            {
                ByteArrayContent content = new ByteArrayContent(texturePayload);
                content.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
                HttpResponseMessage result = await _httpClient!.PutAsync($"api/v1/compressed-blobs/{TestNamespace}/{uncompressedPayloadIdentifier}", content);
                result.EnsureSuccessStatusCode();

                InsertResponse response = await result.Content.ReadAsAsync<InsertResponse>();
                Assert.AreNotEqual(compressedPayloadIdentifier, response.Identifier);
                Assert.AreEqual(uncompressedPayloadIdentifier, response.Identifier);
            }


            {
                HttpResponseMessage result = await _httpClient!.GetAsync($"api/v1/compressed-blobs/{TestNamespace}/{uncompressedPayloadIdentifier}");
                result.EnsureSuccessStatusCode();

                byte[] blobContent = await result.Content.ReadAsByteArrayAsync();
                CollectionAssert.AreEqual(texturePayload, blobContent);
            }

            {
                // verify the compressed blob can be retrieved in the blob store
                HttpResponseMessage result = await _httpClient!.GetAsync($"api/v1/blobs/{TestNamespace}/{compressedPayloadIdentifier}");
                result.EnsureSuccessStatusCode();

                byte[] blobContent = await result.Content.ReadAsByteArrayAsync();
                CollectionAssert.AreEqual(texturePayload, blobContent);
            }

            {
                // the uncompressed payload should not be valid in the blob endpoint
                HttpResponseMessage result = await _httpClient!.GetAsync($"api/v1/blobs/{TestNamespace}/{uncompressedPayloadIdentifier}");
                Assert.AreEqual(HttpStatusCode.NotFound, result.StatusCode);
            }
        }
    }
}
