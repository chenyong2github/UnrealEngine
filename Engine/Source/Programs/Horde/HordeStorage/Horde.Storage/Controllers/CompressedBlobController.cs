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
using Serilog;

namespace Horde.Storage.Controllers
{
    [ApiController]
    [Route("api/v1/compressed-blobs")]
    public class CompressedBlobController : ControllerBase
    {
        private readonly IBlobStore _storage;
        private readonly IContentIdStore _contentIdStore;
        private readonly IDiagnosticContext _diagnosticContext;
        private readonly IAuthorizationService _authorizationService;
        private readonly CompressedBufferUtils _compressedBufferUtils;

        private readonly ILogger _logger = Log.ForContext<CompressedBlobController>();

        public CompressedBlobController(IBlobStore storage, IContentIdStore contentIdStore, IDiagnosticContext diagnosticContext, IAuthorizationService authorizationService, CompressedBufferUtils compressedBufferUtils)
        {
            _storage = storage;
            _contentIdStore = contentIdStore;
            _diagnosticContext = diagnosticContext;
            _authorizationService = authorizationService;
            _compressedBufferUtils = compressedBufferUtils;
        }


        [HttpGet("{ns}/{id}")]
        [Authorize("Storage.read")]
        [ProducesResponseType(type: typeof(byte[]), 200)]
        [ProducesResponseType(type: typeof(ValidationProblemDetails), 400)]
        [Produces(MediaTypeNames.Application.Json, CustomMediaTypeNames.UnrealCompactBinary, CustomMediaTypeNames.UnrealCompressedBuffer)]

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

                return File(blobContents.Stream, CustomMediaTypeNames.UnrealCompressedBuffer, enableRangeProcessing: true);
            }
            catch (BlobNotFoundException e)
            {
                return NotFound(new ValidationProblemDetails { Title = $"Object {e.Blob} not found" });
            }
            catch (ContentIdResolveException e)
            {
                return NotFound(new ValidationProblemDetails { Title = $"Content Id {e.ContentId} not found" });
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
            BlobIdentifier[]? chunks = await _contentIdStore.Resolve(ns, id);
            if (chunks == null)
            {
                return NotFound(new ValidationProblemDetails { Title = $"Content-id {id} not found"});
            }

            Task<bool>[] tasks = new Task<bool>[chunks.Length];
            for (int i = 0; i < chunks.Length; i++)
            {
                tasks[i] = _storage.Exists(ns, id);
            }

            await Task.WhenAll(tasks);

            bool exists = tasks.All(task => task.Result);

            if (!exists)
            {
                return NotFound(new ValidationProblemDetails { Title = $"Object {id} not found"});
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
            ConcurrentBag<BlobIdentifier> invalidContentIds = new ConcurrentBag<BlobIdentifier>();

            IEnumerable<Task> tasks = id.Select(async blob =>
            {
                BlobIdentifier[]? chunks = await _contentIdStore.Resolve(ns, blob);

                if (chunks == null)
                {
                    invalidContentIds.Add(blob);
                    return;
                }

                foreach (BlobIdentifier chunk in chunks)
                {
                    if (!await _storage.Exists(ns, chunk))
                    {
                        missingBlobs.Add(blob);
                    }
                }
            });
            await Task.WhenAll(tasks);

            if (invalidContentIds.Count != 0)
                return NotFound(new ValidationProblemDetails { Title = $"Missing content ids {string.Join(" ,", invalidContentIds)}"});

            return Ok(new HeadMultipleResponse { Needs = missingBlobs.ToArray()});
        }

        private async Task<BlobContents> GetImpl(NamespaceId ns, BlobIdentifier contentId)
        {
            BlobIdentifier[]? chunks = await _contentIdStore.Resolve(ns, contentId);
            if (chunks == null)
            {
                throw new ContentIdResolveException(contentId);
            }

            using Scope _ = Tracer.Instance.StartActive("blob.combine");
            Task<BlobContents>[] tasks = new Task<BlobContents>[chunks.Length];
            for (int i = 0; i < chunks.Length; i++)
            {
                tasks[i] = _storage.GetObject(ns, chunks[i]);
            }

            MemoryStream ms = new MemoryStream();
            foreach (Task<BlobContents> task in tasks)
            {
                BlobContents blob = await task;
                await using Stream s = blob.Stream;
                await s.CopyToAsync(ms);
            }

            ms.Seek(0, SeekOrigin.Begin);

            return new BlobContents(ms, ms.Length);
        }

        [HttpPut("{ns}/{id}")]
        [Authorize("Storage.write")]
        [DisableRequestSizeLimit]
        [RequiredContentType(CustomMediaTypeNames.UnrealCompressedBuffer)]
        public async Task<IActionResult> Put(
            [Required] NamespaceId ns,
            [Required] BlobIdentifier id)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

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
            _diagnosticContext.Set("Content-Length", Request.ContentLength ?? -1);

            BlobIdentifier identifier = await PutImpl(ns, id, blob);
            return Ok(new
            {
                Identifier = identifier.ToString()
            });
        }

        private async Task<BlobIdentifier> PutImpl(NamespaceId ns, BlobIdentifier? id, byte[] content)
        {
            BlobIdentifier identifier;
            {
                using Scope _ = Tracer.Instance.StartActive("web.hash");
                identifier = BlobIdentifier.FromBlob(content);
            }
            // decompress the content and generate a identifier from it to verify the identifier we got
            byte[] decompressedContent = _compressedBufferUtils.DecompressContent(content);

            BlobIdentifier identifierDecompressedPayload;
            {
                using Scope _ = Tracer.Instance.StartActive("web.hash");
                identifierDecompressedPayload = BlobIdentifier.FromBlob(decompressedContent);
            }

            if (id != null && !id.Equals(identifierDecompressedPayload))
            {
                _logger.Debug("ID {@Id} was not the same as identifier {@Identifier} {Content}", id, identifierDecompressedPayload,
                    content);

                throw new ArgumentException("ID was not a hash of the content uploaded.", paramName: nameof(id));
            }

            // commit the mapping from the decompressed hash to the compressed hash, we run this in parallel with the blob store submit
            // TODO: let users specify weight of the blob compared to previously submitted content ids
            Task contentIdStoreTask = _contentIdStore.Put(ns, identifierDecompressedPayload, identifier, content.Length);

            // we still commit the compressed buffer to the object store
            await _storage.PutObject(ns, content, identifier);

            await contentIdStoreTask;

            return identifierDecompressedPayload;
        }

        /*[HttpDelete("{ns}/{id}")]
        [Authorize("Storage.delete")]
        public async Task<IActionResult> Delete(
            [Required] string ns,
            [Required] BlobIdentifier id)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            await DeleteImpl(ns, id);

            return Ok();
        }

        
        [HttpDelete("{ns}")]
        [Authorize("Admin")]
        public async Task<IActionResult> DeleteNamespace(
            [Required] string ns)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            int deletedCount = await  _storage.DeleteNamespace(ns);

            return Ok( new { Deleted = deletedCount });
        }


        private async Task DeleteImpl(string ns, BlobIdentifier id)
        {
            await _storage.DeleteObject(ns, id);
        }*/
    }
}
