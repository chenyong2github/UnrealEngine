// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;
using Jupiter.Controllers;
using Jupiter.Implementation;
using Microsoft.AspNetCore.TestHost;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Serilog;
using Serilog.Core;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;

namespace Jupiter.FunctionalTests.GC
{
    [TestClass]
    public class MemoryGCReferencesTests : GCReferencesTests
    {
        protected override string GetImplementation()
        {
            return "Memory";
        }
    }

    [TestClass]
    public class ScyllaGCReferencesTests : GCReferencesTests
    {
        protected override string GetImplementation()
        {
            return "Scylla";
        }
    }

    public abstract class GCReferencesTests : IDisposable
    {
        private HttpClient? _httpClient;

        private readonly NamespaceId TestNamespace = new NamespaceId("test-namespace");
        private readonly BucketId DefaultBucket = new BucketId("default");

        private static readonly byte[] s_objectContents0 = Encoding.ASCII.GetBytes("blob_00");
        private static readonly byte[] s_objectContents1 = Encoding.ASCII.GetBytes("blob_11");
        private static readonly byte[] s_objectContents2 = Encoding.ASCII.GetBytes("blob_22");
        private static readonly byte[] s_objectContents3 = Encoding.ASCII.GetBytes("blob_33");
        private static readonly byte[] s_objectContents4 = Encoding.ASCII.GetBytes("blob_44");
        private static readonly byte[] s_objectContents5 = Encoding.ASCII.GetBytes("blob_55");
        private static readonly byte[] s_objectContents6 = Encoding.ASCII.GetBytes("blob_66");

        private readonly BlobIdentifier object0id = BlobIdentifier.FromBlob(s_objectContents0);
        private readonly BlobIdentifier object1id = BlobIdentifier.FromBlob(s_objectContents1);
        private readonly BlobIdentifier object2id = BlobIdentifier.FromBlob(s_objectContents2);
        private readonly BlobIdentifier object3id = BlobIdentifier.FromBlob(s_objectContents3);
        private readonly BlobIdentifier object4id = BlobIdentifier.FromBlob(s_objectContents4);
        private readonly BlobIdentifier object5id = BlobIdentifier.FromBlob(s_objectContents5);
        private readonly BlobIdentifier object6id = BlobIdentifier.FromBlob(s_objectContents6);

        private readonly IoHashKey object0Name = IoHashKey.FromName("object0");
        private readonly IoHashKey object1Name = IoHashKey.FromName("object1");
        private readonly IoHashKey object2Name = IoHashKey.FromName("object2");
        private readonly IoHashKey object3Name = IoHashKey.FromName("object3");
        private readonly IoHashKey object4Name = IoHashKey.FromName("object4");
        private readonly IoHashKey object5Name = IoHashKey.FromName("object5");
        private readonly IoHashKey object6Name = IoHashKey.FromName("object6");
        private TestServer? _server;

        [TestInitialize]
        public async Task Setup()
        {
            IConfigurationRoot configuration = new ConfigurationBuilder()
                // we are not reading the base appSettings here as we want exact control over what runs in the tests
                .AddJsonFile("appsettings.Testing.json", false)
                .AddEnvironmentVariables()
                .AddInMemoryCollection(new List<KeyValuePair<string, string>>()
                {
                    new KeyValuePair<string, string>("UnrealCloudDDC:StorageImplementations:0", "Memory"),
                    new KeyValuePair<string, string>("UnrealCloudDDC:BlobIndexImplementation", GetImplementation()),
                })
                .Build();
            Logger logger = new LoggerConfiguration()
                .ReadFrom.Configuration(configuration)
                .CreateLogger();

            _server = new TestServer(new WebHostBuilder()
                .UseConfiguration(configuration)
                .UseEnvironment("Testing")
                .UseSerilog(logger)
                .UseStartup<JupiterStartup>()
            );
            _httpClient = _server.CreateClient();

            IBlobService blobService = _server.Services.GetService<IBlobService>()!;
            await blobService.PutObject(TestNamespace, s_objectContents0, object0id);
            await blobService.PutObject(TestNamespace, s_objectContents1, object1id);
            await blobService.PutObject(TestNamespace, s_objectContents2, object2id);
            await blobService.PutObject(TestNamespace, s_objectContents3, object3id);
            await blobService.PutObject(TestNamespace, s_objectContents4, object4id);
            await blobService.PutObject(TestNamespace, s_objectContents5, object5id);
            await blobService.PutObject(TestNamespace, s_objectContents6, object6id);

            IObjectService? objectService = _server.Services.GetService<IObjectService>()!;
            Assert.IsNotNull(objectService);
            (BlobIdentifier ob0_hash, CbObject ob0_cb) = GetCBWithAttachment(object0id);
            await objectService.Put(TestNamespace, DefaultBucket, object0Name, ob0_hash, ob0_cb);
           
            (BlobIdentifier ob1_hash, CbObject ob1_cb) = GetCBWithAttachment(object1id);
            await objectService.Put(TestNamespace, DefaultBucket, object1Name, ob1_hash, ob1_cb);

            (BlobIdentifier ob2_hash, CbObject ob2_cb) = GetCBWithAttachment(object2id);
            await objectService.Put(TestNamespace, DefaultBucket, object2Name, ob2_hash, ob2_cb);

            (BlobIdentifier ob3_hash, CbObject ob3_cb) = GetCBWithAttachment(object3id);
            await objectService.Put(TestNamespace, DefaultBucket, object3Name, ob3_hash, ob3_cb);

            (BlobIdentifier ob4_hash, CbObject ob4_cb) = GetCBWithAttachment(object4id);
            await objectService.Put(TestNamespace, DefaultBucket, object4Name, ob4_hash, ob4_cb);

            (BlobIdentifier ob5_hash, CbObject ob5_cb) = GetCBWithAttachment(object5id);
            await objectService.Put(TestNamespace, DefaultBucket, object5Name, ob5_hash, ob5_cb);

            (BlobIdentifier ob6_hash, CbObject ob6_cb) = GetCBWithAttachment(object6id);
            await objectService.Put(TestNamespace, DefaultBucket, object6Name, ob6_hash, ob6_cb);

            IReferencesStore referenceStore = _server.Services.GetService<IReferencesStore>()!;
            DateTime oldTimestamp = DateTime.Now.AddDays(-30);
            DateTime newTimestamp = DateTime.Now;
            await referenceStore.UpdateLastAccessTime(TestNamespace, DefaultBucket, object0Name, oldTimestamp);
            await referenceStore.UpdateLastAccessTime(TestNamespace, DefaultBucket, object1Name, newTimestamp);
            await referenceStore.UpdateLastAccessTime(TestNamespace, DefaultBucket, object2Name, oldTimestamp);
            await referenceStore.UpdateLastAccessTime(TestNamespace, DefaultBucket, object3Name, oldTimestamp);
            await referenceStore.UpdateLastAccessTime(TestNamespace, DefaultBucket, object4Name, newTimestamp);
            await referenceStore.UpdateLastAccessTime(TestNamespace, DefaultBucket, object5Name, newTimestamp);
            await referenceStore.UpdateLastAccessTime(TestNamespace, DefaultBucket, object6Name, oldTimestamp);
        }

        protected abstract string GetImplementation();

        [TestMethod]
        public async Task RunRefCleanup()
        {
            // trigger the cleanup
            using StringContent content = new StringContent(string.Empty);
            HttpResponseMessage cleanupResponse = await _httpClient!.PostAsync(new Uri($"api/v1/admin/refCleanup/{TestNamespace}", UriKind.Relative), content);
            cleanupResponse.EnsureSuccessStatusCode();
            RemovedRefRecordsResponse removedRefRecords = await cleanupResponse.Content.ReadAsAsync<RemovedRefRecordsResponse>();
            Assert.AreEqual(4, removedRefRecords.CountOfRemovedRecords);

            IBlobService blobService = _server!.Services.GetService<IBlobService>()!;
            // some blobs should have been collected during the GC while others should remain
            Assert.IsFalse(await blobService.Exists(TestNamespace, object0id));
            Assert.IsTrue(await blobService.Exists(TestNamespace, object1id));
            Assert.IsFalse(await blobService.Exists(TestNamespace, object2id));
            Assert.IsFalse(await blobService.Exists(TestNamespace, object3id));
            Assert.IsTrue(await blobService.Exists(TestNamespace, object4id));
            Assert.IsTrue(await blobService.Exists(TestNamespace, object5id));
            Assert.IsFalse(await blobService.Exists(TestNamespace, object6id));
        }

        private static (BlobIdentifier, CbObject) GetCBWithAttachment(BlobIdentifier blobIdentifier)
        {
            CbWriter writer = new CbWriter();
            writer.BeginObject();
            writer.WriteBinaryAttachment("Attachment", blobIdentifier.AsIoHash());
            writer.EndObject();

            byte[] b = writer.ToByteArray();
            return (BlobIdentifier.FromBlob(b), new CbObject(b));
        }

        protected virtual void Dispose(bool disposing)
        {
            if (disposing)
            {
                _httpClient?.Dispose();
                _server?.Dispose();
            }
        }

        public void Dispose()
        {
            Dispose(true);
            System.GC.SuppressFinalize(this);
        }
    }
}
