// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.IO;
using System.Linq;
using System.Net.Mime;
using System.Threading.Tasks;
using Datadog.Trace;
using Horde.Storage.Implementation;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Controllers
{
    [ApiController]
    [Route("api/v1/s", Order = 1)]
    [Route("api/v1/blobs", Order = 0)]
    public class StorageController : ControllerBase
    {
        private readonly IBlobStore _storage;
        private readonly IDiagnosticContext _diagnosticContext;
        private readonly IAuthorizationService _authorizationService;
        private readonly HordeStorageSettings _settings;

        private readonly ILogger _logger = Log.ForContext<StorageController>();

        public StorageController(IBlobStore storage, IOptions<HordeStorageSettings> settings, IDiagnosticContext diagnosticContext, IAuthorizationService authorizationService)
        {
            _storage = storage;
            _diagnosticContext = diagnosticContext;
            _authorizationService = authorizationService;
            _settings = settings.Value;
        }


        [HttpGet("{ns}/{id}")]
        [Authorize("Storage.read")]
        [ProducesDefaultResponseType]
        public async Task<IActionResult> Get(
            [Required] NamespaceId ns,
            [Required] BlobIdentifier id)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            try
            {
                BlobContents blobContents = await GetImpl(ns, id);

                return File(blobContents.Stream, MediaTypeNames.Application.Octet, enableRangeProcessing: true);
            }
            catch (BlobNotFoundException e)
            {
                return NotFound(new ValidationProblemDetails { Title = $"Blob {e.Blob} not found"});
            }
        }
        
        [HttpHead("{ns}/{id}")]
        [Authorize("Storage.read")]
        [ProducesDefaultResponseType]
        public async Task<IActionResult> Head(
            [Required] NamespaceId ns,
            [Required] BlobIdentifier id)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }
            bool exists = await _storage.Exists(ns, id);

            if (!exists)
            {
                return NotFound(new ValidationProblemDetails { Title = $"Blob {id} not found"});
            }

            return Ok();
        }

        [HttpPost("{ns}/exists")]
        [Authorize("Storage.read")]
        [ProducesDefaultResponseType]
        public async Task<IActionResult> ExistsMultiple(
            [Required] NamespaceId ns,
             [Required] [FromQuery] List<BlobIdentifier> id)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            ConcurrentBag<BlobIdentifier> missingBlobs = new ConcurrentBag<BlobIdentifier>();

            IEnumerable<Task> tasks = id.Select(async blob =>
            {
                if (!await _storage.Exists(ns, blob))
                    missingBlobs.Add(blob);
            });
            await Task.WhenAll(tasks);

            return Ok(new HeadMultipleResponse { Needs = missingBlobs.ToArray()});
        }

        [HttpPost("{ns}/exist")]
        [Authorize("Storage.read")]
        [ProducesDefaultResponseType]
        public async Task<IActionResult> ExistsBody(
            [Required] NamespaceId ns,
            [FromBody] BlobIdentifier[] bodyIds)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            ConcurrentBag<BlobIdentifier> missingBlobs = new ConcurrentBag<BlobIdentifier>();

            IEnumerable<Task> tasks = bodyIds.Select(async blob =>
            {
                if (!await _storage.Exists(ns, blob))
                    missingBlobs.Add(blob);
            });
            await Task.WhenAll(tasks);

            return Ok(new HeadMultipleResponse { Needs = missingBlobs.ToArray()});
        }

        private async Task<BlobContents> GetImpl(NamespaceId ns, BlobIdentifier blob)
        {
            return await _storage.GetObject(ns, blob);
        }

        [HttpPut("{ns}/{id}")]
        [Authorize("Storage.write")]
        [RequiredContentType(MediaTypeNames.Application.Octet)]
        [DisableRequestSizeLimit]
        public async Task<IActionResult> Put(
            [Required] NamespaceId ns,
            [Required] BlobIdentifier id)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            BlobIdentifier identifier;
            // blob is small enough to fit into memory so we just stream it into memory
            if (Request.ContentLength is < Int32.MaxValue)
            {
                byte[] blob;
                try
                {
                    blob = await RequestUtil.ReadRawBody(Request);
                }
                catch (BadHttpRequestException e)
                {
                    const string msg = "Partial content transfer when reading request body.";
                    _logger.Warning(e, msg);
                    return BadRequest(msg);
                }

                identifier = await PutImpl(ns, id, blob);
            }
            else
            {
                FileInfo? tempFile = null;

                Stream s = Request.Body;
                try
                {
                    // stream to a temporary file on disk if the stream is not seekable
                    if (!Request.Body.CanSeek)
                    {
                        tempFile = new FileInfo(Path.GetTempFileName());

                        {
                            await using FileStream fs = tempFile.OpenWrite();
                            await Request.Body.CopyToAsync(fs);
                        }

                        s = tempFile.OpenRead();
                    }
                    identifier = await PutImpl(ns, id, s);
                }
                finally
                {
                    s.Close();
                    if (tempFile != null && tempFile.Exists)
                        tempFile.Delete();
                }
            }
            _diagnosticContext.Set("Content-Length", Request.ContentLength ?? -1);

            return Ok(new
            {
                Identifier = identifier.ToString()
            });
        }

        private async Task<BlobIdentifier> PutImpl(NamespaceId ns, BlobIdentifier id, byte[] content)
        {
            BlobIdentifier identifier;
            {
                using Scope _ = Tracer.Instance.StartActive("web.hash");
                identifier = BlobIdentifier.FromBlob(content);
            }

            if (!id.Equals(identifier))
            {
                _logger.Debug("ID {@Id} was not the same as identifier {@Identifier} {Content}", id, identifier,
                    content);

                throw new ArgumentException("ID was not a hash of the content uploaded.", paramName: nameof(id));
            }

            await _storage.PutObject(ns, content, identifier);
            return identifier;
        }

        private async Task<BlobIdentifier> PutImpl(NamespaceId ns, BlobIdentifier id, Stream content)
        {
            BlobIdentifier identifier;
            {
                using Scope _ = Tracer.Instance.StartActive("web.hash");
                identifier = await BlobIdentifier.FromStream(content);
            }

            if (!id.Equals(identifier))
            {
                _logger.Debug("ID {@Id} was not the same as identifier {@Identifier} {Content}", id, identifier,
                    content);

                throw new ArgumentException("ID was not a hash of the content uploaded.", paramName: nameof(id));
            }

            // seek back to the beginning
            content.Position = 0;
            await _storage.PutObject(ns, content, identifier);
            return identifier;
        }

        [HttpDelete("{ns}/{id}")]
        [Authorize("Storage.delete")]
        public async Task<IActionResult> Delete(
            [Required] NamespaceId ns,
            [Required] BlobIdentifier id)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            await DeleteImpl(ns, id);

            return NoContent();
        }

        
        [HttpDelete("{ns}")]
        [Authorize("Admin")]
        public async Task<IActionResult> DeleteNamespace(
            [Required] NamespaceId ns)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            await  _storage.DeleteNamespace(ns);

            return NoContent();
        }


        private async Task DeleteImpl(NamespaceId ns, BlobIdentifier id)
        {
            await _storage.DeleteObject(ns, id);
        }

        // ReSharper disable UnusedAutoPropertyAccessor.Global
        // ReSharper disable once ClassNeverInstantiated.Global
        public class BatchOp
        {
            // ReSharper disable once InconsistentNaming
            public enum Operation
            {
                INVALID,
                GET,
                PUT,
                DELETE,
                HEAD
            }

            [Required] public NamespaceId? Namespace { get; set; }

            public BlobIdentifier? Id { get; set; }

            [Required] public Operation Op { get; set; }

            public byte[]? Content { get; set; }
        }

        public class BatchCall
        {
            public BatchOp[]? Operations { get; set; }
        }
        // ReSharper restore UnusedAutoPropertyAccessor.Global

        [HttpPost("")]
        public async Task<IActionResult> Post([FromBody] BatchCall batch)
        {
            string OpToPolicy(BatchOp.Operation op)
            {
                switch (op)
                {
                    case BatchOp.Operation.GET:
                    case BatchOp.Operation.HEAD:
                        return "Storage.read";
                    case BatchOp.Operation.PUT:
                        return "Storage.write";
                    case BatchOp.Operation.DELETE:
                        return "Storage.delete";
                    default:
                        throw new ArgumentOutOfRangeException(nameof(op), op, null);
                }
            }

            if (batch?.Operations == null)
            {
                throw new ArgumentNullException();
            }

            Task<object?>[] tasks = new Task<object?>[batch.Operations.Length];
            for (int index = 0; index < batch.Operations.Length; index++)
            {
                BatchOp op = batch.Operations[index];
                if (op.Namespace == null)
                {
                    throw new ArgumentNullException("namespace");
                }

                AuthorizationResult authorizationResultNamespace = await _authorizationService.AuthorizeAsync(User, op.Namespace, NamespaceAccessRequirement.Name);
                AuthorizationResult authorizationResultOp = await _authorizationService.AuthorizeAsync(User, OpToPolicy(op.Op));

                if (!authorizationResultNamespace.Succeeded || !authorizationResultOp.Succeeded)
                {
                    return Forbid();
                }

                switch (op.Op)
                {
                    case BatchOp.Operation.INVALID:
                        throw new ArgumentOutOfRangeException();
                    case BatchOp.Operation.GET:
                        if (op.Id == null)
                        {
                            throw new ArgumentNullException("id");
                        }

                        tasks[index] = GetImpl(op.Namespace.Value, op.Id).ContinueWith(t =>
                        {
                            // TODO: This is very allocation heavy but given that the end result is a json object we can not really stream this anyway
                            using BlobContents blobContents = t.Result;

                            using MemoryStream ms = new MemoryStream();
                            blobContents.Stream.CopyTo(ms);
                            ms.Seek(0, SeekOrigin.Begin);
                            string str = Convert.ToBase64String(ms.ToArray());
                            return (object?) str;
                        });
                        break;
                    case BatchOp.Operation.HEAD:
                        if (op.Id == null)
                        {
                            throw new ArgumentNullException("id");
                        }

                        tasks[index] = _storage.Exists(op.Namespace.Value, op.Id)
                            .ContinueWith(t => t.Result ? (object?) null : op.Id);
                        break;
                    case BatchOp.Operation.PUT:
                    {
                        if (op.Content == null)
                        {
                            return BadRequest();
                        }

                        if (op.Id == null)
                        {
                            return BadRequest();
                        }

                        tasks[index] = PutImpl(op.Namespace.Value, op.Id, op.Content).ContinueWith(t => (object?) t.Result);
                        break;
                    }
                    case BatchOp.Operation.DELETE:
                        if (op.Id == null)
                        {
                            throw new ArgumentNullException("id");
                        }

                        tasks[index] = DeleteImpl(op.Namespace.Value, op.Id).ContinueWith(t => (object?) null);
                        break;
                    default:
                        throw new ArgumentOutOfRangeException();
                }
            }

            await Task.WhenAll(tasks);

            object?[] results = tasks.Select(t => t.Result).ToArray();

            return Ok(results);
        }

    }

    public class HeadMultipleResponse
    {
        public BlobIdentifier[] Needs { get; set; } = null!;
    }
}
