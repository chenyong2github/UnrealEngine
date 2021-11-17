// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net.Mime;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;

namespace Horde.Storage.Controllers
{
    [ApiController]
    [Route("api/v1/c/_debug")]
    public class DebugController : Controller
    {
        private static readonly byte[] OneKilobyteData = new byte[1024];
        private static readonly byte[] OneMegabyteData = new byte[1024 * 1024];
        private static readonly byte[] SixteenMegabyteData = new byte[16 * 1024 * 1024];
        private static bool s_testBuffersFilled = false;
        
        /// <summary>
        /// Return bytes of specified length with auth, used for testing only
        /// </summary>
        /// <returns></returns>
        [HttpGet("getBytes")]
        [Authorize("Any")]
        public IActionResult GetBytes([FromQuery] int length = 1)
        {
            return GenerateByteResponse(length);
        }
        
        /// <summary>
        /// Return bytes of specified length without auth, used for testing only
        /// </summary>
        /// <returns></returns>
        [HttpGet("getBytesWithoutAuth")]
        public IActionResult GetBytesWithoutAuth([FromQuery] int length = 1)
        {
            return GenerateByteResponse(length);
        }
        
        [ApiExplorerSettings(IgnoreApi = true)]
        [NonAction]
        public async Task FastGetBytes(HttpContext httpContext, Func<Task> next)
        {
            async Task Send(byte[] data)
            {
                httpContext.Response.StatusCode = StatusCodes.Status200OK;
                httpContext.Response.Headers["Content-Type"] = MediaTypeNames.Application.Octet;
                httpContext.Response.Headers["Content-Length"] = Convert.ToString(data.Length);
                await httpContext.Response.Body.WriteAsync(data);
            }

            if (!s_testBuffersFilled)
            {
                Array.Fill(OneKilobyteData, (byte)'k');
                Array.Fill(OneMegabyteData, (byte)'m');
                Array.Fill(SixteenMegabyteData, (byte)'s');
                s_testBuffersFilled = true;
            }
            
            if (httpContext.Request.Path.StartsWithSegments("/api/v1/c/_debug/fastBytes/1byte"))
            {
                await Send(new byte[] {0x4A});
            }
            else if (httpContext.Request.Path.StartsWithSegments("/api/v1/c/_debug/fastBytes/1kilobyte"))
            {
                await Send(OneKilobyteData);
            }
            else if (httpContext.Request.Path.StartsWithSegments("/api/v1/c/_debug/fastBytes/1megabyte"))
            {
                await Send(OneMegabyteData);
            }
            else if (httpContext.Request.Path.StartsWithSegments("/api/v1/c/_debug/fastBytes/16megabyte"))
            {
                await Send(SixteenMegabyteData);
            }
            else
            {
                await next.Invoke();    
            }
        }

        private FileContentResult GenerateByteResponse(int length)
        {
            byte[] generatedData = new byte[length];
            Array.Fill(generatedData, (byte)'J');
            return File(generatedData, "application/octet-stream");
        }
    }
}
