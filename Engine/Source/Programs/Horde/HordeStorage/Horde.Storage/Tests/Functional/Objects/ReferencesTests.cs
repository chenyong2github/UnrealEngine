// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Mime;
using System.Text;
using System.Threading.Tasks;
using Cassandra;
using Dasync.Collections;
using EpicGames.Core;
using Horde.Storage.Controllers;
using Horde.Storage.Implementation;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.AspNetCore.TestHost;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using Serilog;
using Logger = Serilog.Core.Logger;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;

namespace Horde.Storage.FunctionalTests.References
{

    [TestClass]
    public class ScyllaReferencesTests : ReferencesTests
    {
        protected override IEnumerable<KeyValuePair<string, string>> GetSettings()
        {
            return new[]
            {
                new KeyValuePair<string, string>("Horde_Storage:ReferencesDbImplementation", HordeStorageSettings.ReferencesDbImplementations.Scylla.ToString()),
                new KeyValuePair<string, string>("Horde_Storage:ContentIdStoreImplementation", HordeStorageSettings.ContentIdStoreImplementations.Scylla.ToString()),
                new KeyValuePair<string, string>("Horde_Storage:ReplicationLogWriterImplementation", HordeStorageSettings.ReplicationLogWriterImplementations.Scylla.ToString()),
            };
        }

        protected override async Task SeedDb(IServiceProvider provider)
        {
            IReferencesStore referencesStore = provider.GetService<IReferencesStore>()!;
            //verify we are using the expected store
            Assert.IsTrue(referencesStore.GetType() == typeof(ScyllaReferencesStore));

            IContentIdStore contentIdStore = provider.GetService<IContentIdStore>()!;
            //verify we are using the expected store
            Assert.IsTrue(contentIdStore.GetType() == typeof(ScyllaContentIdStore));

            IReplicationLog replicationLog = provider.GetService<IReplicationLog>()!;
            //verify we are using the replication log writer
            Assert.IsTrue(replicationLog.GetType() == typeof(ScyllaReplicationLog));

            await SeedTestData(referencesStore);
        }

        protected override async Task TeardownDb(IServiceProvider provider)
        {
            IScyllaSessionManager scyllaSessionManager = provider.GetService<IScyllaSessionManager>()!;

            ISession replicatedKeyspace = scyllaSessionManager.GetSessionForReplicatedKeyspace();
            await replicatedKeyspace.ExecuteAsync(new SimpleStatement("DROP TABLE IF EXISTS objects"));
            await replicatedKeyspace.ExecuteAsync(new SimpleStatement("DROP TABLE IF EXISTS object_last_access"));

            // we need to clear out the state we modified in the replication log, otherwise the replication log tests will fail
            ISession localKeyspace = scyllaSessionManager.GetSessionForLocalKeyspace();
            await Task.WhenAll(
                // remove replication log table as we expect it to be empty when starting the tests
                localKeyspace.ExecuteAsync(new SimpleStatement("DROP TABLE IF EXISTS replication_log;")),
                // remove the snapshots
                localKeyspace.ExecuteAsync(new SimpleStatement("DROP TABLE IF EXISTS replication_snapshot;")),
                // remove the namespaces
                localKeyspace.ExecuteAsync(new SimpleStatement("DROP TABLE IF EXISTS replication_namespace;"))
            );

        }
    }

    [TestClass]
    public class MongoReferencesTests : ReferencesTests
    {
        protected override IEnumerable<KeyValuePair<string, string>> GetSettings()
        {
            return new[]
            {
                new KeyValuePair<string, string>("Horde_Storage:ReferencesDbImplementation", HordeStorageSettings.ReferencesDbImplementations.Mongo.ToString()),
                new KeyValuePair<string, string>("Horde_Storage:ContentIdStoreImplementation", HordeStorageSettings.ContentIdStoreImplementations.Mongo.ToString()),
                // we do not have a mongo version of the replication log, as the mongo deployment is only intended for single servers
                new KeyValuePair<string, string>("Horde_Storage:ReplicationLogWriterImplementation", HordeStorageSettings.ReplicationLogWriterImplementations.Memory.ToString()),
            };
        }

        protected override async Task SeedDb(IServiceProvider provider)
        {
            IReferencesStore referencesStore = provider.GetService<IReferencesStore>()!;
            //verify we are using the expected store
            Assert.IsTrue(referencesStore.GetType() == typeof(MongoReferencesStore));

            IContentIdStore contentIdStore = provider.GetService<IContentIdStore>()!;
            //verify we are using the expected store
            Assert.IsTrue(contentIdStore.GetType() == typeof(MongoContentIdStore));

            IReplicationLog replicationLog = provider.GetService<IReplicationLog>()!;
            //verify we are using the replication log writer
            Assert.IsTrue(replicationLog.GetType() == typeof(MemoryReplicationLog));

            await SeedTestData(referencesStore);
        }

        protected override async Task TeardownDb(IServiceProvider provider)
        {
            await Task.CompletedTask;
            }
    }

    [TestClass]
    public class MemoryReferencesTests : ReferencesTests
    {
        protected override IEnumerable<KeyValuePair<string, string>> GetSettings()
        {
            return new[]
            {
                new KeyValuePair<string, string>("Horde_Storage:ReferencesDbImplementation", HordeStorageSettings.ReferencesDbImplementations.Memory.ToString()),
                new KeyValuePair<string, string>("Horde_Storage:ContentIdStoreImplementation", HordeStorageSettings.ContentIdStoreImplementations.Memory.ToString()),
                new KeyValuePair<string, string>("Horde_Storage:ReplicationLogWriterImplementation", HordeStorageSettings.ReplicationLogWriterImplementations.Memory.ToString()),
            };
        }

        protected override async Task SeedDb(IServiceProvider provider)
        {
            IReferencesStore referencesStore = provider.GetService<IReferencesStore>()!;
            //verify we are using the expected refs store
            Assert.IsTrue(referencesStore.GetType() == typeof(MemoryReferencesStore));

            IContentIdStore contentIdStore = provider.GetService<IContentIdStore>()!;
            //verify we are using the expected store
            Assert.IsTrue(contentIdStore.GetType() == typeof(MemoryContentIdStore));

            IReplicationLog replicationLog = provider.GetService<IReplicationLog>()!;
            //verify we are using the replication log writer
            Assert.IsTrue(replicationLog.GetType() == typeof(MemoryReplicationLog));

            await SeedTestData(referencesStore);
        }

        protected override Task TeardownDb(IServiceProvider provider)
        {
            return Task.CompletedTask;
        }
    }
    
    public abstract class ReferencesTests
    {
        private static TestServer? _server;
        private static HttpClient? _httpClient;
        protected IBlobService _blobService = null!;
        protected IReferencesStore ReferencesStore = null!;

        protected readonly NamespaceId TestNamespace = new NamespaceId("test-namespace");

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

            _blobService = _server.Services.GetService<IBlobService>()!;
            ReferencesStore = _server.Services.GetService<IReferencesStore>()!;
            await SeedDb(server.Services);
        }

        protected async Task SeedTestData(IReferencesStore referencesStore)
        {
            await Task.CompletedTask;
        }


        [TestCleanup]
        public async Task Teardown()
        {
            if (_server != null) 
                await TeardownDb(_server.Services);
        }

        protected abstract IEnumerable<KeyValuePair<string, string>> GetSettings();

        protected abstract Task SeedDb(IServiceProvider provider);
        protected abstract Task TeardownDb(IServiceProvider provider);

        [TestMethod]
        public async Task PutGetBlob()
        {
            const string objectContents = "This is treated as a opaque blob";
            byte[] data = Encoding.ASCII.GetBytes(objectContents);
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(data);
            IoHashKey key = IoHashKey.FromName("newBlobObject");
            HttpContent requestContent = new ByteArrayContent(data);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
            requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

            HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}", requestContent);
            result.EnsureSuccessStatusCode();

            {
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();
                string roundTrippedPayload = Encoding.ASCII.GetString(roundTrippedBuffer);

                Assert.AreEqual(objectContents, roundTrippedPayload);
                CollectionAssert.AreEqual(data, roundTrippedBuffer);
                Assert.AreEqual(objectHash, BlobIdentifier.FromBlob(roundTrippedBuffer));
            }

            {
                BlobIdentifier attachment;
                {
                    HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.uecb");
                    getResponse.EnsureSuccessStatusCode();
                    await using MemoryStream ms = new MemoryStream();
                    await getResponse.Content.CopyToAsync(ms);

                    byte[] roundTrippedBuffer = ms.ToArray();
                    CbObject cb = new CbObject(roundTrippedBuffer);
                    List<CbField> fields = cb.ToList();

                    Assert.AreEqual(2, fields.Count);
                    CbField payloadField = fields[0];
                    Assert.IsNotNull(payloadField);
                    Assert.IsTrue(payloadField.IsBinaryAttachment());
                    attachment = BlobIdentifier.FromIoHash(payloadField.AsBinaryAttachment());
                }

                {
                    HttpResponseMessage getAttachment = await _httpClient.GetAsync($"api/v1/blobs/{TestNamespace}/{attachment}");
                    getAttachment.EnsureSuccessStatusCode();
                    await using MemoryStream ms = new MemoryStream();
                    await getAttachment.Content.CopyToAsync(ms);
                    byte[] roundTrippedBuffer = ms.ToArray();
                    string roundTrippedString = Encoding.ASCII.GetString(roundTrippedBuffer);

                    Assert.AreEqual(objectContents, roundTrippedString);
                    Assert.AreEqual(objectHash, BlobIdentifier.FromBlob(roundTrippedBuffer));
                }
            }

            {
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.json");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();
                string s = Encoding.ASCII.GetString(roundTrippedBuffer);
                JObject jObject = JObject.Parse(s);
                Assert.AreEqual(2, jObject.Children().Count());
                JToken? childToken = jObject.Children().First();
                Assert.IsNotNull(childToken);
                Assert.AreEqual(JTokenType.Property, childToken.Type);
                JProperty property = jObject.Children<JProperty>().First();
                Assert.IsNotNull(property);

                Assert.AreEqual("RawHash", property!.Name);

                string value = property.Value.Value<string>()!;
                Assert.IsNotNull(value);
                Assert.AreEqual(objectHash, new BlobIdentifier(value));
            }

            {
                // request the object as a json response using accept instead of the format filter
                var request = new HttpRequestMessage(HttpMethod.Get, $"api/v1/refs/{TestNamespace}/bucket/{key}");
                request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(MediaTypeNames.Application.Json));
                HttpResponseMessage getResponse = await _httpClient.SendAsync(request);
                getResponse.EnsureSuccessStatusCode();
                Assert.AreEqual(MediaTypeNames.Application.Json, getResponse.Content.Headers.ContentType?.MediaType);

                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();
                string s = Encoding.ASCII.GetString(roundTrippedBuffer);
                JObject jObject = JObject.Parse(s);
                Assert.AreEqual(2, jObject.Children().Count());
                JToken? childToken = jObject.Children().First();
                Assert.IsNotNull(childToken);
                Assert.AreEqual(JTokenType.Property, childToken.Type);
                JProperty property = jObject.Children<JProperty>().First();
                Assert.IsNotNull(property);

                Assert.AreEqual("RawHash", property!.Name);

                string? value = property.Value.Value<string>()!;
                Assert.IsNotNull(value);
                Assert.AreEqual(objectHash, new BlobIdentifier(value));
            }

            {
                // request the object as a jupiter inlined payload
                var request = new HttpRequestMessage(HttpMethod.Get, $"api/v1/refs/{TestNamespace}/bucket/{key}");
                request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.JupiterInlinedPayload));
                HttpResponseMessage getResponse = await _httpClient.SendAsync(request);
                getResponse.EnsureSuccessStatusCode();
                Assert.AreEqual(CustomMediaTypeNames.JupiterInlinedPayload, getResponse.Content.Headers.ContentType?.MediaType);

                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);
                byte[] roundTrippedBuffer = ms.ToArray();

                string roundTrippedString = Encoding.ASCII.GetString(roundTrippedBuffer);

                Assert.AreEqual(objectContents, roundTrippedString);
                Assert.AreEqual(objectHash, BlobIdentifier.FromBlob(roundTrippedBuffer));
            }
        }

        [TestMethod]
        public async Task PutGetCompactBinary()
        {
            CbWriter writer = new CbWriter();
            writer.BeginObject();
            writer.WriteString("stringField", "thisIsAField");
            writer.EndObject();

            byte[] objectData = writer.ToByteArray();
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(objectData);
            IoHashKey key = IoHashKey.FromName("newReferenceObject");

            HttpContent requestContent = new ByteArrayContent(objectData);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
            requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

            HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.uecb", requestContent);
            result.EnsureSuccessStatusCode();

            {
                Assert.AreEqual(CustomMediaTypeNames.UnrealCompactBinary, result!.Content.Headers.ContentType!.MediaType);
                // check that no blobs are missing
                await using MemoryStream ms = new MemoryStream();
                await result.Content.CopyToAsync(ms);
                byte[] roundTrippedBuffer = ms.ToArray();
                CbObject cb = new CbObject(roundTrippedBuffer);
                CbField needsField = cb["needs"];
                Assert.AreNotEqual(CbField.Empty, needsField);
                List<BlobIdentifier> missingBlobs = needsField.AsArray().Select(field => BlobIdentifier.FromIoHash(field.AsHash())).ToList();
                Assert.AreEqual(0, missingBlobs.Count);
            }

            {
                BucketId bucket = new BucketId("bucket");

                ObjectRecord objectRecord = await ReferencesStore.Get(TestNamespace, bucket, key, IReferencesStore.FieldFlags.IncludePayload);

                Assert.IsTrue(objectRecord.IsFinalized);
                Assert.AreEqual(key, objectRecord.Name);
                Assert.AreEqual(objectHash, objectRecord.BlobIdentifier);
                Assert.IsNotNull(objectRecord.InlinePayload);
            }

            {
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();

                CollectionAssert.AreEqual(objectData, roundTrippedBuffer);
                Assert.AreEqual(objectHash, BlobIdentifier.FromBlob(roundTrippedBuffer));
            }

            {
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.uecb");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();
                CbObject cb = new CbObject(roundTrippedBuffer);
                List<CbField> fields = cb.ToList();

                Assert.AreEqual(1, fields.Count);
                CbField stringField = fields[0];
                Assert.AreEqual("stringField", stringField.Name);
                Assert.AreEqual("thisIsAField", stringField.AsString());
            }


            {
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.json");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();

                string s = Encoding.ASCII.GetString(roundTrippedBuffer);
                JObject jObject = JObject.Parse(s);
                Assert.AreEqual(1, jObject.Children().Count());

                JToken? childToken = jObject.Children().First();
                Assert.IsNotNull(childToken);
                Assert.AreEqual(JTokenType.Property, childToken.Type);
                JProperty property = jObject.Children<JProperty>().First();
                Assert.IsNotNull(property);

                Assert.AreEqual("stringField", property!.Name);

                string? value = property.Value.Value<string>();
                Assert.IsNotNull(value);
                Assert.AreEqual("thisIsAField", value);
            }
        }

        
        [TestMethod]
        public async Task PutGetCompactBinaryHierarchy()
        {
            CbWriter childObjectWriter = new CbWriter();
            childObjectWriter.BeginObject();
            childObjectWriter.WriteString("stringField", "thisIsAField");
            childObjectWriter.EndObject();
            byte[] childObjectData = childObjectWriter.ToByteArray();
            BlobIdentifier childObjectHash = BlobIdentifier.FromBlob(childObjectData);

            CbWriter parentObjectWriter = new CbWriter();
            parentObjectWriter.BeginObject();
            parentObjectWriter.WriteObjectAttachment("childObject", childObjectHash.AsIoHash());
            parentObjectWriter.EndObject();
            byte[] parentObjectData = parentObjectWriter.ToByteArray();
            BlobIdentifier parentObjectHash = BlobIdentifier.FromBlob(parentObjectData);

            IoHashKey key = IoHashKey.FromName("newHierarchyObject");
            // this first upload should fail with the child object missing
            {
                HttpContent requestContent = new ByteArrayContent(parentObjectData);
                requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
                requestContent.Headers.Add(CommonHeaders.HashHeaderName, parentObjectHash.ToString());

                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.uecb", requestContent);
                result.EnsureSuccessStatusCode();

                Assert.AreEqual(CustomMediaTypeNames.UnrealCompactBinary, result!.Content.Headers.ContentType!.MediaType);
                // check that one blobs is missing
                await using MemoryStream ms = new MemoryStream();
                await result.Content.CopyToAsync(ms);
                byte[] roundTrippedBuffer = ms.ToArray();
                CbObject cb = new CbObject(roundTrippedBuffer);
                CbField needsField = cb["needs"];
                Assert.AreNotEqual(CbField.Empty, needsField);
                List<BlobIdentifier> missingBlobs = needsField.AsArray().Select(field => BlobIdentifier.FromIoHash(field.AsHash())).ToList();
                Assert.AreEqual(1, missingBlobs.Count);
                Assert.AreEqual(childObjectHash, missingBlobs[0]);
            }

            // upload the child object
            {
                HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Put, $"api/v1/objects/{TestNamespace}/{childObjectHash}");
                request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompactBinary));

                HttpContent requestContent = new ByteArrayContent(childObjectData);
                requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
                requestContent.Headers.Add(CommonHeaders.HashHeaderName, childObjectHash.ToString());
                request.Content = requestContent;

                HttpResponseMessage result = await _httpClient!.SendAsync(request);
                result.EnsureSuccessStatusCode();

                Assert.AreEqual(CustomMediaTypeNames.UnrealCompactBinary, result!.Content.Headers.ContentType!.MediaType);
                // check that one blobs is missing
                await using MemoryStream ms = new MemoryStream();
                await result.Content.CopyToAsync(ms);
                byte[] roundTrippedBuffer = ms.ToArray();
                CbObject cb = new CbObject(roundTrippedBuffer);
                CbField value = cb["identifier"];
                Assert.AreNotEqual(CbField.Empty, value);
                Assert.AreEqual(childObjectHash, BlobIdentifier.FromIoHash(value.AsHash()));
            }

            // since we have now uploaded the child object putting the object again should result in no missing references
            {
                HttpContent requestContent = new ByteArrayContent(parentObjectData);
                requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
                requestContent.Headers.Add(CommonHeaders.HashHeaderName, parentObjectHash.ToString());

                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.uecb", requestContent);
                result.EnsureSuccessStatusCode();

                Assert.AreEqual(result!.Content.Headers.ContentType!.MediaType, CustomMediaTypeNames.UnrealCompactBinary);
                // check that one blobs is missing
                await using MemoryStream ms = new MemoryStream();
                await result.Content.CopyToAsync(ms);
                byte[] roundTrippedBuffer = ms.ToArray();
                CbObject cb = new CbObject(roundTrippedBuffer);
                CbField needsField = cb["needs"];
                List<BlobIdentifier> missingBlobs = needsField.AsArray().Select(field => BlobIdentifier.FromIoHash(field.AsHash())).ToList();
                Assert.AreEqual(0, missingBlobs.Count);
            }

            {
                BucketId bucket = new BucketId("bucket");

                ObjectRecord objectRecord = await ReferencesStore.Get(TestNamespace, bucket, key, IReferencesStore.FieldFlags.IncludePayload);

                Assert.IsTrue(objectRecord.IsFinalized);
                Assert.AreEqual(key, objectRecord.Name);
                Assert.AreEqual(parentObjectHash, objectRecord.BlobIdentifier);
                Assert.IsNotNull(objectRecord.InlinePayload);
            }

            {
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();

                CollectionAssert.AreEqual(parentObjectData, roundTrippedBuffer);
                Assert.AreEqual(parentObjectHash, BlobIdentifier.FromBlob(roundTrippedBuffer));
            }

            {
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.uecb");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();
                CbObject cb = new CbObject(roundTrippedBuffer);
                List<CbField> fields = cb.ToList();

                Assert.AreEqual(1, fields.Count);
                CbField childObjectField = fields[0];
                Assert.AreEqual("childObject", childObjectField.Name);
                Assert.AreEqual(childObjectHash, BlobIdentifier.FromIoHash(childObjectField.AsHash()));
            }


            {
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.json");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();

                string s = Encoding.ASCII.GetString(roundTrippedBuffer);
                JObject jObject = JObject.Parse(s);
                Assert.AreEqual(1, jObject.Children().Count());

                JToken? childToken = jObject.Children().First();
                Assert.IsNotNull(childToken);
                Assert.AreEqual(JTokenType.Property, childToken.Type);
                JProperty property = jObject.Children<JProperty>().First();
                Assert.IsNotNull(property);

                Assert.AreEqual("childObject", property!.Name);

                string? value = property.Value.Value<string>();
                Assert.IsNotNull(value);
                Assert.AreEqual(childObjectHash.ToString().ToLower(), value);
            }

            {
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/objects/{TestNamespace}/{childObjectHash}");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();

                CollectionAssert.AreEqual(childObjectData, roundTrippedBuffer);
                Assert.AreEqual(childObjectHash, BlobIdentifier.FromBlob(roundTrippedBuffer));
            }

        }

        [TestMethod]
        public async Task ExistsChecks()
        {
            const string objectContents = "This is treated as a opaque blob";
            byte[] data = Encoding.ASCII.GetBytes(objectContents);
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(data);
            IoHashKey key = IoHashKey.FromName("newObject");
            HttpContent requestContent = new ByteArrayContent(data);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
            requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

            {
                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}", requestContent);
                result.EnsureSuccessStatusCode();
            }

            {
                HttpResponseMessage result = await _httpClient!.SendAsync(new HttpRequestMessage(HttpMethod.Head, $"api/v1/refs/{TestNamespace}/bucket/{key}"));
                result.EnsureSuccessStatusCode();
            }

            {
                HttpResponseMessage result = await _httpClient!.SendAsync(new HttpRequestMessage(HttpMethod.Head, $"api/v1/refs/{TestNamespace}/bucket/{IoHashKey.FromName("missingObject")}"));
                Assert.AreEqual(HttpStatusCode.NotFound, result.StatusCode);
            }
        }


        [TestMethod]
        public async Task PutGetObjectHierarchy()
        {
            string blobContents = "This is a string that is referenced as a blob";
            byte[] blobData = Encoding.ASCII.GetBytes(blobContents);
            BlobIdentifier blobHash = BlobIdentifier.FromBlob(blobData);
            await _blobService.PutObject(TestNamespace, blobData, blobHash);

            string blobContentsChild = "This string is also referenced as a blob but from a child object";
            byte[] dataChild = Encoding.ASCII.GetBytes(blobContentsChild);
            BlobIdentifier blobHashChild = BlobIdentifier.FromBlob(dataChild);
            await _blobService.PutObject(TestNamespace, dataChild, blobHashChild);

            CbWriter writerChild = new CbWriter();
            writerChild.BeginObject();
            writerChild.WriteBinaryAttachment("blob", blobHashChild.AsIoHash());
            writerChild.EndObject();

            byte[] childDataObject = writerChild.ToByteArray();
            BlobIdentifier childDataObjectHash = BlobIdentifier.FromBlob(childDataObject);
            await _blobService.PutObject(TestNamespace, childDataObject, childDataObjectHash);

            CbWriter writerParent = new CbWriter();
            writerParent.BeginObject();
            
            writerParent.WriteBinaryAttachment("blobAttachment", blobHash.AsIoHash());
            writerParent.WriteObjectAttachment("objectAttachment", childDataObjectHash.AsIoHash());
            writerParent.EndObject();

            byte[] objectData = writerParent.ToByteArray();
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(objectData);

            IoHashKey key = IoHashKey.FromName("newHierarchyObject");

            HttpContent requestContent = new ByteArrayContent(objectData);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
            requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

            HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.uecb", requestContent);
            result.EnsureSuccessStatusCode();

            // check the response
            {
                Assert.AreEqual(result!.Content.Headers.ContentType!.MediaType, CustomMediaTypeNames.UnrealCompactBinary);
                // check that no blobs are missing
                await using MemoryStream ms = new MemoryStream();
                await result.Content.CopyToAsync(ms);
                byte[] roundTrippedBuffer = ms.ToArray();
                CbObject cb = new CbObject(roundTrippedBuffer);
                CbField needsField = cb["needs"];
                List<BlobIdentifier> missingBlobs = needsField.AsArray().Select(field => BlobIdentifier.FromIoHash(field.AsHash())).ToList();
                Assert.AreEqual(0, missingBlobs.Count);
            }

            // check that actual internal representation
            {
                BucketId bucket = new BucketId("bucket");

                ObjectRecord objectRecord = await ReferencesStore.Get(TestNamespace, bucket, key, IReferencesStore.FieldFlags.IncludePayload);

                Assert.IsTrue(objectRecord.IsFinalized);
                Assert.AreEqual(key, objectRecord.Name);
                Assert.AreEqual(objectHash, objectRecord.BlobIdentifier);
                Assert.IsNotNull(objectRecord.InlinePayload);
            }

            // verify attachments
            {
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();

                CollectionAssert.AreEqual(objectData, roundTrippedBuffer);
                Assert.AreEqual(objectHash, BlobIdentifier.FromBlob(roundTrippedBuffer));
            }

            {
                BlobIdentifier blobAttachment;
                BlobIdentifier objectAttachment;
                {
                    HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.uecb");
                    getResponse.EnsureSuccessStatusCode();
                    await using MemoryStream ms = new MemoryStream();
                    await getResponse.Content.CopyToAsync(ms);

                    byte[] roundTrippedBuffer = ms.ToArray();
                    ReadOnlyMemory<byte> localMemory = new ReadOnlyMemory<byte>(roundTrippedBuffer);
                    CbObject cb = new CbObject(roundTrippedBuffer);
                    Assert.AreEqual(2, cb.Count());

                    CbField blobAttachmentField = cb["blobAttachment"];
                    Assert.AreNotEqual(CbField.Empty, blobAttachmentField);
                    blobAttachment = BlobIdentifier.FromIoHash(blobAttachmentField.AsBinaryAttachment());
                    CbField objectAttachmentField = cb["objectAttachment"];
                    Assert.AreNotEqual(CbField.Empty, objectAttachmentField);
                    objectAttachment = BlobIdentifier.FromIoHash(objectAttachmentField.AsObjectAttachment().Hash);
                }

                {
                    HttpResponseMessage getAttachment = await _httpClient.GetAsync($"api/v1/blobs/{TestNamespace}/{blobAttachment}");
                    getAttachment.EnsureSuccessStatusCode();
                    await using MemoryStream ms = new MemoryStream();
                    await getAttachment.Content.CopyToAsync(ms);
                    byte[] roundTrippedBuffer = ms.ToArray();
                    string roundTrippedString = Encoding.ASCII.GetString(roundTrippedBuffer);

                    Assert.AreEqual(blobContents, roundTrippedString);
                    Assert.AreEqual(blobHash, BlobIdentifier.FromBlob(roundTrippedBuffer));
                }

                BlobIdentifier attachedBlobIdentifier;
                {
                    HttpResponseMessage getAttachment = await _httpClient.GetAsync($"api/v1/blobs/{TestNamespace}/{objectAttachment}");
                    getAttachment.EnsureSuccessStatusCode();
                    await using MemoryStream ms = new MemoryStream();
                    await getAttachment.Content.CopyToAsync(ms);
                    byte[] roundTrippedBuffer = ms.ToArray();
                    ReadOnlyMemory<byte> localMemory = new ReadOnlyMemory<byte>(roundTrippedBuffer);
                    CbObject cb = new CbObject(roundTrippedBuffer);
                    Assert.AreEqual(1, cb.Count());

                    CbField blobField = cb["blob"];
                    Assert.AreNotEqual(CbField.Empty, blobField);

                    attachedBlobIdentifier = BlobIdentifier.FromIoHash(blobField!.AsBinaryAttachment());
                }

                {
                    HttpResponseMessage getAttachment = await _httpClient.GetAsync($"api/v1/blobs/{TestNamespace}/{attachedBlobIdentifier}");
                    getAttachment.EnsureSuccessStatusCode();
                    await using MemoryStream ms = new MemoryStream();
                    await getAttachment.Content.CopyToAsync(ms);
                    byte[] roundTrippedBuffer = ms.ToArray();
                    string roundTrippedString = Encoding.ASCII.GetString(roundTrippedBuffer);

                    Assert.AreEqual(blobContentsChild, roundTrippedString);
                    Assert.AreEqual(blobHashChild, BlobIdentifier.FromBlob(roundTrippedBuffer));
                }
            }

            // check json representation
            {
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.json");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();
                string s = Encoding.ASCII.GetString(roundTrippedBuffer);
                JObject jObject = JObject.Parse(s);
                Assert.AreEqual(2, jObject.Children().Count());

                JToken? blobAttachment = jObject["blobAttachment"];
                string blobAttachmentString = blobAttachment!.Value<string>()!;
                Assert.AreEqual(blobHash, new BlobIdentifier(blobAttachmentString));

                JToken? objectAttachment = jObject["objectAttachment"];
                string objectAttachmentString = objectAttachment!.Value<string>()!;
                Assert.AreEqual(childDataObjectHash, new BlobIdentifier(objectAttachmentString));

            }
        }


        
        [TestMethod]
        public async Task PutPartialHierarchy()
        {
            // do not submit the content of the blobs, which should be reported in the response of the put
            string blobContents = "This is a string that is referenced as a blob";
            byte[] blobData = Encoding.ASCII.GetBytes(blobContents);
            BlobIdentifier blobHash = BlobIdentifier.FromBlob(blobData);

            string blobContentsChild = "This string is also referenced as a blob but from a child object";
            byte[] dataChild = Encoding.ASCII.GetBytes(blobContentsChild);
            BlobIdentifier blobHashChild = BlobIdentifier.FromBlob(dataChild);

            CbWriter writerChild = new CbWriter();
            writerChild.BeginObject();
            writerChild.WriteBinaryAttachment("blob", blobHashChild.AsIoHash());
            writerChild.EndObject();

            byte[] childDataObject = writerChild.ToByteArray();
            BlobIdentifier childDataObjectHash = BlobIdentifier.FromBlob(childDataObject);
            await _blobService.PutObject(TestNamespace, childDataObject, childDataObjectHash);

            CbWriter writerParent = new CbWriter();
            writerParent.BeginObject();
            
            writerParent.WriteBinaryAttachment("blobAttachment", blobHash.AsIoHash());
            writerParent.WriteObjectAttachment("objectAttachment", childDataObjectHash.AsIoHash());
            writerParent.EndObject();

            byte[] objectData = writerParent.ToByteArray();
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(objectData);
            
            IoHashKey key = IoHashKey.FromName("newHierarchyObject");

            {
                HttpContent requestContent = new ByteArrayContent(objectData);
                requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
                requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.uecb", requestContent);
                result.EnsureSuccessStatusCode();

                {
                    Assert.AreEqual(result!.Content.Headers.ContentType!.MediaType, CustomMediaTypeNames.UnrealCompactBinary);
                    await using MemoryStream ms = new MemoryStream();
                    await result.Content.CopyToAsync(ms);
                    byte[] roundTrippedBuffer = ms.ToArray();
                    ReadOnlyMemory<byte> localMemory = new ReadOnlyMemory<byte>(roundTrippedBuffer);
                    CbObject cb = new CbObject(roundTrippedBuffer);
                    CbField needsField = cb["needs"];
                    List<BlobIdentifier> missingBlobs = needsField.AsArray().Select(field => BlobIdentifier.FromIoHash(field.AsHash())).ToList();
                    Assert.AreEqual(2, missingBlobs.Count);
                    Assert.IsTrue(missingBlobs.Contains(blobHash));
                    Assert.IsTrue(missingBlobs.Contains(blobHashChild));
                }
            }

            {
                HttpContent requestContent = new ByteArrayContent(objectData);
                requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
                requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.json", requestContent);
                result.EnsureSuccessStatusCode();

                {
                    Assert.AreEqual(result!.Content.Headers.ContentType!.MediaType, MediaTypeNames.Application.Json);
                    await using MemoryStream ms = new MemoryStream();
                    await result.Content.CopyToAsync(ms);
                    byte[] roundTrippedBuffer = ms.ToArray();
                    string s = Encoding.ASCII.GetString(roundTrippedBuffer);
                    JObject jObject = JObject.Parse(s);
                    Assert.AreEqual(1, jObject.Children().Count());

                    JToken? needs = jObject["needs"];
                    BlobIdentifier[] missingBlobs = needs!.ToArray().Select(token => new BlobIdentifier(token.Value<string>()!)).ToArray();
                    Assert.AreEqual(2, missingBlobs.Length);
                    Assert.IsTrue(missingBlobs.Contains(blobHash));
                    Assert.IsTrue(missingBlobs.Contains(blobHashChild));
                }
            }
        }

        
        [TestMethod]
        public async Task PutContentIdMissingBlob()
        {
            IContentIdStore? contentIdStore = _server!.Services.GetService<IContentIdStore>();
            Assert.IsNotNull(contentIdStore);

            // submit a object which contains a content id, which exists but points to a blob that does not exist
            string blobContents = "This is a string that is referenced as a blob";
            byte[] blobData = Encoding.ASCII.GetBytes(blobContents);
            BlobIdentifier blobHash = BlobIdentifier.FromBlob(blobData);
            ContentId contentId = new ContentId("0000000000000000000000000000000000000000");

            await contentIdStore.Put(TestNamespace, contentId, blobHash, blobData.Length);

            CbWriter writer = new CbWriter();
            writer.BeginObject();
            writer.WriteBinaryAttachment("blob", contentId.AsIoHash());
            writer.EndObject();

            byte[] objectData = writer.ToByteArray();
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(objectData);
            
            IoHashKey key = IoHashKey.FromName("putContentIdMissingBlob");

            {
                HttpContent requestContent = new ByteArrayContent(objectData);
                requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
                requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.uecb", requestContent);
                result.EnsureSuccessStatusCode();

                {
                    Assert.AreEqual(result!.Content.Headers.ContentType!.MediaType, CustomMediaTypeNames.UnrealCompactBinary);
                    await using MemoryStream ms = new MemoryStream();
                    await result.Content.CopyToAsync(ms);
                    byte[] roundTrippedBuffer = ms.ToArray();
                    CbObject cb = new CbObject(roundTrippedBuffer);
                    CbField needsField = cb["needs"];
                    Assert.AreNotEqual(CbField.Empty, needsField);
                    List<IoHash> missingBlobs = needsField.AsArray().Select(field => field.AsHash()).ToList();
                    Assert.AreEqual(1, missingBlobs.Count);

                    Assert.AreNotEqual(blobHash, missingBlobs[0], "Refs should not be returning the mapped blob identifiers as this is unknown to the client attempting to put a new ref");
                    Assert.AreEqual(contentId.AsBlobIdentifier(), BlobIdentifier.FromIoHash(missingBlobs[0]));
                }
            }

            {
                HttpContent requestContent = new ByteArrayContent(objectData);
                requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
                requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.json", requestContent);
                result.EnsureSuccessStatusCode();

                {
                    Assert.AreEqual(result!.Content.Headers.ContentType!.MediaType, MediaTypeNames.Application.Json);
                    await using MemoryStream ms = new MemoryStream();
                    await result.Content.CopyToAsync(ms);
                    byte[] roundTrippedBuffer = ms.ToArray();
                    string s = Encoding.ASCII.GetString(roundTrippedBuffer);
                    JObject jObject = JObject.Parse(s);
                    Assert.AreEqual(1, jObject.Children().Count());

                    JToken? needs = jObject["needs"];
                    BlobIdentifier[] missingBlobs = needs!.ToArray().Select(token => new BlobIdentifier(token.Value<string>()!)).ToArray();
                    Assert.AreEqual(1, missingBlobs.Length);
                    Assert.AreEqual(contentId.AsBlobIdentifier(), missingBlobs[0]);
                }
            }
        }

        [TestMethod]
        public async Task PutAndFinalize()
        {
            BucketId bucket = new BucketId("bucket");
            IoHashKey key = IoHashKey.FromName("willFinalizeObject");

            // do not submit the content of the blobs, which should be reported in the response of the put
            string blobContents = "This is a string that is referenced as a blob";
            byte[] blobData = Encoding.ASCII.GetBytes(blobContents);
            BlobIdentifier blobHash = BlobIdentifier.FromBlob(blobData);

            string blobContentsChild = "This string is also referenced as a blob but from a child object";
            byte[] dataChild = Encoding.ASCII.GetBytes(blobContentsChild);
            BlobIdentifier blobHashChild = BlobIdentifier.FromBlob(dataChild);

            CbWriter writerChild = new CbWriter();
            writerChild.BeginObject();
            writerChild.WriteBinaryAttachment("blob", blobHashChild.AsIoHash());
            writerChild.EndObject();

            byte[] childDataObject = writerChild.ToByteArray();
            BlobIdentifier childDataObjectHash = BlobIdentifier.FromBlob(childDataObject);
            await _blobService.PutObject(TestNamespace, childDataObject, childDataObjectHash);

            CbWriter writerParent = new CbWriter();
            writerParent.BeginObject();
            
            writerParent.WriteBinaryAttachment("blobAttachment", blobHash.AsIoHash());
            writerParent.WriteObjectAttachment("objectAttachment", childDataObjectHash.AsIoHash());
            writerParent.EndObject();

            byte[] objectData = writerParent.ToByteArray();
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(objectData);

            {
                HttpContent requestContent = new ByteArrayContent(objectData);
                requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
                requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.uecb", requestContent);
                result.EnsureSuccessStatusCode();

                {
                    Assert.AreEqual(result!.Content.Headers.ContentType!.MediaType, CustomMediaTypeNames.UnrealCompactBinary);
                    await using MemoryStream ms = new MemoryStream();
                    await result.Content.CopyToAsync(ms);
                    byte[] roundTrippedBuffer = ms.ToArray();
                    CbObject cb = new CbObject(roundTrippedBuffer);
                    CbField needsField = cb["needs"];
                    List<BlobIdentifier> missingBlobs = needsField.AsArray().Select(field => BlobIdentifier.FromIoHash(field.AsHash())).ToList();
                    Assert.AreEqual(2, missingBlobs.Count);
                    Assert.IsTrue(missingBlobs.Contains(blobHash));
                    Assert.IsTrue(missingBlobs.Contains(blobHashChild));
                }
            }

            // check that actual internal representation
            {
                ObjectRecord objectRecord = await ReferencesStore.Get(TestNamespace, bucket, key, IReferencesStore.FieldFlags.IncludePayload);

                Assert.IsFalse(objectRecord.IsFinalized);
                Assert.AreEqual(key, objectRecord.Name);
                Assert.AreEqual(objectHash, objectRecord.BlobIdentifier);
                Assert.IsNotNull(objectRecord.InlinePayload);
            }

            // upload missing pieces
            {
                await _blobService.PutObject(TestNamespace, blobData, blobHash);
                await _blobService.PutObject(TestNamespace, dataChild, blobHashChild);
            }

            // finalize the object as no pieces is now missing
            {
                HttpContent requestContent = new ByteArrayContent(Array.Empty<byte>());

                HttpResponseMessage result = await _httpClient!.PostAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}/finalize/{objectHash}.uecb", requestContent);
                result.EnsureSuccessStatusCode();

                {
                    Assert.AreEqual(result!.Content.Headers.ContentType!.MediaType, CustomMediaTypeNames.UnrealCompactBinary);
                    await using MemoryStream ms = new MemoryStream();
                    await result.Content.CopyToAsync(ms);
                    byte[] roundTrippedBuffer = ms.ToArray();
                    ReadOnlyMemory<byte> localMemory = new ReadOnlyMemory<byte>(roundTrippedBuffer);
                    CbObject cb = new CbObject(roundTrippedBuffer);
                    CbField? needsField = cb["needs"];
                    List<BlobIdentifier> missingBlobs = needsField.AsArray().Select(field => BlobIdentifier.FromIoHash(field.AsHash())).ToList();
                    Assert.AreEqual(0, missingBlobs.Count);
                }
            }

            // check that actual internal representation has updated its state
            {
                ObjectRecord objectRecord = await ReferencesStore.Get(TestNamespace, bucket, key, IReferencesStore.FieldFlags.None);

                Assert.IsTrue(objectRecord.IsFinalized);
                Assert.AreEqual(key, objectRecord.Name);
                Assert.AreEqual(objectHash, objectRecord.BlobIdentifier);
            }
        }

        [TestMethod]
        public async Task GetMissingBlobRecord()
        {
            string blobContents = "This is a blob";
            byte[] blobData = Encoding.ASCII.GetBytes(blobContents);
            BlobIdentifier blobHash = BlobIdentifier.FromBlob(blobData);
            IoHashKey key = IoHashKey.FromName("newReferenceObject");

            HttpContent requestContent = new ByteArrayContent(blobData);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
            requestContent.Headers.Add(CommonHeaders.HashHeaderName, blobHash.ToString());

            HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.uecb", requestContent);
            result.EnsureSuccessStatusCode();

            {
                Assert.AreEqual(CustomMediaTypeNames.UnrealCompactBinary, result!.Content.Headers.ContentType!.MediaType);
                // check that no blobs are missing
                await using MemoryStream ms = new MemoryStream();
                await result.Content.CopyToAsync(ms);
                byte[] roundTrippedBuffer = ms.ToArray();
                CbObject cb = new CbObject(roundTrippedBuffer);
                CbField needsField = cb["needs"];
                List<BlobIdentifier> missingBlobs = needsField.AsArray().Select(field => BlobIdentifier.FromIoHash(field.AsHash())).ToList();
                Assert.AreEqual(0, missingBlobs.Count);
            }

            {
                // verify we can fetch the blob properly
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                getResponse.EnsureSuccessStatusCode();
                await using MemoryStream ms = new MemoryStream();
                await getResponse.Content.CopyToAsync(ms);

                byte[] roundTrippedBuffer = ms.ToArray();

                CollectionAssert.AreEqual(blobData, roundTrippedBuffer);
                Assert.AreEqual(blobHash, BlobIdentifier.FromBlob(roundTrippedBuffer));
            }

            {
                // delete the blob 
                await _blobService.DeleteObject(TestNamespace, blobHash);
            }

            {
                // the compact binary object still exists, so this returns a success (trying to resolve the attachment will fail)
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.uecb");
                getResponse.EnsureSuccessStatusCode();
            }

            {
                // the compact binary object still exists, so this returns a success (trying to resolve the attachment will fail)
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.json");
                getResponse.EnsureSuccessStatusCode();
            }

            {
                // we should now see a 404 as the blob is missing
                HttpResponseMessage getResponse = await _httpClient.GetAsync($"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                Assert.AreEqual(HttpStatusCode.NotFound, getResponse.StatusCode);
                Assert.AreEqual("application/problem+json", getResponse.Content.Headers.ContentType!.MediaType);
                string s = await getResponse.Content.ReadAsStringAsync();
                ProblemDetails? problem = JsonConvert.DeserializeObject<ProblemDetails>(s);
                Assert.IsNotNull(problem);
                Assert.AreEqual($"Object {blobHash} in {TestNamespace} not found", problem.Title);
            }
        }
        
        
        [TestMethod]
        public async Task DeleteObject()
        {
            const string objectContents = "This is treated as a opaque blob";
            byte[] data = Encoding.ASCII.GetBytes(objectContents);
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(data);

            HttpContent requestContent = new ByteArrayContent(data);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
            requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

            IoHashKey key = IoHashKey.FromName("deletableObject");
            // submit the object
            {
                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}", requestContent);
                result.EnsureSuccessStatusCode();
            }

            // verify it is present
            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                result.EnsureSuccessStatusCode();
            }

            // delete the object
            {
                HttpResponseMessage result = await _httpClient!.DeleteAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}");
                result.EnsureSuccessStatusCode();
            }

            // ensure the object is not present anymore
            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                Assert.AreEqual(HttpStatusCode.NotFound, result.StatusCode);
            }
        }

        
        [TestMethod]
        public async Task DropBucket()
        {
            const string BucketToDelete = "delete-bucket";

            const string objectContents = "This is treated as a opaque blob";
            byte[] data = Encoding.ASCII.GetBytes(objectContents);
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(data);

            HttpContent requestContent = new ByteArrayContent(data);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
            requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

            IoHashKey key = IoHashKey.FromName("deletableObject");

            // submit the object into multiple buckets
            {
                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/{BucketToDelete}/{key}", requestContent);
                result.EnsureSuccessStatusCode();
            }

            {
                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}", requestContent);
                result.EnsureSuccessStatusCode();
            }

            // verify it is present
            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                result.EnsureSuccessStatusCode();
            }

            // verify it is present
            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs/{TestNamespace}/{BucketToDelete}/{key}.raw");
                result.EnsureSuccessStatusCode();
            }

            // delete the bucket
            {
                HttpResponseMessage result = await _httpClient!.DeleteAsync(requestUri: $"api/v1/refs/{TestNamespace}/{BucketToDelete}");
                result.EnsureSuccessStatusCode();
            }

            // ensure the object is present in not deleted bucket
            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                result.EnsureSuccessStatusCode();
            }

            // ensure the object is not present anymore
            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs/{TestNamespace}/{BucketToDelete}/{key}.raw");
                Assert.AreEqual(HttpStatusCode.NotFound, result.StatusCode);
            }
        }

        
        [TestMethod]
        public async Task DeleteNamespace()
        {
            const string NamespaceToBeDeleted = "test-delete-namespace";

            const string objectContents = "This is treated as a opaque blob";
            byte[] data = Encoding.ASCII.GetBytes(objectContents);
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(data);

            HttpContent requestContent = new ByteArrayContent(data);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
            requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

            IoHashKey key = IoHashKey.FromName("deletableObject");
            // submit the object into multiple namespaces
            {
                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}", requestContent);
                result.EnsureSuccessStatusCode();
            }

            {
                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{NamespaceToBeDeleted}/bucket/{key}", requestContent);
                result.EnsureSuccessStatusCode();
            }

            // verify it is present
            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                result.EnsureSuccessStatusCode();
            }

            // verify it is present
            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs/{NamespaceToBeDeleted}/bucket/{key}.raw");
                result.EnsureSuccessStatusCode();
            }

            // delete the namespace
            {
                HttpResponseMessage result = await _httpClient!.DeleteAsync(requestUri: $"api/v1/refs/{NamespaceToBeDeleted}");
                result.EnsureSuccessStatusCode();
            }

            // ensure the object is present in not deleted namespace
            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}.raw");
                result.EnsureSuccessStatusCode();
            }

            // ensure the object is not present anymore
            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs/{NamespaceToBeDeleted}/bucket/{key}.raw");
                Assert.AreEqual(HttpStatusCode.NotFound, result.StatusCode);
            }

            // make sure the namespace is not considered a valid namespace anymore
            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs");
                result.EnsureSuccessStatusCode();
                Assert.AreEqual(result!.Content.Headers.ContentType!.MediaType, MediaTypeNames.Application.Json);
                GetNamespacesResponse response = await result.Content.ReadAsAsync<GetNamespacesResponse>();
                CollectionAssert.DoesNotContain(response.Namespaces, NamespaceToBeDeleted);
            }
        }


        
        [TestMethod]
        public async Task ListNamespaces()
        {
            const string objectContents = "This is treated as a opaque blob";
            byte[] data = Encoding.ASCII.GetBytes(objectContents);
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(data);
            IoHashKey key = IoHashKey.FromName("notUsedObject");

            HttpContent requestContent = new ByteArrayContent(data);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
            requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

            // submit a object to make sure a namespace is created
            {
                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}", requestContent);
                result.EnsureSuccessStatusCode();
            }

            {
                HttpResponseMessage result = await _httpClient!.GetAsync(requestUri: $"api/v1/refs");
                result.EnsureSuccessStatusCode();
                Assert.AreEqual(result!.Content.Headers.ContentType!.MediaType, MediaTypeNames.Application.Json);
                GetNamespacesResponse response = await result.Content.ReadAsAsync<GetNamespacesResponse>();
                Assert.IsTrue(response.Namespaces.Contains(TestNamespace));
            }
        }

        
        [TestMethod]
        public async Task GetOldRecords()
        {
            const string objectContents = "This is treated as a opaque blob";
            byte[] data = Encoding.ASCII.GetBytes(objectContents);
            BlobIdentifier objectHash = BlobIdentifier.FromBlob(data);

            HttpContent requestContent = new ByteArrayContent(data);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
            requestContent.Headers.Add(CommonHeaders.HashHeaderName, objectHash.ToString());

            IoHashKey key = IoHashKey.FromName("oldRecord");
            // submit some contents
            {
                HttpResponseMessage result = await _httpClient!.PutAsync(requestUri: $"api/v1/refs/{TestNamespace}/bucket/{key}", requestContent);
                result.EnsureSuccessStatusCode();
            }

            List<(BucketId, IoHashKey, DateTime)> records = await ReferencesStore.GetRecords(TestNamespace).ToListAsync();

            (BucketId, IoHashKey, DateTime)? oldRecord = records.Find(record => record.Item2 == key);
            Assert.IsNotNull(oldRecord);
            Assert.AreEqual(key, oldRecord.Value.Item2);
            Assert.AreEqual("bucket", oldRecord.Value.Item1.ToString());
        }
    }
}
