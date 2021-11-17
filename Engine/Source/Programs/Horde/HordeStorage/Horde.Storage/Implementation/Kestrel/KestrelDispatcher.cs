// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.IO;
using System.IO.Pipelines;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Horde.Storage.Controllers;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Server.Kestrel.Core.Internal.Http;

namespace Horde.Storage.Implementation.Kestrel
{
    public partial class KestrelDispatcher
    {
        private RequestType _requestType;
        private HttpMethod _requestMethod;
        private AsciiString _requestPath;
        private long? _requestBodyLength = null;
        private byte[]? _requestBodyData = null;
        private MemoryStream? _requestBodyDataStream = null;
        private readonly IDDCRefService _ddcRefService;

        private enum RequestType
        {
            Ping,
            HealthLive,
            HealthReady,
            DdcGet,
            DdcHead,
            DdcPut,
            NotRecognized
        }

        public static class Paths
        {
            public static readonly AsciiString Ping = "/ping";
            public static readonly AsciiString HealthLive = "/health/live";
            public static readonly AsciiString HealthReady = "/health/ready";
            public static readonly AsciiString DdcGet = "/api/v1/c/ddc";
            public static readonly AsciiString DdcHead = "/api/v1/c/ddc";
            public static readonly AsciiString DdcPut = "/api/v1/c/ddc";
        }

        private RequestType GetRequestType(ReadOnlySpan<byte> path)
        {
            if (path.Length == 5 && path.SequenceEqual(Paths.Ping)) { return RequestType.Ping; }
            if (path.Length == 12 && path.SequenceEqual(Paths.HealthLive)) { return RequestType.HealthLive; }
            if (path.Length == 13 && path.SequenceEqual(Paths.HealthReady)) { return RequestType.HealthReady; }
            if (_requestMethod == HttpMethod.Get && path.Length > 13 && path[..13].SequenceEqual(Paths.DdcGet)) { return RequestType.DdcGet; }
            if (_requestMethod == HttpMethod.Head && path.Length > 13 && path[..13].SequenceEqual(Paths.DdcHead)) { return RequestType.DdcHead; }
            if (_requestMethod == HttpMethod.Put && path.Length > 13 && path[..13].SequenceEqual(Paths.DdcPut)) { return RequestType.DdcPut; }
            
            return RequestType.NotRecognized;
        }
        
        private Task ProcessRequestAsync(ref ReadOnlySequence<byte> buffer) => _requestType switch
        {
            RequestType.Ping => Ping(Writer, Socket),
            RequestType.HealthLive => HealthLive(Writer, Socket),
            RequestType.HealthReady => HealthReady(Writer, Socket),
            RequestType.DdcGet => DdcGet(_requestPath, Writer, Socket),
            RequestType.DdcHead => DdcHead(_requestPath, Writer, Socket),
            RequestType.DdcPut => DdcPut(buffer, _requestPath, Writer, Socket),
            _ => Default(_requestPath, Writer, Socket)
        };
        
        private static readonly AsciiString Crlf = "\r\n";
        private static readonly AsciiString Eoh = "\r\n\r\n"; // End Of Headers
        private static readonly AsciiString Http11NotFound = "HTTP/1.1 404 Not Found\r\n";
        private static readonly AsciiString Http11NotFoundBody = "404 Not Found";
        private static readonly AsciiString Http11Ok = "HTTP/1.1 200 OK\r\n";
        private static readonly AsciiString Http11NoContent = "HTTP/1.1 204 No Content\r\n";
        private static readonly AsciiString Http11BadRequest = "HTTP/1.1 400 Bad Request\r\n";
        private static readonly AsciiString Http11InternalServerError = "HTTP/1.1 500 Internal Server Error\r\n";
        private static readonly AsciiString PingBody = "pong";
        private static readonly AsciiString HealthLiveBody = "Healthy";
        private static readonly AsciiString HeaderContentLength = "Content-Length";
        public static readonly AsciiString HeaderIoHash = "X-Jupiter-IoHash";
        private static readonly AsciiString HeaderIoHashRes = HeaderIoHash + ": ";
        private static readonly AsciiString HeaderServer = "Server: K";
        private static readonly AsciiString HeaderContentLengthRes = HeaderContentLength + ": ";
        private static readonly AsciiString HeaderContentTypeText = "Content-Type: text/plain";
        private static readonly AsciiString HeaderContentTypeJson = "Content-Type: application/json";
        private static readonly AsciiString HeaderContentTypeOctetStream = "Content-Type: application/octet-stream";

        private static readonly AsciiString _defaultPreamble =
            Http11NotFound +
            HeaderServer + Crlf +
            HeaderContentTypeText + Crlf +
            HeaderContentLengthRes + Http11NotFoundBody.Length.ToString() + Eoh;
        
        private async Task Default(AsciiString requestPath, PipeWriter pipeWriter, Socket socket)
        {
            _logger.Information("Unhandled request path: {RequestPath}", requestPath);
            await pipeWriter.WriteAsync(_defaultPreamble.Data.AsMemory());
            await pipeWriter.WriteAsync(Http11NotFoundBody.Data.AsMemory());
        }
        
        
        private static readonly AsciiString _pingPreamble =
            Http11Ok +
            HeaderServer + Crlf +
            HeaderContentTypeText + Crlf +
            HeaderContentLengthRes + PingBody.Length.ToString() + Eoh;

        private static async Task Ping(PipeWriter pipeWriter, Socket socket)
        {
            await pipeWriter.WriteAsync(_pingPreamble.Data.AsMemory());
            await pipeWriter.WriteAsync(PingBody.Data.AsMemory());
        }
        
        private static readonly AsciiString _healthLivePreamble =
            Http11Ok +
            HeaderServer + Crlf +
            HeaderContentTypeText + Crlf +
            HeaderContentLengthRes + HealthLiveBody.Length.ToString() + Eoh;

        private static async Task HealthLive(PipeWriter pipeWriter, Socket socket)
        {
            await pipeWriter.WriteAsync(_healthLivePreamble.Data.AsMemory());
            await pipeWriter.WriteAsync(HealthLiveBody.Data.AsMemory());
        }
        
        private static readonly AsciiString _healthReadyPreamble =
            Http11Ok +
            HeaderServer + Crlf +
            HeaderContentTypeText + Crlf +
            HeaderContentLengthRes + HealthLiveBody.Length.ToString() + Eoh;

        private static async Task HealthReady(PipeWriter pipeWriter, Socket socket)
        {
            await pipeWriter.WriteAsync(_healthReadyPreamble.Data.AsMemory());
            await pipeWriter.WriteAsync(HealthLiveBody.Data.AsMemory());
        }


        private async Task WriteJsonError(PipeWriter pipeWriter, int statusCode, AsciiString message)
        {
            AsciiString statusLine = statusCode switch
            {
                400 => Http11BadRequest,
                _ => Http11InternalServerError
            };

            AsciiString preamble = statusLine + HeaderServer + Crlf + HeaderContentTypeJson + Crlf + HeaderContentLengthRes + message.Length.ToString() + Eoh;
            await pipeWriter.WriteAsync(preamble.Data.AsMemory());
            await pipeWriter.WriteAsync(message.Data.AsMemory());
        }
        
        private static readonly AsciiString _ddcGetPreamble =
            Http11Ok +
            HeaderServer + Crlf +
            HeaderContentTypeOctetStream + Crlf +
            HeaderContentLengthRes;
        
        private async Task DdcGet(AsciiString requestPath, PipeWriter pipeWriter, Socket socket)
        {
            string[] parts = requestPath.ToString().Split("/");
            if (parts.Length != 8)
            {
                await WriteJsonError(pipeWriter, 400, "Bad request path: " + requestPath);
                return;
            }
            string ns = parts[5];
            string bucket = parts[6];
            string key = parts[7];
            bool isRaw = key.EndsWith(".raw");
            if (!isRaw)
            {
                await WriteJsonError(pipeWriter, 400, "Can only handle requests with .raw suffix");
                return;
            }

            key = key[..^4]; // Strip .raw suffix

            try
            {
                (RefResponse refRes, BlobContents? tempBlob) = await _ddcRefService.Get(new NamespaceId(ns), new BucketId(bucket), new KeyId(key), new[] {"blob"});
                BlobContents blob = tempBlob!;

                AsciiString contentLength = Convert.ToString(blob.Length) + Crlf + HeaderIoHashRes + refRes.ContentHash.ToString() + Eoh;
                await pipeWriter.WriteAsync(_ddcGetPreamble.Data.AsMemory());
                await pipeWriter.WriteAsync(contentLength.Data.AsMemory());
                await blob.Stream.CopyToAsync(pipeWriter, CancellationToken.None);
            }
            catch (NamespaceNotFoundException e)
            {
                await WriteJsonError(pipeWriter, 400, "{\"title\": \"Namespace " + e.Namespace + " did not exist\", \"status\": 400}");
            }
            catch (RefRecordNotFoundException e)
            {
                await WriteJsonError(pipeWriter, 400, "{\"title\": \"Object " + e.Bucket + " " + e.Key + " did not exist\", \"status\": 400}");
            }
            catch (BlobNotFoundException e)
            {
                await WriteJsonError(pipeWriter, 400, "{\"title\": \"Object " + e.Blob + " in " + e.Ns + " not found\", \"status\": 400}");
            }
            catch (Exception e)
            {
                await WriteJsonError(pipeWriter, 500, "{\"title\": \"Unhandled internal server error\", \"status\": 500}");
                _logger.Error(e, "Unhandled exception in DDC GET call");
            }
        }

        private static readonly AsciiString _ddcHeadPreamble =
            Http11NoContent +
            HeaderServer + Crlf +
            HeaderIoHashRes;
        
        private async Task DdcHead(AsciiString requestPath, PipeWriter pipeWriter, Socket socket)
        {
            string[] parts = requestPath.ToString().Split("/");
            if (parts.Length != 8)
            {
                await WriteJsonError(pipeWriter, 400, "Bad request path: " + requestPath);
                return;
            }
            string ns = parts[5];
            string bucket = parts[6];
            string key = parts[7];

            try
            {
                RefRecord refRecord = await _ddcRefService.Exists(new NamespaceId(ns), new BucketId(bucket), new KeyId(key));

                AsciiString contentHash = refRecord.ContentHash.ToString() + Eoh;
                await pipeWriter.WriteAsync(_ddcHeadPreamble.Data.AsMemory());
                await pipeWriter.WriteAsync(contentHash.Data.AsMemory());
            }
            catch (NamespaceNotFoundException e)
            {
                await WriteJsonError(pipeWriter, 400, "{\"title\": \"Namespace " + e.Namespace + " did not exist\", \"status\": 400}");
            }
            catch (RefRecordNotFoundException e)
            {
                await WriteJsonError(pipeWriter, 400, "{\"title\": \"Object " + e.Bucket + " " + e.Key + " did not exist\", \"status\": 400}");
            }
            catch (MissingBlobsException e)
            {
                await WriteJsonError(pipeWriter, 400, "{\"title\": \"Blobs " + e.Blobs + " from object " + e.Bucket + " " + e.Key + " in namespace " + e.Namespace + " did not exist\", \"status\": 400}");
            }
            catch (Exception e)
            {
                await WriteJsonError(pipeWriter, 500, "{\"title\": \"Unhandled internal server error\", \"status\": 500}");
                _logger.Error(e, "Unhandled exception in DDC HEAD call");
                throw;
            }
        }
        
        private static readonly AsciiString _ddcPutContent = "{\"TransactionId\": 1}";

        private static readonly AsciiString _ddcPutNoOpPreamble =
            Http11Ok +
            HeaderServer + Crlf +
            HeaderContentTypeJson + Crlf +
            HeaderContentLengthRes + _ddcPutContent.Length.ToString() + Eoh;
        
        private async Task DdcPut(ReadOnlySequence<byte> buffer, AsciiString requestPath, PipeWriter pipeWriter, Socket socket)
        {
            await pipeWriter.WriteAsync(_ddcPutNoOpPreamble.Data.AsMemory());
            await pipeWriter.WriteAsync(_ddcPutContent.Data.AsMemory());
        }
    }
}
