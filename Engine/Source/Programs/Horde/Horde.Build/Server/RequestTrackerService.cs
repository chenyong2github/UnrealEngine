// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Logging;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Tracks open HTTP requests to ASP.NET
	/// Will block a pending shutdown until all requests in progress are finished (or graceful timeout is reached)
	/// This avoids interrupting long running requests such as artifact uploads.
	/// </summary>
	public class RequestTrackerService
	{
		/// <summary>
		/// Writer for log output
		/// </summary>
		private readonly ILogger<RequestTrackerService> Logger;

		readonly ConcurrentDictionary<string, TrackedRequest> RequestsInProgress = new ConcurrentDictionary<string, TrackedRequest>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Logger">Logger</param>
		public RequestTrackerService(ILogger<RequestTrackerService> Logger)
		{
			this.Logger = Logger;
		}

		/// <summary>
		/// Called by the middleware when a request is started
		/// </summary>
		/// <param name="Context">HTTP Context</param>
		public void RequestStarted(HttpContext Context)
		{
			RequestsInProgress[Context.TraceIdentifier] = new TrackedRequest(Context.Request);
		}
		
		/// <summary>
		/// Called by the middleware when a request is finished (no matter if an exception occurred or not)
		/// </summary>
		/// <param name="Context">HTTP Context</param>
		public void RequestFinished(HttpContext Context)
		{
			RequestsInProgress.Remove(Context.TraceIdentifier, out _);
		}

		/// <summary>
		/// Get current requests in progress
		/// </summary>
		/// <returns>The requests in progress</returns>
		public IReadOnlyDictionary<string, TrackedRequest> GetRequestsInProgress()
		{
			return RequestsInProgress;
		}

		private string GetRequestsInProgressAsString()
		{
			List<KeyValuePair<string, TrackedRequest>> Requests = GetRequestsInProgress().ToList();
			Requests.Sort((A, B) => A.Value.StartedAt.CompareTo(B.Value.StartedAt));
			StringBuilder Content = new StringBuilder();
			foreach (KeyValuePair<string,TrackedRequest> Pair in Requests)
			{
				int AgeInMs = Pair.Value.GetTimeSinceStartInMs();
				string Path = Pair.Value.Request.Path;
				Content.AppendLine(CultureInfo.InvariantCulture, $"{AgeInMs,9}  {Path}");
			}

			return Content.ToString();
		}

		/// <summary>
		/// Log all requests currently in progress
		/// </summary>
		public void LogRequestsInProgress()
		{
			if (GetRequestsInProgress().Count == 0)
			{
				Logger.LogInformation("There are no requests in progress!");
			}
			else
			{
				Logger.LogInformation("Current open requests are:\n{RequestsInProgress}", GetRequestsInProgressAsString());
			}
		}
	}
	
	/// <summary>
	/// Value object for tracked requests
	/// </summary>
	public class TrackedRequest
	{
		/// <summary>
		/// When the request was received
		/// </summary>
		public DateTime StartedAt { get; }
			
		/// <summary>
		/// The HTTP request being tracked
		/// </summary>
		public HttpRequest Request { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Request">HTTP request being tracked</param>
		public TrackedRequest(HttpRequest Request)
		{
			StartedAt = DateTime.UtcNow;
			this.Request = Request;
		}

		/// <summary>
		/// How long the request has been running
		/// </summary>
		/// <returns>Time elapsed in milliseconds since request started</returns>
		public int GetTimeSinceStartInMs()
		{
			return (int) (DateTime.UtcNow - StartedAt).TotalMilliseconds;
		}
	}
	
	/// <summary>
	/// ASP.NET Middleware to track open requests
	/// </summary>
	public class RequestTrackerMiddleware
	{
		private readonly RequestDelegate Next;
	
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Next">Next middleware to call</param>
		public RequestTrackerMiddleware(RequestDelegate Next)
		{
			this.Next = Next;
		}
	
		/// <summary>
		/// Invoked by ASP.NET framework itself
		/// </summary>
		/// <param name="Context">HTTP Context</param>
		/// <param name="Service">The RequestTrackerService singleton</param>
		/// <returns></returns>
		public async Task Invoke(HttpContext Context, RequestTrackerService Service)
		{
			if (!Context.Request.Path.StartsWithSegments("/health", StringComparison.Ordinal))
			{
				try
				{
					Service.RequestStarted(Context);
					await Next(Context);
				}
				finally
				{
					Service.RequestFinished(Context);
				}
			}
			else
			{
				// Ignore requests to /health/*
				await Next(Context);
			}
		}
	}
}