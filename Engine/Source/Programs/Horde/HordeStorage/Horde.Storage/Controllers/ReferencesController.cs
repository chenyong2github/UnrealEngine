// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net.Mime;
using System.Text;
using System.Threading.Tasks;
using Dasync.Collections;
using Datadog.Trace;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Horde.Storage.Implementation;
using Jupiter;
using Jupiter.Common.Implementation;
using Jupiter.Implementation;
using Jupiter.Utils;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.Extensions;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using Newtonsoft.Json;
using Serilog;
using ContentHash = Jupiter.Implementation.ContentHash;
using Log = Serilog.Log;

namespace Horde.Storage.Controllers
{
    using BlobNotFoundException = Horde.Storage.Implementation.BlobNotFoundException;

    [ApiController]
    [FormatFilter]
    [Produces(MediaTypeNames.Application.Json, MediaTypeNames.Application.Octet, CustomMediaTypeNames.UnrealCompactBinary)]
    [Route("api/v1/refs")]
    public class ReferencesController : ControllerBase
    {
        private readonly IDiagnosticContext _diagnosticContext;
        private readonly IAuthorizationService _authorizationService;
        private readonly IOptionsMonitor<JupiterSettings> _jupiterSettings;
        private readonly FormatResolver _formatResolver;
        private readonly BufferedPayloadFactory _bufferedPayloadFactory;
        private readonly IReferenceResolver _referenceResolver;

        private readonly ILogger _logger = Log.ForContext<ReferencesController>();
        private readonly IObjectService _objectService;
        private readonly IBlobService _blobStore;

        public ReferencesController(IObjectService objectService, IBlobService blobStore, IDiagnosticContext diagnosticContext, IAuthorizationService authorizationService, IOptionsMonitor<JupiterSettings> jupiterSettings, FormatResolver formatResolver, BufferedPayloadFactory bufferedPayloadFactory, IReferenceResolver referenceResolver)
        {
            _objectService = objectService;
            _blobStore = blobStore;
            _diagnosticContext = diagnosticContext;
            _authorizationService = authorizationService;
            _jupiterSettings = jupiterSettings;
            _formatResolver = formatResolver;
            _bufferedPayloadFactory = bufferedPayloadFactory;
            _referenceResolver = referenceResolver;
        }

        /// <summary>
        /// Returns all the known namespace the token has access to
        /// </summary>
        /// <returns></returns>
        [HttpGet("")]
        [ProducesDefaultResponseType]
        [ProducesResponseType(type: typeof(ProblemDetails), 400)]
        [Authorize("Cache.read")]
        public async Task<IActionResult> GetNamespaces()
        {
            NamespaceId[] namespaces = await _objectService.GetNamespaces().ToArrayAsync();

            // filter namespaces down to only the namespaces the user has access to
            namespaces = namespaces.Where(ns =>
            {
                Task<AuthorizationResult> authorizationResult = _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);
                return authorizationResult.Result.Succeeded;
            }).ToArray();

            return Ok(new GetNamespacesResponse(namespaces));
        }

        /// <summary>
        /// Returns a refs key
        /// </summary>
        /// <param name="ns">Namespace. Each namespace is completely separated from each other. Use for different types of data that is never expected to be similar (between two different games for instance). Example: `uc4.ddc`</param>
        /// <param name="bucket">The category/type of record you are caching. Is a clustered key together with the actual key, but all records in the same bucket can be dropped easily. Example: `terrainTexture` </param>
        /// <param name="key">The unique name of this particular key. `iAmAVeryValidKey`</param>
        /// <param name="fields">The fields to include in the response, omit this to include everything.</param>
        /// <param name="format">Optional specifier to set which output format is used json/raw/cb</param>
        [HttpGet("{ns}/{bucket}/{key}.{format?}", Order = 500)]
        [Produces(MediaTypeNames.Application.Json, MediaTypeNames.Application.Octet, CustomMediaTypeNames.UnrealCompactBinary, CustomMediaTypeNames.JupiterInlinedPayload)]
        [Authorize("Object.read")]
        public async Task<IActionResult> Get(
            [FromRoute] [Required] NamespaceId ns,
            [FromRoute] [Required] BucketId bucket,
            [FromRoute] [Required] IoHashKey key,
            [FromQuery] string[] fields,
            [FromRoute] string? format = null)
        {
            {
                using IScope _ = Tracer.Instance.StartActive("authorize");
                AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

                if (!authorizationResult.Succeeded)
                {
                    return Forbid();
                }
            }

            try
            {
                (ObjectRecord objectRecord, BlobContents? blob) = await _objectService.Get(ns, bucket, key, fields);

                if (blob == null)
                    throw new InvalidOperationException($"Blob was null when attempting to fetch {ns} {bucket} {key}");

                if (!objectRecord.IsFinalized)
                {
                    // we do not consider un-finalized objects as valid
                    return BadRequest(new ProblemDetails { Title = $"Object {objectRecord.Bucket} {objectRecord.Name} is not finalized." });
                }

                Response.Headers[CommonHeaders.HashHeaderName] = objectRecord.BlobIdentifier.ToString();
                Response.Headers[CommonHeaders.LastAccessHeaderName] = objectRecord.LastAccess.ToString(CultureInfo.InvariantCulture);

                async Task WriteBody(BlobContents blobContents, string contentType)
                {
                    using IScope scope = Tracer.Instance.StartActive("body.write");
                    long contentLength = blobContents.Length;
                    scope.Span.SetTag("content-length", contentLength.ToString());
                    const int BufferSize = 64 * 1024;
                    var outputStream = Response.Body;
                    Response.ContentLength = contentLength;
                    Response.ContentType = contentType;
                    Response.StatusCode = StatusCodes.Status200OK;
                    await StreamCopyOperation.CopyToAsync(blobContents.Stream, outputStream, count: null, bufferSize: BufferSize, cancel: Response.HttpContext.RequestAborted);
                }

                string responseType = _formatResolver.GetResponseType(Request, format, CustomMediaTypeNames.UnrealCompactBinary);

                switch (responseType)
                {
                    case CustomMediaTypeNames.UnrealCompactBinary:
                    {
                        // for compact binary we can just serialize our internal object
                        await WriteBody(blob, CustomMediaTypeNames.UnrealCompactBinary);

                        break;
                    }
                    case MediaTypeNames.Application.Octet:
                    {
                        byte[] blobMemory = await blob.Stream.ToByteArray();
                        CbObject cb = new CbObject(blobMemory);
                        int countOfAttachmentFields = cb.Count(field => field.IsAttachment());
                        CbField? binaryAttachmentField = cb.FirstOrDefault(field => field.IsBinaryAttachment());
                        if (countOfAttachmentFields == 1 && binaryAttachmentField != null)
                        {
                            // there is a single attachment field and that is of the binary attachment type, fetch that attachment and return it instead of the compact binary
                            // this is so that we match the uploaded that happened as a octet-stream which generates a small cb object with a single attachment

                            IoHash hash = binaryAttachmentField.AsBinaryAttachment();

                            BlobContents referencedBlobContents = await _blobStore.GetObject(ns, BlobIdentifier.FromIoHash(hash));
                            await WriteBody(referencedBlobContents, MediaTypeNames.Application.Octet);
                            break;
                        }

                        // this doesn't look like the generated compact binary so we just return the payload
                        await WriteBody(new BlobContents(blobMemory), MediaTypeNames.Application.Octet);
                        break;
                    }
                    case MediaTypeNames.Application.Json:
                    {
                        byte[] blobMemory;
                        {
                            using IScope scope = Tracer.Instance.StartActive("json.readblob");
                            blobMemory = await blob.Stream.ToByteArray();
                        }
                        CbObject cb = new CbObject(blobMemory);
                        string s = cb.ToJson();
                        await WriteBody(new BlobContents(Encoding.UTF8.GetBytes(s)), MediaTypeNames.Application.Json);
                        break;

                    }
                    case CustomMediaTypeNames.JupiterInlinedPayload:
                    {

                        byte[] blobMemory = await blob.Stream.ToByteArray();
                        CbObject cb = new CbObject(blobMemory);
                        List<CbField> compactBinaryFields = cb.ToList();
                        int countOfBinaryAttachmentFields = compactBinaryFields.Count(field => field.IsBinaryAttachment());
                        int countOfAttachmentFields = compactBinaryFields.Count(field => field.IsAttachment());
                        // if the object consists of a single attachment field we return this attachment field instead
                        if (countOfBinaryAttachmentFields == 1 && countOfAttachmentFields == 1)
                        {
                            // fetch the blob so we can resolve any content ids in it
                            List<BlobIdentifier> referencedBlobs;
                            try
                            {
                                IAsyncEnumerable<BlobIdentifier> referencedBlobsEnumerable = _referenceResolver.ResolveReferences(ns, cb);
                                referencedBlobs = await referencedBlobsEnumerable.ToListAsync();
                            }
                            catch (PartialReferenceResolveException)
                            {
                                return NotFound(new ProblemDetails {Title = $"Object {bucket} {key} in namespace {ns} was missing some content ids"});
                            }
                            catch (ReferenceIsMissingBlobsException)
                            {
                                return NotFound(new ProblemDetails {Title = $"Object {bucket} {key} in namespace {ns} was missing some blobs"});
                            }

                            if (referencedBlobs.Count == 1)
                            {
                                BlobIdentifier attachmentToSend = referencedBlobs.First();
                                BlobContents referencedBlobContents = await _blobStore.GetObject(ns, attachmentToSend);
                                Response.Headers[CommonHeaders.InlinePayloadHash] = attachmentToSend.ToString();

                                await WriteBody(referencedBlobContents, CustomMediaTypeNames.JupiterInlinedPayload);
                                return new EmptyResult();
                            }
                            else if (referencedBlobs.Count == 0)
                            {
                                return NotFound(new ProblemDetails
                                {
                                    Title =
                                        $"Object {objectRecord.Bucket} {objectRecord.Name} did not resolve into any objects that we could find."
                                });
                            }

                            return BadRequest(new ProblemDetails
                            {
                                Title =
                                    $"Object {objectRecord.Bucket} {objectRecord.Name} contained a content id which resolved to more then 1 blob, unable to inline this object. Use compact object response instead."
                            });
                        }
                        else if (countOfBinaryAttachmentFields == 0 && countOfAttachmentFields == 0)
                        {
                            // no attachments so we just return the compact object instead
                            await WriteBody(new BlobContents(blobMemory), CustomMediaTypeNames.JupiterInlinedPayload);
                            return new EmptyResult();
                        }

                        return BadRequest(new ProblemDetails
                        {
                            Title =
                                $"Object {objectRecord.Bucket} {objectRecord.Name} had more then 1 binary attachment field, unable to inline this object. Use compact object response instead."
                        });
                    }
                    default:
                        throw new NotImplementedException($"Unknown expected response type {responseType}");
                }
                
                // this result is ignored as we write to the body explicitly
                return new EmptyResult();

            }
            catch (NamespaceNotFoundException e)
            {
                return NotFound(new ProblemDetails {Title = $"Namespace {e.Namespace} did not exist"});
            }
            catch (ObjectNotFoundException e)
            {
                return NotFound(new ProblemDetails { Title = $"Object {e.Bucket} {e.Key} did not exist" });
            }
            catch (BlobNotFoundException e)
            {
                return NotFound(new ProblemDetails { Title = $"Object {e.Blob} in {e.Ns} not found" });
            }
        }

          /// <summary>
        /// Returns the metadata about a ref key
        /// </summary>
        /// <param name="ns">Namespace. Each namespace is completely separated from each other. Use for different types of data that is never expected to be similar (between two different games for instance). Example: `uc4.ddc`</param>
        /// <param name="bucket">The category/type of record you are caching. Is a clustered key together with the actual key, but all records in the same bucket can be dropped easily. Example: `terrainTexture` </param>
        /// <param name="key">The unique name of this particular key. `iAmAVeryValidKey`</param>
        /// <param name="fields">The fields to include in the response, omit this to include everything.</param>
        [HttpGet("{ns}/{bucket}/{key}/metadata", Order = 500)]
        [Authorize("Object.read")]
        public async Task<IActionResult> GetMetadata(
            [FromRoute] [Required] NamespaceId ns,
            [FromRoute] [Required] BucketId bucket,
            [FromRoute] [Required] IoHashKey key,
            [FromQuery] string[] fields)
        {
            {
                using IScope _ = Tracer.Instance.StartActive("authorize");
                AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

                if (!authorizationResult.Succeeded)
                {
                    return Forbid();
                }
            }

            try
            {
                (ObjectRecord objectRecord, BlobContents? _) = await _objectService.Get(ns, bucket, key, fields);

                return Ok(new RefMetadataResponse(objectRecord));
            }
            catch (NamespaceNotFoundException e)
            {
                return NotFound(new ProblemDetails {Title = $"Namespace {e.Namespace} did not exist"});
            }
            catch (ObjectNotFoundException e)
            {
                return NotFound(new ProblemDetails { Title = $"Object {e.Bucket} {e.Key} did not exist" });
            }
            catch (BlobNotFoundException e)
            {
                return NotFound(new ProblemDetails { Title = $"Object {e.Blob} in {e.Ns} not found" });
            }
        }


        /// <summary>
        /// Checks if a object exists
        /// </summary>
        /// <param name="ns">Namespace. Each namespace is completely separated from each other. Use for different types of data that is never expected to be similar (between two different games for instance). Example: `uc4.ddc`</param>
        /// <param name="bucket">The category/type of record you are caching. Is a clustered key together with the actual key, but all records in the same bucket can be dropped easily. Example: `terrainTexture` </param>
        /// <param name="key">The unique name of this particular key. `iAmAVeryValidKey`</param>
        /// <returns>200 if it existed, 400 otherwise</returns>
        [HttpHead("{ns}/{bucket}/{key}", Order = 500)]
        [ProducesResponseType(type: typeof(RefResponse), 200)]
        [ProducesResponseType(type: typeof(ValidationProblemDetails), 400)]
        [Authorize("Object.read")]
        public async Task<IActionResult> Head(
            [FromRoute] [Required] NamespaceId ns,
            [FromRoute] [Required] BucketId bucket,
            [FromRoute] [Required] IoHashKey key)
        {
            {
                using IScope _ = Tracer.Instance.StartActive("authorize");
                AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

                if (!authorizationResult.Succeeded)
                {
                    return Forbid();
                }
            }

            try
            {
                (ObjectRecord record, BlobContents? blob) = await _objectService.Get(ns, bucket, key, new string[] {"blobIdentifier", "IsFinalized"});
                Response.Headers[CommonHeaders.HashHeaderName] = record.BlobIdentifier.ToString();

                if (!record.IsFinalized)
                {
                    return NotFound(new ProblemDetails {Title = $"Object {bucket} {key} in namespace {ns} is not finalized."});
                }

                blob ??= await _blobStore.GetObject(ns, record.BlobIdentifier);

                // we have to verify the blobs are available locally, as the record of the key is replicated a head of the content
                // TODO: Once we support inline replication this step is not needed as at least one region as this blob, just maybe not this current one
                byte[] blobContents = await blob.Stream.ToByteArray();
                CbObject compactBinaryObject = new CbObject(blobContents);
                // the reference resolver will throw if any blob is missing, so no need to do anything other then process each reference
                IAsyncEnumerable<BlobIdentifier> references = _referenceResolver.ResolveReferences(ns, compactBinaryObject);
                List<BlobIdentifier>? _ = await references.ToListAsync();

                // we have to verify the blobs are available locally, as the record of the key is replicated a head of the content
                // TODO: Once we support inline replication this step is not needed as at least one region as this blob, just maybe not this current one
                BlobIdentifier[] unknownBlobs = await _blobStore.FilterOutKnownBlobs(ns, new BlobIdentifier[] { record.BlobIdentifier });
                if (unknownBlobs.Length != 0)
                {
                    return NotFound(new ProblemDetails {Title = $"Object {bucket} {key} in namespace {ns} had at least one missing blob."});
                }
            }
            catch (NamespaceNotFoundException e)
            {
                return NotFound(new ProblemDetails {Title = $"Namespace {e.Namespace} did not exist"});
            }
            catch (ObjectNotFoundException e)
            {
                return NotFound(new ProblemDetails {Title = $"Object {e.Bucket} {e.Key} in namespace {e.Namespace} did not exist"});
            }
            catch (MissingBlobsException e)
            {
                return NotFound(new ProblemDetails { Title = $"Blobs {e.Blobs} from object {e.Bucket} {e.Key} in namespace {e.Namespace} did not exist" });
            }
            catch (PartialReferenceResolveException)
            {
                return NotFound(new ProblemDetails {Title = $"Object {bucket} {key} in namespace {ns} was missing some content ids"});
            }
            catch (ReferenceIsMissingBlobsException)
            {
                return NotFound(new ProblemDetails {Title = $"Object {bucket} {key} in namespace {ns} was missing some blobs"});
            }

            return Ok();
        }

        [HttpPost("{ns}/exists")]
        [Authorize("Object.read")]
        [ProducesDefaultResponseType]
        public async Task<IActionResult> ExistsMultiple(
            [FromRoute] [Required] NamespaceId ns,
            [FromRoute] [Required] BucketId bucket,
            [FromQuery] [Required] List<IoHashKey> names)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            ConcurrentBag<IoHashKey> missingObject = new ConcurrentBag<IoHashKey>();

            IEnumerable<Task> tasks = names.Select(async name =>
            {
                try
                {
                    (ObjectRecord record, BlobContents? blob) =
                        await _objectService.Get(ns, bucket, name, new string[] { "blobIdentifier" });

                    blob ??= await _blobStore.GetObject(ns, record.BlobIdentifier);

                    // we have to verify the blobs are available locally, as the record of the key is replicated a head of the content
                    // TODO: Once we support inline replication this step is not needed as at least one region as this blob, just maybe not this current one
                    byte[] blobContents = await blob.Stream.ToByteArray();
                    CbObject cb = new CbObject(blobContents);
                    // the reference resolver will throw if any blob is missing, so no need to do anything other then process each reference
                    IAsyncEnumerable<BlobIdentifier> references = _referenceResolver.ResolveReferences(ns, cb);
                    List<BlobIdentifier>? _ = await references.ToListAsync();
                }
                catch (ObjectNotFoundException)
                {
                    missingObject.Add(name);
                }
                catch (PartialReferenceResolveException)
                {
                    missingObject.Add(name);
                }
                catch (ReferenceIsMissingBlobsException)
                {
                    missingObject.Add(name);
                }
            });
            await Task.WhenAll(tasks);

            return Ok(new ExistCheckMultipleRefsResponse(missingObject.ToList()));
        }


        [HttpPut("{ns}/{bucket}/{key}.{format?}", Order = 500)]
        [DisableRequestSizeLimit]
        [Authorize("Object.write")]
        public async Task<IActionResult> PutObject(
            [FromRoute] [Required] NamespaceId ns,
            [FromRoute] [Required] BucketId bucket,
            [FromRoute] [Required] IoHashKey key)
        {
            {
                using IScope _ = Tracer.Instance.StartActive("authorize");
                AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

                if (!authorizationResult.Succeeded)
                {
                    return Forbid();
                }
            }

            _diagnosticContext.Set("Content-Length", Request.ContentLength ?? -1);
            
            using IBufferedPayload payload = await _bufferedPayloadFactory.CreateFromRequest(Request);

            BlobIdentifier headerHash;
            if (Request.Headers.ContainsKey(CommonHeaders.HashHeaderName))
            {
                headerHash = new BlobIdentifier(Request.Headers[CommonHeaders.HashHeaderName]);
            }
            else
            {
                return BadRequest(new ProblemDetails
                {
                    Title = $"Missing expected header {CommonHeaders.HashHeaderName}"
                });
            }

            CbObject payloadObject;
            BlobIdentifier blobHeader = headerHash;
            try
            {
                switch (Request.ContentType)
                {
                    case MediaTypeNames.Application.Json:
                    {
                        // TODO: define a scheme for how a json object specifies references

                        blobHeader = await _blobStore.PutObject(ns, payload, headerHash);

                        // TODO: convert the json object into a compact binary instead
                        CbWriter writer = new CbWriter();
                        writer.BeginObject();
                        writer.WriteBinaryAttachmentValue(blobHeader.AsIoHash());
                        writer.EndObject();

                        byte[] blob = writer.ToByteArray();
                        payloadObject = new CbObject(blob);
                        blobHeader = BlobIdentifier.FromBlob(blob);
                        break;
                    }
                    case CustomMediaTypeNames.UnrealCompactBinary:
                    {
                        MemoryStream ms = new MemoryStream();
                        await using Stream payloadStream = payload.GetStream();
                        await payloadStream.CopyToAsync(ms);
                        payloadObject = new CbObject(ms.ToArray());
                        break;
                    }
                    case MediaTypeNames.Application.Octet:
                    {
                        blobHeader = await _blobStore.PutObject(ns, payload, headerHash);

                        CbWriter writer = new CbWriter();
                        writer.BeginObject();
                        writer.WriteBinaryAttachment("RawHash", blobHeader.AsIoHash());
                        writer.WriteInteger("RawSize", payload.Length);
                        writer.EndObject();

                        byte[] blob = writer.ToByteArray();
                        payloadObject = new CbObject(blob);
                        blobHeader = BlobIdentifier.FromBlob(blob);
                        break;
                    }
                    default:
                        throw new Exception($"Unknown request type {Request.ContentType}, if submitting a blob please use {MediaTypeNames.Application.Octet}");
                }
            }
            catch (HashMismatchException e)
            {
                return BadRequest(new ProblemDetails
                {
                    Title = $"Incorrect hash, got hash \"{e.SuppliedHash}\" but hash of content was determined to be \"{e.ContentHash}\""
                });
            }


            (ContentId[] missingReferences, BlobIdentifier[] missingBlobs) = await _objectService.Put(ns, bucket, key, blobHeader, payloadObject);

            List<ContentHash> missingHashes = new List<ContentHash>(missingReferences);
            missingHashes.AddRange(missingBlobs);
            return Ok(new PutObjectResponse(missingHashes.ToArray()));
        }

        [HttpPost("{ns}/{bucket}/{key}/finalize/{hash}.{format?}")]
        [Authorize("Object.write")]
        public async Task<IActionResult> FinalizeObject(
            [FromRoute] [Required] NamespaceId ns,
            [FromRoute] [Required] BucketId bucket,
            [FromRoute] [Required] IoHashKey key,
            [FromRoute] [Required] BlobIdentifier hash)
        {
            {
                using IScope _ = Tracer.Instance.StartActive("authorize");
                AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

                if (!authorizationResult.Succeeded)
                {
                    return Forbid();
                }
            }

            (ContentId[] missingReferences, BlobIdentifier[] missingBlobs) = await _objectService.Finalize(ns, bucket, key, hash);
            List<ContentHash> missingHashes = new List<ContentHash>(missingReferences);
            missingHashes.AddRange(missingBlobs);

            return Ok(new PutObjectResponse(missingHashes.ToArray()));
        }


        
        /// <summary>
        /// Drop all refs records in the namespace
        /// </summary>
        /// <param name="ns">Namespace. Each namespace is completely separated from each other. Use for different types of data that is never expected to be similar (between two different games for instance)</param>
        [HttpDelete("{ns}", Order = 500)]
        [ProducesResponseType(204)]
        [Authorize("admin")]
        public async Task<IActionResult> DeleteNamespace(
            [FromRoute] [Required] NamespaceId ns
        )
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            try
            {
                await _objectService.DropNamespace(ns);
            }
            catch (NamespaceNotFoundException e)
            {
                return NotFound(new ProblemDetails {Title = $"Namespace {e.Namespace} did not exist"});
            }


            return NoContent();
        }

        /// <summary>
        /// Drop all refs records in the bucket
        /// </summary>
        /// <param name="ns">Namespace. Each namespace is completely separated from each other. Use for different types of data that is never expected to be similar (between two different games for instance)</param>
        /// <param name="bucket">The category/type of record you are caching. Is a clustered key together with the actual key, but all records in the same bucket can be dropped easily.</param>
        [HttpDelete("{ns}/{bucket}", Order = 500)]
        [ProducesResponseType(204)]
        [Authorize("Object.delete")]
        public async Task<IActionResult> DeleteBucket(
            [FromRoute] [Required] NamespaceId ns,
            [FromRoute] [Required] BucketId bucket)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            long countOfDeletedRecords = 0;
            try
            {
                countOfDeletedRecords = await _objectService.DeleteBucket(ns, bucket);
            }
            catch (NamespaceNotFoundException e)
            {
                return NotFound(new ProblemDetails {Title = $"Namespace {e.Namespace} did not exist"});
            }


            return Ok(new BucketDeletedResponse(countOfDeletedRecords));
        }

        /// <summary>
        /// Delete a individual refs key
        /// </summary>
        /// <param name="ns">Namespace. Each namespace is completely separated from each other. Use for different types of data that is never expected to be similar (between two different games for instance)</param>
        /// <param name="bucket">The category/type of record you are caching. Is a clustered key together with the actual key, but all records in the same bucket can be dropped easily.</param>
        /// <param name="key">The unique name of this particular key</param>
        [HttpDelete("{ns}/{bucket}/{key}", Order = 500)]
        [ProducesResponseType(204)]
        [ProducesResponseType(400)]
        [Authorize("Object.delete")]
        public async Task<IActionResult> Delete(
            [FromRoute] [Required] NamespaceId ns,
            [FromRoute] [Required] BucketId bucket,
            [FromRoute] [Required] IoHashKey key)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            try
            {
                bool deleted = await _objectService.Delete(ns, bucket, key);
                return Ok(new RefDeletedResponse(deleted ? 1: 0));
            }
            catch (NamespaceNotFoundException e)
            {
                return NotFound(new ProblemDetails {Title = $"Namespace {e.Namespace} did not exist"});
            }
        }
    }

    public class RefDeletedResponse
    {
        public RefDeletedResponse()
        {

        }

        public RefDeletedResponse(int deletedCount)
        {
            DeletedCount = deletedCount;
        }

        [CbField("deletedCount")]
        public int DeletedCount { get; set; }
    }

    public class BucketDeletedResponse
    {
        public BucketDeletedResponse()
        {

        }

        public BucketDeletedResponse(long countOfDeletedRecords)
        {
            CountOfDeletedRecords = countOfDeletedRecords;
        }

        [CbField("countOfDeletedRecords")]
        public long CountOfDeletedRecords { get; set; }
    }

    public class RefMetadataResponse
    {
        public RefMetadataResponse()
        {
            PayloadIdentifier = null!;
            InlinePayload = null!;
        }

        [JsonConstructor]
        public RefMetadataResponse(NamespaceId ns, BucketId bucket, IoHashKey name, BlobIdentifier payloadIdentifier, DateTime lastAccess, bool isFinalized, byte[]? inlinePayload)
        {
            Ns = ns;
            Bucket = bucket;
            Name = name;
            PayloadIdentifier = payloadIdentifier;
            LastAccess = lastAccess;
            IsFinalized = isFinalized;
            InlinePayload = inlinePayload;
        }

        public RefMetadataResponse(ObjectRecord objectRecord)
        {
            Ns = objectRecord.Namespace;
            Bucket = objectRecord.Bucket;
            Name = objectRecord.Name;
            PayloadIdentifier = objectRecord.BlobIdentifier;
            LastAccess = objectRecord.LastAccess;
            IsFinalized = objectRecord.IsFinalized;
            InlinePayload = objectRecord.InlinePayload;
        }

        [CbField("ns")]
        public NamespaceId Ns { get; set; }
        
        [CbField("bucket")]
        public BucketId Bucket { get; set; }

        [CbField("name")]
        public IoHashKey Name { get; set; }

        [CbField("payloadIdentifier")]
        public BlobIdentifier PayloadIdentifier { get; set; }

        [CbField("lastAccess")]
        public DateTime LastAccess { get; set; }

        [CbField("isFinalized")]
        public bool IsFinalized { get; set; }

        [CbField("inlinePayload")]
        public byte[]? InlinePayload { get; set; }
    }

    public class PutObjectResponse
    {
        public PutObjectResponse()
        {
            Needs = null!;
        }

        public PutObjectResponse(ContentHash[] missingReferences)
        {
            Needs = missingReferences;
        }

        [CbField("needs")]
        public ContentHash[] Needs { get; set; }
    }

    public class ExistCheckMultipleRefsResponse
    {
        public ExistCheckMultipleRefsResponse(List<IoHashKey> missingNames)
        {
            Needs = missingNames;
            Names = missingNames;
        }

        [CbField("needs")]
        public List<IoHashKey> Needs { get; set; }

        [CbField("names")]
        public List<IoHashKey> Names { get; set; }
    }
}
