// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.IO;
using System.Linq;
using System.Net.Mime;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Horde.Storage.Implementation;
using Jupiter;
using Jupiter.Common.Implementation;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Serilog;

namespace Horde.Storage.Controllers
{
    using BlobNotFoundException = Horde.Storage.Implementation.BlobNotFoundException;

    [ApiController]
    [Route("api/v1/s", Order = 1)]
    [Route("api/v1/blobs", Order = 0)]
    public class StorageController : ControllerBase
    {
        private readonly IBlobService _storage;
        private readonly IDiagnosticContext _diagnosticContext;
        private readonly IAuthorizationService _authorizationService;
        private readonly BufferedPayloadFactory _bufferedPayloadFactory;

        public StorageController(IBlobService storage, IDiagnosticContext diagnosticContext, IAuthorizationService authorizationService, BufferedPayloadFactory bufferedPayloadFactory)
        {
            _storage = storage;
            _diagnosticContext = diagnosticContext;
            _authorizationService = authorizationService;
            _bufferedPayloadFactory = bufferedPayloadFactory;
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
                {
                    missingBlobs.Add(blob);
                }
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
                {
                    missingBlobs.Add(blob);
                }
            });
            await Task.WhenAll(tasks);

            return Ok(new HeadMultipleResponse { Needs = missingBlobs.ToArray()});
        }

        private async Task<BlobContents> GetImpl(NamespaceId ns, BlobIdentifier blob)
        {
            try
            {
                return await _storage.GetObject(ns, blob);
            }
            catch (BlobNotFoundException)
            {
                if (!_storage.ShouldFetchBlobOnDemand(ns))
                {
                    throw;
                }

                return await _storage.ReplicateObject(ns, blob);
            }
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

            _diagnosticContext.Set("Content-Length", Request.ContentLength ?? -1);
            using IBufferedPayload payload = await _bufferedPayloadFactory.CreateFromRequest(Request);

            try
            {
                BlobIdentifier identifier = await _storage.PutObject(ns, payload, id);
                return Ok(new
                {
                    Identifier = identifier.ToString()
                });
            }
            catch (ResourceHasToManyRequestsException)
            {
                return StatusCode(StatusCodes.Status429TooManyRequests);
            }
        }

        [HttpPost("{ns}")]
        [Authorize("Storage.write")]
        [RequiredContentType(MediaTypeNames.Application.Octet)]
        [DisableRequestSizeLimit]
        public async Task<IActionResult> Post(
            [Required] NamespaceId ns)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            _diagnosticContext.Set("Content-Length", Request.ContentLength ?? -1);
            using IBufferedPayload payload = await _bufferedPayloadFactory.CreateFromRequest(Request);
            
            await using Stream stream = payload.GetStream();

            BlobIdentifier id = await BlobIdentifier.FromStream(stream);
            await _storage.PutObjectKnownHash(ns, payload, id);

            return Ok(new
            {
                Identifier = id.ToString()
            });
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
        [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1034:Nested types should not be visible", Justification = "Used by serialization")]
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

        [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1034:Nested types should not be visible", Justification = "Used by serialization")]
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
                throw new ();
            }

            Task<object?>[] tasks = new Task<object?>[batch.Operations.Length];
            for (int index = 0; index < batch.Operations.Length; index++)
            {
                BatchOp op = batch.Operations[index];
                if (op.Namespace == null)
                {
                    throw new Exception(nameof(op.Namespace));
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
                        throw new InvalidOperationException($"Op type {BatchOp.Operation.INVALID} is not a valid op type for operation id {op.Id}");
                    case BatchOp.Operation.GET:
                        if (op.Id == null)
                        {
                            return BadRequest();
                        }

                        tasks[index] = GetImpl(op.Namespace.Value, op.Id).ContinueWith((t, _) =>
                        {
                            // TODO: This is very allocation heavy but given that the end result is a json object we can not really stream this anyway
                            using BlobContents blobContents = t.Result;

                            using MemoryStream ms = new MemoryStream();
                            blobContents.Stream.CopyTo(ms);
                            ms.Seek(0, SeekOrigin.Begin);
                            string str = Convert.ToBase64String(ms.ToArray());
                            return (object?) str;
                        }, null, TaskScheduler.Current);
                        break;
                    case BatchOp.Operation.HEAD:
                        if (op.Id == null)
                        {
                            return BadRequest();
                        }

                        tasks[index] = _storage.Exists(op.Namespace.Value, op.Id)
                            .ContinueWith((t,_) => t.Result ? (object?) null : op.Id, null, TaskScheduler.Current);
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

                        using MemoryBufferedPayload payload = new MemoryBufferedPayload(op.Content);
                        tasks[index] = _storage.PutObject(op.Namespace.Value, payload, op.Id).ContinueWith((t, _) => (object?) t.Result, null, TaskScheduler.Current);
                        break;
                    }
                    case BatchOp.Operation.DELETE:
                        if (op.Id == null)
                        {
                            return BadRequest();
                        }

                        tasks[index] = DeleteImpl(op.Namespace.Value, op.Id).ContinueWith((t, _) => (object?) null, null, TaskScheduler.Current);
                        break;
                    default:
                        throw new NotImplementedException($"{op.Op} is not a support op type");
                }
            }

            await Task.WhenAll(tasks);

            object?[] results = tasks.Select(t => t.Result).ToArray();

            return Ok(results);
        }
    }

    public class HeadMultipleResponse
    {
        [CbField("needs")]
        public BlobIdentifier[] Needs { get; set; } = null!;
    }
}
