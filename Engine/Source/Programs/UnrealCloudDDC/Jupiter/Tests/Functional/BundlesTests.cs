// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Amazon.S3;
using Amazon.S3.Model;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using Jupiter.Controllers;
using Jupiter.Implementation;
using Jupiter.Implementation.Bundles;
using Microsoft.AspNetCore.TestHost;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Serilog;
using Serilog.Core;

namespace Jupiter.FunctionalTests.Storage;

[TestClass]
public class S3BundlesTests : BundlesTests
{
    private IAmazonS3? _s3;
    protected override IEnumerable<KeyValuePair<string, string>> GetSettings()
    {
        return new[]
        {
            new KeyValuePair<string, string>("UnrealCloudDDC:StorageImplementations:0", UnrealCloudDDCSettings.StorageBackendImplementations.S3.ToString()),
            new KeyValuePair<string, string>("S3:BucketName", $"tests-{TestNamespaceName}")
        };
    }

    protected override async Task Seed(IServiceProvider provider)
    {
        string s3BucketName = $"tests-{TestNamespaceName}";

        _s3 = provider.GetService<IAmazonS3>();
        Assert.IsNotNull(_s3);
        if (await _s3!.DoesS3BucketExistAsync(s3BucketName))
        {
            // if we have failed to run the cleanup for some reason we run it now
            await Teardown(provider);
        }

        await _s3.PutBucketAsync(s3BucketName);
        await _s3.PutObjectAsync(new PutObjectRequest { BucketName = s3BucketName, Key = BlobIdentifier.FromBlobLocator(SmallFileLocator).AsS3Key(), ContentBody = SmallFileContents });
    }

    protected override async Task Teardown(IServiceProvider provider)
    {
        string s3BucketName = $"tests-{TestNamespaceName}";
        ListObjectsResponse response = await _s3!.ListObjectsAsync(s3BucketName);
        List<KeyVersion> objectKeys = response.S3Objects.Select(o => new KeyVersion { Key = o.Key }).ToList();
        await _s3.DeleteObjectsAsync(new DeleteObjectsRequest { BucketName = s3BucketName, Objects = objectKeys });

        await _s3.DeleteBucketAsync(s3BucketName);
    }
}

public abstract class BundlesTests
{
    protected TestServer? Server { get; set; }
    protected NamespaceId TestNamespaceName { get; } = new NamespaceId("test-namespace-bundle");

    private HttpClient? _httpClient;

    protected const string SmallFileContents = "Small file contents";

    protected BlobLocator SmallFileLocator { get; } = BlobLocator.Create(HostId.Empty);

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
            .UseStartup<JupiterStartup>()
        );
        _httpClient = server.CreateClient();
        Server = server;

        // Seed storage
        await Seed(Server.Services);
    }

    protected abstract IEnumerable<KeyValuePair<string, string>> GetSettings();

    protected abstract Task Seed(IServiceProvider serverServices);
    protected abstract Task Teardown(IServiceProvider serverServices);

    [TestCleanup]
    public async Task MyTeardown()
    {
        await Teardown(Server!.Services);
    }

    [TestMethod]
    public async Task GetFile()
    {
        HttpResponseMessage result = await _httpClient!.GetAsync(new Uri($"api/v1/storage/{TestNamespaceName}/blobs/{SmallFileLocator.ToString().ToLower()}", UriKind.Relative));
        Assert.AreEqual(HttpStatusCode.Redirect, result.StatusCode);

        using HttpClient httpClient = new HttpClient();
        HttpResponseMessage redirectResult = await httpClient!.GetAsync(result.Headers.Location);
        redirectResult.EnsureSuccessStatusCode();
        string content = await redirectResult.Content.ReadAsStringAsync();
        Assert.AreEqual(SmallFileContents, content);
    }

    [TestMethod]
    public async Task PutSmallBlobDirectly()
    {
        byte[] payload = Encoding.ASCII.GetBytes("I am a small blob");
        using ByteArrayContent requestContent = new ByteArrayContent(payload);
        requestContent.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
        requestContent.Headers.ContentLength = payload.Length;

        using MultipartFormDataContent multipartContent = new MultipartFormDataContent();
        multipartContent.Add(requestContent, "file", "smallBlobContent.dat");

        HttpResponseMessage result = await _httpClient!.PostAsync(new Uri($"api/v1/storage/{TestNamespaceName}/blobs", UriKind.Relative), multipartContent);

        result.EnsureSuccessStatusCode();
    }

    [TestMethod]
    public async Task PutSmallBlobRedirect()
    {
        byte[] payload = Encoding.ASCII.GetBytes("I am also a small blob");

        HttpResponseMessage result = await _httpClient!.PostAsync(new Uri($"api/v1/storage/{TestNamespaceName}/blobs", UriKind.Relative), null);
        /*Assert.AreEqual(HttpStatusCode.Redirect, result.StatusCode);*/

        result.EnsureSuccessStatusCode();
        WriteBlobResponse? response = await result.Content.ReadFromJsonAsync<WriteBlobResponse>();
        Assert.IsNotNull(response);
        //Assert.IsTrue(response.SupportsRedirects);
        Assert.IsNotNull(response.UploadUrl);

        Uri redirectUri = response.UploadUrl;
        using ByteArrayContent requestContent = new ByteArrayContent(payload);

        using HttpClient httpClient = new HttpClient();
        HttpResponseMessage redirectResult = await httpClient!.PutAsync(redirectUri, requestContent);
        string s = await redirectResult.Content.ReadAsStringAsync();
        redirectResult.EnsureSuccessStatusCode();
    }

    /*[TestMethod]
    public async Task PutGetRef()
    {
        IoHash targetHash = IoHash.Compute(Encoding.ASCII.GetBytes(SmallFileContents));
        RefName refName = new RefName("this-is-a-ref");
        int exportIdx = 1;
        using JsonContent requestContent = JsonContent.Create(new WriteRefRequest { Blob = SmallFileLocator, ExportIdx = exportIdx, Hash = targetHash, });
        HttpResponseMessage result = await _httpClient!.PutAsync(new Uri($"api/v1/storage/{TestNamespaceName}/refs/{refName}", UriKind.Relative), requestContent);
        result.EnsureSuccessStatusCode();

        HttpResponseMessage getResult = await _httpClient!.GetAsync(new Uri($"api/v1/storage/{TestNamespaceName}/refs/{refName}", UriKind.Relative));
        result.EnsureSuccessStatusCode();
        ReadRefResponse? getResponse = await getResult.Content.ReadFromJsonAsync<ReadRefResponse>();
        Assert.IsNotNull(getResponse);

        Assert.AreEqual(targetHash, getResponse.Hash);
        Assert.AreEqual(SmallFileLocator, getResponse.Blob);
        Assert.AreEqual(0, getResponse.ExportIdx);
        Assert.AreEqual($"/api/v1/storage/test-namespace-bundle/nodes/{SmallFileLocator}?export={exportIdx}", getResponse.Link);
    }*/

    [TestMethod]
    public async Task PutGetBundle()
    {
        Bundle manualBundle = CreateBundleManually();
        byte[] payload = manualBundle.AsSequence().ToArray(); // TODO: create bundle
        using ByteArrayContent requestContent = new ByteArrayContent(payload);
        requestContent.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
        requestContent.Headers.ContentLength = payload.Length;

        using MultipartFormDataContent multipartContent = new MultipartFormDataContent();
        multipartContent.Add(requestContent, "file", "bundle.dat");

        HttpResponseMessage result = await _httpClient!.PostAsync(new Uri($"api/v1/storage/{TestNamespaceName}/blobs", UriKind.Relative), multipartContent);
        result.EnsureSuccessStatusCode();

        WriteBlobResponse? writeBlobResponse = await result.Content.ReadFromJsonAsync<WriteBlobResponse>();
        Assert.IsNotNull(writeBlobResponse);
        BlobLocator bundleLocator = writeBlobResponse.Blob;

        HttpResponseMessage getResult = await _httpClient!.GetAsync(new Uri($"api/v1/storage/{TestNamespaceName}/bundles/{bundleLocator}", UriKind.Relative));
        getResult.EnsureSuccessStatusCode();

        // TODO: validate result?
    }
    
    static Bundle CreateBundleManually()
    {
        ArrayMemoryWriter payloadWriter = new ArrayMemoryWriter(200);
        payloadWriter.WriteString("Hello world");
        byte[] payload = payloadWriter.WrittenMemory.ToArray();

        List<BundleType> types = new List<BundleType>();
        types.Add(new BundleType(Guid.Parse("F63606D4-5DBB-4061-A655-6F444F65229E"), 1));

        List<BundleExport> exports = new List<BundleExport>();
        exports.Add(new BundleExport(0, IoHash.Compute(payload), payload.Length, Array.Empty<int>()));

        List<BundlePacket> packets = new List<BundlePacket>();
        packets.Add(new BundlePacket(payload.Length, payload.Length));

        BundleHeader header = new BundleHeader(BundleCompressionFormat.None, types, Array.Empty<BundleImport>(), exports, packets);
        return new Bundle(header, new List<ReadOnlyMemory<byte>> { payload });
    }

}
