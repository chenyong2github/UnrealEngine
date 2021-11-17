// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net.Mime;
using System.Threading.Tasks;
using Datadog.Trace;
using Horde.Storage.Controllers;
using Horde.Storage.Implementation;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace Horde.Storage
{
    public class DDCEndpoints
    {
        private readonly IDDCRefService _ddcRefService;
        private readonly IAuthorizationService _authorizationService;

        public DDCEndpoints(IDDCRefService ddcRefService, IAuthorizationService authorizationService)
        {
            _ddcRefService = ddcRefService;
            _authorizationService = authorizationService;
        }

        public string GetRawRoute { get; set; } = "/api/v1/c/ddc/{ns}/{bucket}/{key}.raw";

        public async Task GetRaw(HttpContext httpContext)
        {
            string? ns = httpContext.GetRouteValue("ns") as string;
            if (string.IsNullOrEmpty(ns))
            {
                // TODO: Error message
                return;
            }
            
            using (Scope _ = Tracer.Instance.StartActive("authorize"))
            {
                {
                    AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(httpContext.User, ns, NamespaceAccessRequirement.Name);

                    if (!authorizationResult.Succeeded)
                    {
                        httpContext.Response.StatusCode = StatusCodes.Status401Unauthorized;
                        return;
                    }
                }
            }

            string? bucket = httpContext.GetRouteValue("bucket") as string;
            if (string.IsNullOrEmpty(bucket))
            {
                // TODO: Error message
                return;
            }

            string? key = httpContext.GetRouteValue("key") as string;
            if (string.IsNullOrEmpty(key))
            {
                // TODO: Error message
                return;
            }

            httpContext.Response.Headers["X-Jupiter-ImplUsed"] = "Endpoint";
            try
            {
                (RefResponse refRes, BlobContents? tempBlob) = await _ddcRefService.Get(new NamespaceId(ns), new BucketId(bucket), new KeyId(key), fields: new[] {"blob"});
                BlobContents blob = tempBlob!;

                httpContext.Response.StatusCode = StatusCodes.Status200OK;
                httpContext.Response.Headers["Content-Type"] = MediaTypeNames.Application.Octet;
                httpContext.Response.Headers["Content-Length"] = Convert.ToString(blob.Length);
                httpContext.Response.Headers[CommonHeaders.HashHeaderName] = refRes.ContentHash.ToString();

                using (Scope _ = Tracer.Instance.StartActive("body.write"))
                {
                    await blob.Stream.CopyToAsync(httpContext.Response.Body);
                }
            }
            catch (NamespaceNotFoundException e)
            {
                httpContext.Response.StatusCode = StatusCodes.Status400BadRequest;
                httpContext.Response.Headers["Content-Type"] = "application/json";
                await httpContext.Response.WriteAsync("{\"title\": \"Namespace " + e.Namespace + " did not exist\", \"status\": 400}");
            }
            catch (RefRecordNotFoundException e)
            {
                httpContext.Response.StatusCode = StatusCodes.Status400BadRequest;
                httpContext.Response.Headers["Content-Type"] = "application/json";
                await httpContext.Response.WriteAsync("{\"title\": \"Object " + e.Bucket + " " + e.Key + " did not exist\", \"status\": 400}");
            }
            catch (BlobNotFoundException e)
            {
                httpContext.Response.StatusCode = StatusCodes.Status400BadRequest;
                httpContext.Response.Headers["Content-Type"] = "application/json";
                await httpContext.Response.WriteAsync("{\"title\": \"Object " + e.Blob + " in " + e.Ns + " not found\", \"status\": 400}");
            }
        }
    }
}
