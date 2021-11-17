// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Net.Mime;
using System.Threading.Tasks;
using async_enumerable_dotnet;
using Datadog.Trace;
using Horde.Storage.Implementation;
using Jupiter;
using Jupiter.Implementation;
using Jupiter.Utils;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Serilog;

namespace Horde.Storage.Controllers
{
    [ApiController]
    [Route("api/v1/objects", Order = 0)]
    [Produces(CustomMediaTypeNames.UnrealCompactBinary, MediaTypeNames.Application.Json)]
    public class ObjectController : ControllerBase
    {
        private readonly IBlobStore _storage;
        private readonly IDiagnosticContext _diagnosticContext;
        private readonly IAuthorizationService _authorizationService;
        private readonly IReferenceResolver _referenceResolver;

        private readonly ILogger _logger = Log.ForContext<ObjectController>();

        public ObjectController(IBlobStore storage, IDiagnosticContext diagnosticContext, IAuthorizationService authorizationService, IReferenceResolver referenceResolver)
        {
            _storage = storage;
            _diagnosticContext = diagnosticContext;
            _authorizationService = authorizationService;
            _referenceResolver = referenceResolver;
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
                BlobContents blobContents = await _storage.GetObject(ns, id);

                return File(blobContents.Stream, CustomMediaTypeNames.UnrealCompactBinary);
            }
            catch (BlobNotFoundException e)
            {
                return NotFound(new ValidationProblemDetails {Title = $"Object {e.Blob} not found"});
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
                return NotFound(new ValidationProblemDetails {Title = $"Object {id} not found"});
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

            return Ok(new HeadMultipleResponse {Needs = missingBlobs.ToArray()});
        }

        [HttpPut("{ns}/{id}")]
        [Authorize("Storage.write")]
        [RequiredContentType(CustomMediaTypeNames.UnrealCompactBinary)]
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

            BlobIdentifier blobHash;
            {
                using Scope _ = Tracer.Instance.StartActive("web.hash");
                blobHash = BlobIdentifier.FromBlob(blob);
            }

            if (!id.Equals(blobHash))
            {
                _logger.Debug("ID {@Id} was not the same as identifier {@Identifier} {Content}", id, blobHash,
                    blob);

                throw new ArgumentException("ID was not a hash of the content uploaded.", paramName: nameof(id));
            }

            await _storage.PutObject(ns, blob, blobHash);
            BlobIdentifier identifier = blobHash;
            return Ok(new
            {
                Identifier = identifier
            });
        }

        [HttpGet("{ns}/{id}/references")]
        [Authorize("Storage.read")]
        public async Task<IActionResult> ResolveReferences(
            [Required] NamespaceId ns,
            [Required] BlobIdentifier id,
            [FromQuery] int depth = 1)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            BlobContents blob = await _storage.GetObject(ns, id);
            byte[] blobContents = await blob.Stream.ToByteArray();
            CompactBinaryObject compactBinaryObject = CompactBinaryObject.Load(blobContents);

            try
            {
                BlobIdentifier[] references = await _referenceResolver.ResolveReferences(ns, compactBinaryObject).ToArrayAsync();
                return Ok(new ResolvedReferencesResult(references));
            }
            catch (PartialReferenceResolveException e)
            {
                return BadRequest(new ValidationProblemDetails {Title = $"Object {id} is missing blobs", Detail = $"Following blobs are missing: {string.Join(",", e.UnresolvedReferences)}"});
            }
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

            await _storage.DeleteObject(ns, id);

            return Ok( new DeletedResponse
            {
                DeletedCount = 1
            });
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

            await _storage.DeleteNamespace(ns);

            return Ok();
        }
    }

    public class DeletedResponse
    {
        public int DeletedCount { get; set; }
    }

    public class ResolvedReferencesResult
    {
        public ResolvedReferencesResult(BlobIdentifier[] references)
        {
            References = references;
        }

        public BlobIdentifier[] References { get; set; }
    }
}
