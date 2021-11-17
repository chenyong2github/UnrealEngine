// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Net.Mime;
using System.Text;
using System.Threading.Tasks;
using Dasync.Collections;
using Datadog.Trace;
using Horde.Storage.Implementation;
using Jupiter;
using Jupiter.Implementation;
using Jupiter.Utils;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.Extensions;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Controllers
{
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

        private readonly ILogger _logger = Log.ForContext<ReferencesController>();
        private readonly IObjectService _objectService;
        private readonly IBlobStore _blobStore;

        public ReferencesController(IObjectService objectService, IBlobStore blobStore, IDiagnosticContext diagnosticContext, IAuthorizationService authorizationService, IOptionsMonitor<JupiterSettings> jupiterSettings, FormatResolver formatResolver)
        {
            _objectService = objectService;
            _blobStore = blobStore;
            _diagnosticContext = diagnosticContext;
            _authorizationService = authorizationService;
            _jupiterSettings = jupiterSettings;
            _formatResolver = formatResolver;
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

            if (ShouldDoAuth())
            {
                // filter namespaces down to only the namespaces the user has access to
                namespaces = namespaces.Where(ns =>
                {
                    Task<AuthorizationResult> authorizationResult = _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);
                    return authorizationResult.Result.Succeeded;
                }).ToArray();
            }

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
            [FromRoute] [Required] KeyId key,
            [FromQuery] string[] fields,
            [FromRoute] string? format = null)
        {
            if (ShouldDoAuth())
            {
                using (Scope _ = Tracer.Instance.StartActive("authorize"))
                {
                    AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

                    if (!authorizationResult.Succeeded)
                    {
                        return Forbid();
                    }
                }
            }

            try
            {
                // TODO: Do we want to keep fields? we have no real need for it now
                (ObjectRecord objectRecord, BlobContents? blob) = await _objectService.Get(ns, bucket, key, fields);

                if (!objectRecord.IsFinalized)
                {
                    // we do not considered un-finalized objects are valid
                    return BadRequest(new ProblemDetails { Title = $"Object {objectRecord.Bucket} {objectRecord.Name} is not finalized." });
                }

                Response.Headers[CommonHeaders.HashHeaderName] = objectRecord.BlobIdentifier.ToString();
                Response.Headers[CommonHeaders.LastAccessHeaderName] = objectRecord.LastAccess.ToString();

                async Task WriteBody(BlobContents blobContents, string contentType)
                {
                    using Scope scope = Tracer.Instance.StartActive("body.write");
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
                            using Scope scope = Tracer.Instance.StartActive("json.readblob");
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
            [FromRoute] [Required] KeyId key)
        {
            if (ShouldDoAuth())
            {
                using (Scope _ = Tracer.Instance.StartActive("authorize"))
                {
                    AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

                    if (!authorizationResult.Succeeded)
                    {
                        return Forbid();
                    }
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
            [FromQuery] [Required] List<KeyId> names)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            ConcurrentBag<KeyId> missingObject = new ConcurrentBag<KeyId>();

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
            [FromRoute] [Required] KeyId key)
        {
            if (ShouldDoAuth())
            {
                using (Scope _ = Tracer.Instance.StartActive("authorize"))
                {
                    AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

                    if (!authorizationResult.Succeeded)
                    {
                        return Forbid();
                    }
                }
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

            BlobIdentifier headerHash;
            BlobIdentifier blobHeader = BlobIdentifier.FromBlob(blob);
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

            if (!blobHeader.Equals(headerHash))
            {
                return BadRequest(new ProblemDetails
                {
                    Title = $"Incorrect hash, got hash \"{headerHash}\" but hash of content was determined to be \"{blobHeader}\""
                });
            }

            CompactBinaryObject payloadObject;
            switch (Request.ContentType)
            {
                case MediaTypeNames.Application.Json:
                {
                    // TODO: define a scheme for how a json object specifies references

                    await _blobStore.PutObject(ns, blob, blobHeader);

                    // TODO: convert the json object into a compact binary instead
                    CompactBinaryWriter compactBinaryWriter = new CompactBinaryWriter();
                    compactBinaryWriter.BeginObject();
                    compactBinaryWriter.AddBinaryAttachment(blobHeader);
                    compactBinaryWriter.EndObject();

                    blob = compactBinaryWriter.Save();
                    payloadObject = CompactBinaryObject.Load(blob);
                    blobHeader = BlobIdentifier.FromBlob(blob);
                    break;
                }
                case CustomMediaTypeNames.UnrealCompactBinary:
                {
                    payloadObject = CompactBinaryObject.Load(blob);
                    break;
                }
                case MediaTypeNames.Application.Octet:
                {
                    await _blobStore.PutObject(ns, blob, blobHeader);

                    CompactBinaryWriter compactBinaryWriter = new CompactBinaryWriter();
                    compactBinaryWriter.BeginObject();
                    compactBinaryWriter.AddBinaryAttachment(blobHeader);
                    compactBinaryWriter.EndObject();

                    blob = compactBinaryWriter.Save();
                    payloadObject = CompactBinaryObject.Load(blob);
                    blobHeader = BlobIdentifier.FromBlob(blob);
                    break;
                }
                default:
                    throw new Exception($"Unknown request type {Request.ContentType}, if submitting a blob please use {MediaTypeNames.Application.Octet}");
            }

            PutObjectResult result = await _objectService.Put(ns, bucket, key, blobHeader, payloadObject);
            return Ok(new PutObjectResponse(result.MissingReferences));
        }

        [HttpPost("{ns}/{bucket}/{key}/finalize/{hash}.{format?}")]
        [Authorize("Object.write")]
        public async Task<IActionResult> FinalizeObject(
            [FromRoute] [Required] NamespaceId ns,
            [FromRoute] [Required] BucketId bucket,
            [FromRoute] [Required] KeyId key,
            [FromRoute] [Required] BlobIdentifier hash)
        {
            if (ShouldDoAuth())
            {
                using (Scope _ = Tracer.Instance.StartActive("authorize"))
                {
                    AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

                    if (!authorizationResult.Succeeded)
                    {
                        return Forbid();
                    }
                }
            }

            BlobIdentifier[] missingReferences = await _objectService.Finalize(ns, bucket, key, hash);
            return Ok(new PutObjectResponse(missingReferences));
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
            [FromRoute] [Required] KeyId key)
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            try
            {
                long deleteCount = await _objectService.Delete(ns, bucket, key);
                return Ok(new { DeletedCount = deleteCount } );
            }
            catch (NamespaceNotFoundException e)
            {
                return NotFound(new ProblemDetails {Title = $"Namespace {e.Namespace} did not exist"});
            }
        }

        private bool ShouldDoAuth()
        {
            foreach (int port in _jupiterSettings.CurrentValue.DisableAuthOnPorts)
            {
                if (port == HttpContext.Connection.LocalPort)
                    return false;
            }

            return true;
        }
    }

    public class PutObjectResponse
    {
        public PutObjectResponse(BlobIdentifier[] missingReferences)
        {
            Needs = missingReferences;
        }

        public BlobIdentifier[] Needs { get; set; }
    }
}
