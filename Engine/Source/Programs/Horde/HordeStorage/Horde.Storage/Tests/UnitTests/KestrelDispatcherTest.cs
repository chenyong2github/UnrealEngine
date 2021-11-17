// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipelines;
using System.Text;
using System.Threading.Tasks;
using Horde.Storage.Implementation;
using Horde.Storage.Implementation.Kestrel;
using Jupiter.Implementation;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

namespace Horde.Storage.UnitTests
{
    [TestClass]
    public class KestrelDispatcherTest
    {
        private class HttpResponse
        {
            public readonly int StatusCode;
            public readonly Dictionary<string, string> Headers = new ();
            public readonly byte[] Body;

            public HttpResponse(int statusCode, byte[] body)
            {
                StatusCode = statusCode;
                Body = body;
            }

            public string GetBodyAsString()
            {
                return Encoding.ASCII.GetString(Body);
            }
        }

        public Mock<IDDCRefService> GetDdcRefServiceMock()
        {
            Mock<IDDCRefService> mock = new () { DefaultValue = DefaultValue.Empty };

            NamespaceId ns = new NamespaceId("test.ddc");
            BucketId bucket = new BucketId("default");
            ContentHash contentHash1 = new ("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");
            RefResponse refResponse = new RefResponse("bogusName", DateTime.Now, contentHash1, new BlobIdentifier[0], null);
            byte[] data = new AsciiString("testing");
            MemoryStream ms = new (data);
            BlobContents blobContents = new BlobContents(ms, data.Length);
            var returnGetSuccess = (refResponse, blobContents);
            mock.Setup(s => s.Get(ns, bucket, new KeyId("somekey"), It.IsAny<string[]>())).ReturnsAsync(returnGetSuccess).Verifiable();

            ContentHash contentHash2 = new ("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
            RefRecord returnExistsSuccess = new (ns, bucket, new KeyId("BLARGH"), new BlobIdentifier[0], null, contentHash2, null);
            mock.Setup(s => s.Exists(ns, bucket, new KeyId("somekey"))).ReturnsAsync(returnExistsSuccess).Verifiable();

            return mock;
        }
        
        [TestMethod]
        public async Task Ping()
        {
            IDDCRefService ddcRefService = GetDdcRefServiceMock().Object;
            HttpResponse res = await SendHttpRequest(ddcRefService,"GET", "/ping");
            Assert.AreEqual(200, res.StatusCode);
            Assert.AreEqual("pong", res.GetBodyAsString());
            Assert.AreEqual("text/plain", res.Headers["Content-Type"]);
        }
        
        [TestMethod]
        public async Task HealthLive()
        {
            IDDCRefService ddcRefService = GetDdcRefServiceMock().Object;
            HttpResponse res = await SendHttpRequest(ddcRefService,"GET", "/health/live");
            Assert.AreEqual(200, res.StatusCode);
            Assert.AreEqual("Healthy", res.GetBodyAsString());
            Assert.AreEqual("text/plain", res.Headers["Content-Type"]);
        }
        
        [TestMethod]
        public async Task HealthReady()
        {
            IDDCRefService ddcRefService = GetDdcRefServiceMock().Object;
            HttpResponse res = await SendHttpRequest(ddcRefService,"GET", "/health/ready");
            Assert.AreEqual(200, res.StatusCode);
            Assert.AreEqual("Healthy", res.GetBodyAsString());
            Assert.AreEqual("text/plain", res.Headers["Content-Type"]);
        }
        
        [TestMethod]
        public async Task DdcGet()
        {
            IDDCRefService ddcRefService = GetDdcRefServiceMock().Object;
            HttpResponse res = await SendHttpRequest(ddcRefService,"GET", "/api/v1/c/ddc/test.ddc/default/somekey.raw");
            Assert.AreEqual(200, res.StatusCode);
            Assert.AreEqual("application/octet-stream", res.Headers["Content-Type"]);
            Assert.AreEqual("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB", res.Headers[KestrelDispatcher.HeaderIoHash.ToString()]);
            Assert.AreEqual("testing", res.GetBodyAsString());
        }
        
        [TestMethod]
        public async Task DdcGetFail()
        {
            IDDCRefService ddcRefService = GetDdcRefServiceMock().Object;
            HttpResponse res = await SendHttpRequest(ddcRefService,"GET", "/api/v1/c/ddc/test.ddc/default/foo/bar/baz");
            Assert.AreEqual(400, res.StatusCode);
            Assert.AreEqual("application/json", res.Headers["Content-Type"]);

            string expectedBody = "Bad request path: /api/v1/c/ddc/test.ddc/default/foo/bar/baz";
            Assert.AreEqual(expectedBody.Length, Convert.ToInt32(res.Headers["Content-Length"]));
            Assert.AreEqual(expectedBody, res.GetBodyAsString());
        }
        
        [TestMethod]
        public async Task DdcHead()
        {
            IDDCRefService ddcRefService = GetDdcRefServiceMock().Object;
            HttpResponse res = await SendHttpRequest(ddcRefService,"HEAD", "/api/v1/c/ddc/test.ddc/default/somekey");
            Assert.AreEqual(204, res.StatusCode);
            Assert.AreEqual("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", res.Headers["X-Jupiter-IoHash"]);
            Assert.AreEqual("", res.GetBodyAsString());
        }
        
        [TestMethod]
        public async Task DdcHeadFail()
        {
            IDDCRefService ddcRefService = GetDdcRefServiceMock().Object;
            HttpResponse res = await SendHttpRequest(ddcRefService,"HEAD", "/api/v1/c/ddc/test.ddc/default/foo/bar/baz");
            Assert.AreEqual(400, res.StatusCode);
            Assert.IsTrue(res.GetBodyAsString().Contains("Bad request path"));
        }
        
        [TestMethod]
        public async Task DdcPut()
        {
            IDDCRefService ddcRefService = GetDdcRefServiceMock().Object;
            HttpResponse res = await SendHttpRequest(ddcRefService,"PUT", "/api/v1/c/ddc/test.ddc/default/somekey", "mypayload");
            Assert.AreEqual(200, res.StatusCode);
            Assert.AreEqual("{\"TransactionId\": 1}", res.GetBodyAsString());
        }
        
        [TestMethod]
        public async Task DdcPutLarge()
        {
            IDDCRefService ddcRefService = GetDdcRefServiceMock().Object;
            byte[] largeData = new byte[10 * 1024 * 1024];
            Array.Fill(largeData, (byte) 'j');
            HttpResponse res = await SendHttpRequest(ddcRefService,"PUT", "/api/v1/c/ddc/test.ddc/default/somekey", new AsciiString(largeData));
            Assert.AreEqual(200, res.StatusCode);
            Assert.AreEqual("{\"TransactionId\": 1}", res.GetBodyAsString());
            
            res = await SendHttpRequest(ddcRefService,"GET", "/api/v1/c/ddc/test.ddc/default/somekey.raw");
            Assert.AreEqual(200, res.StatusCode);
            Assert.AreEqual("application/octet-stream", res.Headers["Content-Type"]);
            Assert.AreEqual("testing", res.GetBodyAsString());
        }
        
        [TestMethod]
        public async Task NotFound()
        {
            IDDCRefService ddcRefService = GetDdcRefServiceMock().Object;
            HttpResponse res = await SendHttpRequest(ddcRefService, "GET", "/invalid-path");
            Assert.AreEqual(404, res.StatusCode);
            Assert.AreEqual("404 Not Found", res.GetBodyAsString());
            Assert.AreEqual("text/plain", res.Headers["Content-Type"]);
        }

        private static async Task<HttpResponse> SendHttpRequest(IDDCRefService ddcRefService, string method, string path, AsciiString? requestBody = null)
        {
            Pipe requestPipe = new ();
            Pipe responsePipe = new ();

            KestrelDispatcher dispatcher = new (ddcRefService, requestPipe.Reader, responsePipe.Writer, null!);
            
            AsciiString crlf = "\r\n";
            AsciiString eoh = "\r\n\r\n"; // End Of Headers
            AsciiString requestBytes = method + " " + path + " HTTP/1.1" + crlf + "Host: localhost";

            if (requestBody != null)
            {
                requestBytes += crlf + "Content-Length: " + Convert.ToString(requestBody.Value.Length);
            }
            
            requestBytes += eoh;

            await requestPipe.Writer.WriteAsync(requestBytes.Data);
            
            if (requestBody != null)
            {
                // Do two separate writes for splitting the buffers on the receiving (hopefully exercising more edge cases in pipeline reader)
                int size = requestBody.Value.Data.Length / 2;
                Memory<byte> firstSlice = requestBody.Value.Data.AsMemory().Slice(0, size);
                Memory<byte> secondSlice = requestBody.Value.Data.AsMemory().Slice(size);
                var t1 = requestPipe.Writer.WriteAsync(firstSlice);
                var t2 = requestPipe.Writer.WriteAsync(secondSlice);
            }
            
            await requestPipe.Writer.CompleteAsync();
            
            await dispatcher.ExecuteAsync(null!);

            byte[] temp = new byte[10000];
            MemoryStream ms = new (temp);
            await responsePipe.Reader.AsStream().CopyToAsync(ms);

            int bodyOffset = SearchBytes(temp, eoh);
            
            string headersStr = Encoding.ASCII.GetString(temp.AsSpan(0, bodyOffset));
            string[] headers = headersStr.Split("\r\n");
            string[] statusLineTokens = headers[0].Split(' ');
            Assert.AreEqual("HTTP/1.1", statusLineTokens[0]);
            int statusCode = Convert.ToInt32(statusLineTokens[1]);
            
            byte[] body = PruneZeroBytes(temp.AsSpan(bodyOffset + eoh.Length).ToArray());
            HttpResponse response = new (statusCode, body);
            
            foreach (string str in headers[1..])
            {
                string[] headerTokens = str.Split(": ");
                response.Headers[headerTokens[0]] = headerTokens[1];
            }
            
            return response;
        }
        
        private static int SearchBytes(byte[] haystack, byte[] needle)
        {
            int len = needle.Length;
            int limit = haystack.Length - len;
            for (int i = 0;  i <= limit;  i++ )
            {
                int k = 0;
                for (; k < len; k++)
                {
                    if (needle[k] != haystack[i + k])
                        break;
                }
                
                if( k == len )
                    return i;
            }
            return -1;
        }
        
        private static byte[] PruneZeroBytes(byte[] data) {
            if (data.Length == 0) return data;
            int i = data.Length - 1;
            while (data[i] == 0) {
                i--;
                if (i == 0) return Array.Empty<byte>();
            }
            byte[] copy = new byte[i + 1];
            Array.Copy(data, copy, i + 1);
            return copy;
        }
    }
}
