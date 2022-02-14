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
using EpicGames.Horde.Storage;
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

        private readonly ILogger _logger = Log.ForContext<ReferencesController>();
        private readonly IObjectService _objectService;
        private readonly IBlobService _blobStore;

        public ReferencesController(IObjectService objectService, IBlobService blobStore, IDiagnosticContext diagnosticContext, IAuthorizationService authorizationService, IOptionsMonitor<JupiterSettings> jupiterSettings, FormatResolver formatResolver, BufferedPayloadFactory bufferedPayloadFactory)
        {
            _objectService = objectService;
            _blobStore = blobStore;
            _diagnosticContext = diagnosticContext;
            _authorizationService = authorizationService;
            _jupiterSettings = jupiterSettings;
            _formatResolver = formatResolver;
            _bufferedPayloadFactory = bufferedPayloadFactory;
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
                        ReadOnlyMemory<byte> localMemory = new ReadOnlyMemory<byte>(blobMemory);
                        CompactBinaryObject cb = CompactBinaryObject.Load(ref localMemory);
                        List<CompactBinaryField> compactBinaryFields = cb.GetFields().ToList();
                        // detect if this object was uploaded as a raw object, in which case we deconstruct the generated compact binary
                        if (compactBinaryFields.Count == 1)
                        {
                            CompactBinaryField field = compactBinaryFields[0];
                            if (field.IsBinaryAttachment() && string.IsNullOrEmpty(field.Name))
                            {
                                // this is a very simple object, we just fetch the blob it references and return that
                                BlobContents referencedBlobContents = await _blobStore.GetObject(ns, field.AsBinaryAttachment()!);
                                await WriteBody(referencedBlobContents, MediaTypeNames.Application.Octet);
                                break;
                            }
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
                        ReadOnlyMemory<byte> localMemory = new ReadOnlyMemory<byte>(blobMemory);
                        CompactBinaryObject cb = CompactBinaryObject.Load(ref localMemory);
                        string s = cb.ToJson();
                        await WriteBody(new BlobContents(Encoding.UTF8.GetBytes(s)), MediaTypeNames.Application.Json);
                        break;

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
                (ObjectRecord record, BlobContents _) = await _objectService.Get(ns, bucket, key, new string[] {"blobIdentifier"});
                Response.Headers[CommonHeaders.HashHeaderName] = record.BlobIdentifier.ToString();
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
                    await _objectService.Get(ns, bucket, name, new string[] {"blobIdentifier"});
                }
                catch (ObjectNotFoundException)
                {
                    missingObject.Add(name);
                }
            });
            await Task.WhenAll(tasks);

            return Ok(new { Needs = missingObject.ToArray()});
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

            CompactBinaryObject payloadObject;
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
                        CompactBinaryWriter compactBinaryWriter = new CompactBinaryWriter();
                        compactBinaryWriter.BeginObject();
                        compactBinaryWriter.AddBinaryAttachment(blobHeader);
                        compactBinaryWriter.EndObject();

                        byte[] blob = compactBinaryWriter.Save();
                        payloadObject = CompactBinaryObject.Load(blob);
                        blobHeader = BlobIdentifier.FromBlob(blob);
                        break;
                    }
                    case CustomMediaTypeNames.UnrealCompactBinary:
                    {
                        MemoryStream ms = new MemoryStream();
                        await using Stream payloadStream = payload.GetStream();
                        await payloadStream.CopyToAsync(ms);
                        payloadObject = CompactBinaryObject.Load(ms.ToArray());
                        break;
                    }
                    case MediaTypeNames.Application.Octet:
                    {
                        blobHeader = await _blobStore.PutObject(ns, payload, headerHash);

                        CompactBinaryWriter compactBinaryWriter = new CompactBinaryWriter();
                        compactBinaryWriter.BeginObject();
                        compactBinaryWriter.AddBinaryAttachment(blobHeader);
                        compactBinaryWriter.EndObject();

                        byte[] blob = compactBinaryWriter.Save();
                        payloadObject = CompactBinaryObject.Load(blob);
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


            return Ok(new { CountOfDeletedRecords = countOfDeletedRecords});
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
                return Ok(new { DeletedCount = deleted ? 1: 0 } );
            }
            catch (NamespaceNotFoundException e)
            {
                return NotFound(new ProblemDetails {Title = $"Namespace {e.Namespace} did not exist"});
            }
        }
    }

    public class RefMetadataResponse
    {
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

        public NamespaceId Ns { get; set; }
        public BucketId Bucket { get; set; }
        public IoHashKey Name { get; set; }
        public BlobIdentifier PayloadIdentifier { get; set; }
        public DateTime LastAccess { get; set; }
        public bool IsFinalized { get; set; }
        public byte[]? InlinePayload { get; set; }
    }

    public class PutObjectResponse
    {
        public PutObjectResponse(ContentHash[] missingReferences)
        {
            Needs = missingReferences;
        }

        public ContentHash[] Needs { get; set; }
    }
}
