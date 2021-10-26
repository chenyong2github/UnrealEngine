// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.Infrastructure;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http.Headers;
using System.Net.Mime;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
    /// <summary>
    /// Class deriving from a FileResult that allows custom file types (used for zip file creation)
    /// </summary>
    public class CustomFileCallbackResult : FileResult
    {
        private Func<Stream, ActionContext, Task> Callback;
		string FileName;
		bool Inline;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="FileName">Default filename for the downloaded file</param>
		/// <param name="MimeType">Content type for the file</param>
		/// <param name="Inline">Whether to display the file inline in the browser</param>
		/// <param name="Callback">Callback used to write the data</param>
		public CustomFileCallbackResult(string FileName, string MimeType, bool Inline, Func<Stream, ActionContext, Task> Callback)
            : base(MimeType)
        {
			this.FileName = FileName;
			this.Inline = Inline;
            this.Callback = Callback;
        }

        /// <summary>
        /// Executes the action result
        /// </summary>
        /// <param name="Context">The controller context</param>
        /// <returns></returns>
        public override Task ExecuteResultAsync(ActionContext Context)
        {
			ContentDisposition ContentDisposition = new ContentDisposition();
			ContentDisposition.Inline = Inline;
			ContentDisposition.FileName = FileName;
			Context.HttpContext.Response.Headers.Add("Content-Disposition", ContentDisposition.ToString());

			CustomFileCallbackResultExecutor Executor = new CustomFileCallbackResultExecutor(Context.HttpContext.RequestServices.GetRequiredService<ILoggerFactory>());
            return Executor.ExecuteAsync(Context, this);
        }

        /// <summary>
        /// Exectutor for the custom FileResult
        /// </summary>
        private sealed class CustomFileCallbackResultExecutor : FileResultExecutorBase
        {
            /// <summary>
            /// Constructor
            /// </summary>
            /// <param name="LoggerFactory">The logger</param>
            public CustomFileCallbackResultExecutor(ILoggerFactory LoggerFactory)
                : base(CreateLogger<CustomFileCallbackResultExecutor>(LoggerFactory))
            {
            }

            /// <summary>
            /// Executes a CustomFileResult callback
            /// </summary>
            /// <param name="Context">The controller context</param>
            /// <param name="Result">The custom file result</param>
            /// <returns></returns>
            public Task ExecuteAsync(ActionContext Context, CustomFileCallbackResult Result)
            {
                SetHeadersAndLog(Context, Result, null, false);
                return Result.Callback(Context.HttpContext.Response.Body, Context);
            }
        }
    }

    /// <summary>
    /// Stream overriding CanSeek to false so the zip file plays nice with it.
    /// </summary>
    public class CustomBufferStream : MemoryStream
    {
        /// <summary>
        /// Always report unseekable.
        /// </summary>
        public override bool CanSeek => false;
    }
}
