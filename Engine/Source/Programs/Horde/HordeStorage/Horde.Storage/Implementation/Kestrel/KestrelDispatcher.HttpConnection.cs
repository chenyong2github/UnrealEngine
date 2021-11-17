// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.IO;
using System.IO.Pipelines;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;
using Jupiter;
using Microsoft.AspNetCore.Connections;
using Microsoft.AspNetCore.Server.Kestrel.Core.Internal.Http;
using Microsoft.Extensions.DependencyInjection;
using Serilog;

namespace Horde.Storage.Implementation.Kestrel
{
    public partial class KestrelDispatcher : BaseHttpConnection
    {
        private readonly ILogger _logger = Log.ForContext<KestrelDispatcher>();
        private State _state;
        private HttpParser<ParsingAdapter> Parser { get; } = new HttpParser<ParsingAdapter>();
        
        public KestrelDispatcher(IServiceProvider serviceProvider, PipeReader reader, PipeWriter writer, Socket socket) : base(serviceProvider, reader, writer, socket)
        {
            _ddcRefService = serviceProvider.GetService<DDCRefService>()!;
        }
        
        // Used for tests only
        public KestrelDispatcher(IDDCRefService ddcRefService, PipeReader reader, PipeWriter writer, Socket socket) : base(null!, reader, writer, socket)
        {
            _ddcRefService = ddcRefService;
        }

        /// <inheritdoc/>
        public override async Task ExecuteAsync(ConnectionContext connection)
        {
            try
            {
                await ProcessRequestsAsync();

                Reader.Complete();
            }
            catch (Exception ex)
            {
                Reader.Complete(ex);

                if (!(ex is ConnectionResetException))
                {
                    throw;  
                }
            }
            finally
            {
                Writer.Complete();
            }
        }

        private async Task ProcessRequestsAsync()
        {
            while (true)
            {
                _requestBodyLength = null;
                _requestBodyData = null;
                _requestBodyDataStream = null;
                var readResult = await Reader.ReadAsync();
                var buffer = readResult.Buffer;
                var isCompleted = readResult.IsCompleted;

                if (buffer.IsEmpty && isCompleted)
                {
                    return;
                }

                while (true)
                {
                    if (!ParseHttpRequest(ref buffer, isCompleted))
                    {
                        return;
                    }

                    if (_state == State.Body)
                    {
                        await ProcessRequestAsync(ref buffer);

                        _state = State.StartLine;

                        if (!buffer.IsEmpty)
                        {
                            // More input data to parse
                            continue;
                        }
                    }

                    // No more input or incomplete data, Advance the Reader
                    Reader.AdvanceTo(buffer.Start, buffer.End);
                    break;
                }

                await Writer.FlushAsync();
            }
        }

        private bool ParseHttpRequest(ref ReadOnlySequence<byte> buffer, bool isCompleted)
        {
            var reader = new SequenceReader<byte>(buffer);
            var state = _state;

            if (state == State.StartLine)
            {
                if (Parser.ParseRequestLine(new ParsingAdapter(this), ref reader))
                {
                    state = State.Headers;
                }
            }

            if (state == State.Headers)
            {
                var success = Parser.ParseHeaders(new ParsingAdapter(this), ref reader);

                if (success)
                {
                    state = State.Body;
                }
            }

            if (state != State.Body && isCompleted)
            {
                ThrowUnexpectedEndOfData();
            }

            _state = state;

            if (state == State.Body)
            {
                if (_requestMethod == HttpMethod.Put && _requestBodyLength != null && _requestBodyDataStream != null)
                {
                    // Remove the status line and headers
                    buffer = buffer.Slice(reader.Position);
                    // Body starts here in buffer

                    // Dumb byte-by-byte reading
                    var temp = new byte[1];
                    while (reader.TryRead(out byte v))
                    {
                        temp[0] = v;
                        _requestBodyDataStream.Write(temp);
                    }
                    buffer = buffer.Slice(reader.Position, 0);
                }
                else
                {
                    // Mark the rest as consumed
                    buffer = buffer.Slice(reader.Position, 0);
                }
            }
            else
            {
                // In-complete request read, consumed is current position and examined is the remaining.
                buffer = buffer.Slice(reader.Position);
            }

            return true;
        }

        public void OnStaticIndexedHeader(int index)
        {
        }

        public void OnStaticIndexedHeader(int index, ReadOnlySpan<byte> value)
        {
        }

        public void OnHeader(ReadOnlySpan<byte> name, ReadOnlySpan<byte> value)
        {
            if (name.Length == 14 && name.SequenceEqual(HeaderContentLength))
            {
                _requestBodyLength = Convert.ToInt64(Encoding.ASCII.GetString(value));
                _requestBodyData = new byte[_requestBodyLength.Value];
                _requestBodyDataStream = new MemoryStream(_requestBodyData);
            }
        }
        
        public void OnHeadersComplete(bool endStream)
        {
        }
        
        public void OnStartLine(HttpVersionAndMethod versionAndMethod, TargetOffsetPathLength targetPath, Span<byte> startLine)
        {
            _requestMethod = versionAndMethod.Method;
            _requestType = GetRequestType(startLine.Slice(targetPath.Offset, targetPath.Length));
            _requestPath = new AsciiString(startLine.Slice(targetPath.Offset, targetPath.Length).ToArray());
        }
        
        private static void ThrowUnexpectedEndOfData()
        {
            throw new InvalidOperationException("Unexpected end of data!");
        }

        private enum State
        {
            StartLine,
            Headers,
            Body
        }

        private struct ParsingAdapter : IHttpRequestLineHandler, IHttpHeadersHandler
        {
            public KestrelDispatcher RequestHandler;

            public ParsingAdapter(KestrelDispatcher requestHandler)
                => RequestHandler = requestHandler;

            public void OnStaticIndexedHeader(int index) 
                => RequestHandler.OnStaticIndexedHeader(index);

            public void OnStaticIndexedHeader(int index, ReadOnlySpan<byte> value)
                => RequestHandler.OnStaticIndexedHeader(index, value);

            public void OnHeader(ReadOnlySpan<byte> name, ReadOnlySpan<byte> value)
                => RequestHandler.OnHeader(name, value);

            public void OnHeadersComplete(bool endStream)
                => RequestHandler.OnHeadersComplete(endStream);

            public void OnStartLine(HttpVersionAndMethod versionAndMethod, TargetOffsetPathLength targetPath, Span<byte> startLine)
                => RequestHandler.OnStartLine(versionAndMethod, targetPath, startLine);
        }
    }
}
