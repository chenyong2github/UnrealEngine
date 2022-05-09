// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Net.Mime;
using System.Threading.Tasks;
using async_enumerable_dotnet;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Horde.Storage.Implementation;
using Jupiter;
using Jupiter.Common.Implementation;
using Jupiter.Implementation;
using Jupiter.Utils;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Serilog;

namespace Horde.Storage.Controllers
{
    using BlobNotFoundException = Horde.Storage.Implementation.BlobNotFoundException;

    [ApiController]
    [Route("api/v1/objects", Order = 0)]
    [Produces(CustomMediaTypeNames.UnrealCompactBinary, MediaTypeNames.Application.Json)]
    public class ObjectController : ControllerBase
    {
        private readonly IBlobService _storage;
        private readonly IDiagnosticContext _diagnosticContext;
        private readonly IAuthorizationService _authorizationService;
        private readonly IReferenceResolver _referenceResolver;
        private readonly BufferedPayloadFactory _bufferedPayloadFactory;

        private readonly ILogger _logger = Log.ForContext<ObjectController>();

        public ObjectController(IBlobService storage, IDiagnosticContext diagnosticContext, IAuthorizationService authorizationService, IReferenceResolver referenceResolver, BufferedPayloadFactory bufferedPayloadFactory)
        {
            _storage = storage;
            _diagnosticContext = diagnosticContext;
            _authorizationService = authorizationService;
            _referenceResolver = referenceResolver;
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
                {
                    missingBlobs.Add(blob);
                }
            });
            await Task.WhenAll(tasks);

            return Ok(new HeadMultipleResponse {Needs = missingBlobs.ToArray()});
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

            return Ok(new HeadMultipleResponse { Needs = missingBlobs.ToArray() });
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

            _diagnosticContext.Set("Content-Length", Request.ContentLength ?? -1);
            using IBufferedPayload payload = await _bufferedPayloadFactory.CreateFromRequest(Request);

            BlobIdentifier identifier = await _storage.PutObject(ns, payload, id);
            return Ok(new PutBlobResponse(identifier));
        }

        [HttpGet("{ns}/{id}/references")]
        [Authorize("Storage.read")]
        public async Task<IActionResult> ResolveReferences(
            [Required] NamespaceId ns,
            [Required] BlobIdentifier id)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            BlobContents blob;
            try
            {
                blob = await _storage.GetObject(ns, id);
            }
            catch (BlobNotFoundException e)
            {
                return NotFound(new ValidationProblemDetails {Title = $"Object {e.Blob} not found"});
            }
           
            byte[] blobContents = await blob.Stream.ToByteArray();
            if (blobContents.Length == 0)
            {
                _logger.Warning("0 byte object found for {Id} {Namespace}", id, ns);
            }

            CbObject compactBinaryObject;
            try
            {
                compactBinaryObject = new CbObject(blobContents);
            }
            catch (IndexOutOfRangeException)
            {
                return Problem(title: $"{id} was not a proper compact binary object.", detail: "Index out of range");
            }

            try
            {
                BlobIdentifier[] references = await _referenceResolver.ResolveReferences(ns, compactBinaryObject).ToArrayAsync();
                return Ok(new ResolvedReferencesResult(references));
            }
            catch (PartialReferenceResolveException e)
            {
                return BadRequest(new ValidationProblemDetails {Title = $"Object {id} is missing content ids", Detail = $"Following content ids are invalid: {string.Join(",", e.UnresolvedReferences)}"});
            }
            catch (ReferenceIsMissingBlobsException e)
            {
                return BadRequest(new ValidationProblemDetails {Title = $"Object {id} is missing blobs", Detail = $"Following blobs are missing: {string.Join(",", e.MissingBlobs)}"});
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

    public class PutBlobResponse
    {
        public PutBlobResponse()
        {
            Identifier = null!;
        }

        public PutBlobResponse(BlobIdentifier identifier)
        {
            Identifier = identifier;
        }

        [CbField("identifier")]
        public BlobIdentifier Identifier { get; set; }
    }

    public class DeletedResponse
    {
        public int DeletedCount { get; set; }
    }

    public class ResolvedReferencesResult
    {
        public ResolvedReferencesResult()
        {
            References = null!;
        }

        public ResolvedReferencesResult(BlobIdentifier[] references)
        {
            References = references;
        }

        [CbField("references")]
        public BlobIdentifier[] References { get; set; }
    }
}
