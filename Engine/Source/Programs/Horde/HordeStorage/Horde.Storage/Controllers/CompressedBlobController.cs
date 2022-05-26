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
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Horde.Storage.Implementation;
using Jupiter;
using Jupiter.Common.Implementation;
using Jupiter.Implementation;
using Jupiter.Utils;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Primitives;
using Serilog;

namespace Horde.Storage.Controllers
{
    using BlobNotFoundException = Horde.Storage.Implementation.BlobNotFoundException;

    [ApiController]
    [Route("api/v1/compressed-blobs")]
    public class CompressedBlobController : ControllerBase
    {
        private readonly IBlobService _storage;
        private readonly IContentIdStore _contentIdStore;
        private readonly IDiagnosticContext _diagnosticContext;
        private readonly RequestHelper _requestHelper;
        private readonly CompressedBufferUtils _compressedBufferUtils;
        private readonly BufferedPayloadFactory _bufferedPayloadFactory;

        public CompressedBlobController(IBlobService storage, IContentIdStore contentIdStore, IDiagnosticContext diagnosticContext, RequestHelper requestHelper, CompressedBufferUtils compressedBufferUtils, BufferedPayloadFactory bufferedPayloadFactory)
        {
            _storage = storage;
            _contentIdStore = contentIdStore;
            _diagnosticContext = diagnosticContext;
            _requestHelper = requestHelper;
            _compressedBufferUtils = compressedBufferUtils;
            _bufferedPayloadFactory = bufferedPayloadFactory;
        }

        [HttpGet("{ns}/{id}")]
        [Authorize("Storage.read")]
        [ProducesResponseType(type: typeof(byte[]), 200)]
        [ProducesResponseType(type: typeof(ValidationProblemDetails), 400)]
        [Produces(CustomMediaTypeNames.UnrealCompressedBuffer, MediaTypeNames.Application.Octet)]

        public async Task<IActionResult> Get(
            [Required] NamespaceId ns,
            [Required] ContentId id)
        {
            ActionResult? result = await _requestHelper.HasAccessToNamespace(User, Request, ns);
            if (result != null)
            {
                return result;
            }

            try
            {
                (BlobContents blobContents, string mediaType) = await GetImpl(ns, id);

                StringValues acceptHeader = Request.Headers["Accept"];
                if (!acceptHeader.Contains("*/*") && acceptHeader.Count != 0 && !acceptHeader.Contains(mediaType))
                {
                    return new UnsupportedMediaTypeResult();
                }

                return File(blobContents.Stream, mediaType, enableRangeProcessing: true);
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
            [Required] ContentId id)
        {
            ActionResult? result = await _requestHelper.HasAccessToNamespace(User, Request, ns);
            if (result != null)
            {
                return result;
            }

            BlobIdentifier[]? chunks = await _contentIdStore.Resolve(ns, id, mustBeContentId: false);
            if (chunks == null || chunks.Length == 0)
            {
                return NotFound();
            }

            Task<bool>[] tasks = new Task<bool>[chunks.Length];
            for (int i = 0; i < chunks.Length; i++)
            {
                tasks[i] = _storage.Exists(ns, chunks[i]);
            }

            await Task.WhenAll(tasks);

            bool exists = tasks.All(task => task.Result);

            if (!exists)
            {
                return NotFound();
            }

            return Ok();
        }

        [HttpPost("{ns}/exists")]
        [Authorize("Storage.read")]
        [ProducesDefaultResponseType]
        public async Task<IActionResult> ExistsMultiple(
            [Required] NamespaceId ns,
            [Required] [FromQuery] List<ContentId> id)
        {
            ActionResult? result = await _requestHelper.HasAccessToNamespace(User, Request, ns);
            if (result != null)
            {
                return result;
            }

            ConcurrentBag<ContentId> partialContentIds = new ConcurrentBag<ContentId>();
            ConcurrentBag<ContentId> invalidContentIds = new ConcurrentBag<ContentId>();

            IEnumerable<Task> tasks = id.Select(async blob =>
            {
                BlobIdentifier[]? chunks = await _contentIdStore.Resolve(ns, blob, mustBeContentId: false);

                if (chunks == null)
                {
                    invalidContentIds.Add(blob);
                    return;
                }

                foreach (BlobIdentifier chunk in chunks)
                {
                    if (!await _storage.Exists(ns, chunk))
                    {
                        partialContentIds.Add(blob);
                        break;
                    }
                }
            });
            await Task.WhenAll(tasks);

            List<ContentId> needs = new List<ContentId>(invalidContentIds);
            needs.AddRange(partialContentIds);
             
            return Ok(new ExistCheckMultipleContentIdResponse { Needs = needs.ToArray()});
        }

        [HttpPost("{ns}/exist")]
        [Authorize("Storage.read")]
        [ProducesDefaultResponseType]
        public async Task<IActionResult> ExistsBody(
            [Required] NamespaceId ns,
            [FromBody] ContentId[] bodyIds)
        {
            ActionResult? result = await _requestHelper.HasAccessToNamespace(User, Request, ns);
            if (result != null)
            {
                return result;
            }

            ConcurrentBag<ContentId> partialContentIds = new ConcurrentBag<ContentId>();
            ConcurrentBag<ContentId> invalidContentIds = new ConcurrentBag<ContentId>();

            IEnumerable<Task> tasks = bodyIds.Select(async blob =>
            {
                BlobIdentifier[]? chunks = await _contentIdStore.Resolve(ns, blob, mustBeContentId: false);

                if (chunks == null)
                {
                    invalidContentIds.Add(blob);
                    return;
                }

                foreach (BlobIdentifier chunk in chunks)
                {
                    if (!await _storage.Exists(ns, chunk))
                    {
                        partialContentIds.Add(blob);
                        break;
                    }
                }
            });
            await Task.WhenAll(tasks);

            List<ContentId> needs = new List<ContentId>(invalidContentIds);
            needs.AddRange(partialContentIds);
             
            return Ok(new ExistCheckMultipleContentIdResponse { Needs = needs.ToArray()});
        }

        private async Task<(BlobContents, string)> GetImpl(NamespaceId ns, ContentId contentId)
        {
            BlobIdentifier[]? chunks = await _contentIdStore.Resolve(ns, contentId, mustBeContentId: false);
            if (chunks == null || chunks.Length == 0)
            {
                throw new ContentIdResolveException(contentId);
            }

            // single chunk, we just return that chunk
            if (chunks.Length == 1)
            {
                BlobIdentifier blobToReturn = chunks[0];
                string mimeType = CustomMediaTypeNames.UnrealCompressedBuffer;
                if (contentId.Equals(blobToReturn))
                {
                    // this was actually the unmapped blob, meaning its not a compressed buffer
                    mimeType = MediaTypeNames.Application.Octet;
                }

                return (await _storage.GetObject(ns, blobToReturn), mimeType);
            }

            // chunked content, combine the chunks into a single stream
            using IScope _ = Tracer.Instance.StartActive("blob.combine");
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

            // chunking could not have happened for a non compressed buffer so assume it is compressed
            return (new BlobContents(ms, ms.Length), CustomMediaTypeNames.UnrealCompressedBuffer);
        }

        [HttpPut("{ns}/{id}")]
        [Authorize("Storage.write")]
        [DisableRequestSizeLimit]
        [RequiredContentType(CustomMediaTypeNames.UnrealCompressedBuffer)]
        public async Task<IActionResult> Put(
            [Required] NamespaceId ns,
            [Required] ContentId id)
        {
            ActionResult? result = await _requestHelper.HasAccessToNamespace(User, Request, ns);
            if (result != null)
            {
                return result;
            }

            _diagnosticContext.Set("Content-Length", Request.ContentLength ?? -1);

            using IBufferedPayload payload = await _bufferedPayloadFactory.CreateFromRequest(Request);

            try
            {
                ContentId identifier = await PutImpl(ns, id, payload);

                return Ok(new
                {
                    Identifier = identifier.ToString()
                });
            }
            catch (HashMismatchException e)
            {
                return BadRequest(new ProblemDetails
                {
                    Title = $"Incorrect hash, got hash \"{e.SuppliedHash}\" but hash of content was determined to be \"{e.ContentHash}\""
                });
            }
        }

        [HttpPost("{ns}")]
        [Authorize("Storage.write")]
        [DisableRequestSizeLimit]
        [RequiredContentType(CustomMediaTypeNames.UnrealCompressedBuffer)]
        public async Task<IActionResult> Post(
            [Required] NamespaceId ns)
        {
            ActionResult? result = await _requestHelper.HasAccessToNamespace(User, Request, ns);
            if (result != null)
            {
                return result;
            }

            _diagnosticContext.Set("Content-Length", Request.ContentLength ?? -1);

            using IBufferedPayload payload = await _bufferedPayloadFactory.CreateFromRequest(Request);

            try
            {
                ContentId identifier = await PutImpl(ns, null, payload);

                return Ok(new
                {
                    Identifier = identifier.ToString()
                });
            }
            catch (HashMismatchException e)
            {
                return BadRequest(new ProblemDetails
                {
                    Title = $"Incorrect hash, got hash \"{e.SuppliedHash}\" but hash of content was determined to be \"{e.ContentHash}\""
                });
            }
        }

        private async Task<ContentId> PutImpl(NamespaceId ns, ContentId? id, IBufferedPayload payload)
        {
            // decompress the content and generate a identifier from it to verify the identifier we got if we got one
            await using Stream decompressStream = payload.GetStream();
            // TODO: we should add a overload for decompress content that can work on streams, otherwise we are still limited to 2GB compressed blobs
            byte[] decompressedContent = _compressedBufferUtils.DecompressContent(await decompressStream.ToByteArray());

            await using MemoryStream decompressedStream = new MemoryStream(decompressedContent);
            ContentId identifierDecompressedPayload;
            if (id != null)
            {
                identifierDecompressedPayload = ContentId.FromContentHash(await _storage.VerifyContentMatchesHash(decompressedStream, id));
            }
            else
            {
                ContentHash blobHash;
                {
                    using IScope _ = Tracer.Instance.StartActive("web.hash");
                    blobHash = await BlobIdentifier.FromStream(decompressedStream);
                }

                identifierDecompressedPayload = ContentId.FromContentHash(blobHash);
            }

            BlobIdentifier identifierCompressedPayload;
            {
                using IScope _ = Tracer.Instance.StartActive("web.hash");
                await using Stream hashStream = payload.GetStream();
                identifierCompressedPayload = await BlobIdentifier.FromStream(hashStream);
            }

            // commit the mapping from the decompressed hash to the compressed hash, we run this in parallel with the blob store submit
            // TODO: let users specify weight of the blob compared to previously submitted content ids
            int contentIdWeight = (int)payload.Length;
            Task contentIdStoreTask = _contentIdStore.Put(ns, identifierDecompressedPayload, identifierCompressedPayload, contentIdWeight);

            // we still commit the compressed buffer to the object store using the hash of the compressed content
            {
                await _storage.PutObjectKnownHash(ns, payload, identifierCompressedPayload);
            }

            await contentIdStoreTask;

            return identifierDecompressedPayload;
        }

        /*[HttpDelete("{ns}/{id}")]
        [Authorize("Storage.delete")]
        public async Task<IActionResult> Delete(
            [Required] string ns,
            [Required] BlobIdentifier id)
        {
            ActionResult? result = await _requestHelper.HasAccessToNamespace(User, Request, ns);
            if (result != null)
            {
                return result;
            }

            await DeleteImpl(ns, id);

            return Ok();
        }

        
        [HttpDelete("{ns}")]
        [Authorize("Admin")]
        public async Task<IActionResult> DeleteNamespace(
            [Required] string ns)
        {
            ActionResult? result = await _requestHelper.HasAccessToNamespace(User, Request, ns);
            if (result != null)
            {
                return result;
            }

            int deletedCount = await  _storage.DeleteNamespace(ns);

            return Ok( new { Deleted = deletedCount });
        }


        private async Task DeleteImpl(string ns, BlobIdentifier id)
        {
            await _storage.DeleteObject(ns, id);
        }*/
    }

    public class ExistCheckMultipleContentIdResponse
    {
		[CbField("needs")]
		public ContentId[] Needs { get; set; } = null!;
    }
}
