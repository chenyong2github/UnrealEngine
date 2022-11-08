// Copyright Epic Games, Inc. All Rights Reserved.

//#define ENABLE_PUBLIC_DEBUG_CONTROLLER
#define ENABLE_SECURE_DEBUG_CONTROLLER


using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using System.Web;
using EpicGames.Core;
using Horde.Build.Acls;
using Horde.Build.Jobs;
using Horde.Build.Jobs.Graphs;
using Horde.Build.Jobs.Templates;
using Horde.Build.Logs;
using Horde.Build.Utilities;
using JetBrains.Profiler.SelfApi;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using MongoDB.Driver;

namespace Horde.Build.Server
{
#if ENABLE_PUBLIC_DEBUG_CONTROLLER
	/// <summary>
	/// Public endpoints for the debug controller
	/// </summary>
	[ApiController]
	public class PublicDebugController : ControllerBase
	{
		/// <summary>
		/// The connection tracker service singleton
		/// </summary>
		RequestTrackerService RequestTrackerService;

		IHostApplicationLifetime ApplicationLifetime;

		IDogStatsd DogStatsd;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="RequestTrackerService"></param>
		/// <param name="ApplicationLifetime"></param>
		/// <param name="DogStatsd"></param>
		public PublicDebugController(RequestTrackerService RequestTrackerService, IHostApplicationLifetime ApplicationLifetime, IDogStatsd DogStatsd)
		{
			RequestTrackerService = RequestTrackerService;
			ApplicationLifetime = ApplicationLifetime;
			DogStatsd = DogStatsd;
		}

		/// <summary>
		/// Prints all the headers for the incoming request
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/headers")]
		public IActionResult GetRequestHeaders()
		{
			StringBuilder Content = new StringBuilder();
			Content.AppendLine("<html><body><pre>");
			foreach (KeyValuePair<string, StringValues> Pair in HttpContext.Request.Headers)
			{
				foreach (string Value in Pair.Value)
				{
					Content.AppendLine(HttpUtility.HtmlEncode($"{Pair.Key}: {Value}"));
				}
			}
			Content.Append("</pre></body></html>");
			return new ContentResult { ContentType = "text/html", StatusCode = (int)HttpStatusCode.OK, Content = Content.ToString() };
		}

		/// <summary>
		/// Waits specified number of milliseconds and then returns a response
		/// Used for testing timeouts proxy settings.
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/wait")]
		public async Task<ActionResult> GetAndWait([FromQuery] int WaitTimeMs = 1000)
		{
			await Task.Delay(WaitTimeMs);
			string Content = $"Waited {WaitTimeMs} ms. " + new Random().Next(0, 10000000);
			return new ContentResult { ContentType = "text/plain", StatusCode = (int)HttpStatusCode.OK, Content = Content };
		}

		/// <summary>
		/// Waits specified number of milliseconds and then throws an exception
		/// Used for testing graceful shutdown and interruption of outstanding requests.
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/exception")]
		public async Task<ActionResult> ThrowException([FromQuery] int WaitTimeMs = 0)
		{
			await Task.Delay(WaitTimeMs);
			throw new Exception("Test exception triggered by debug controller!");
		}

		/// <summary>
		/// Trigger an increment of a DogStatsd metric
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/metric")]
		public ActionResult TriggerMetric([FromQuery] int Value = 10)
		{
			DogStatsd.Increment("hordeMetricTest", Value);
			return Ok("Incremented metric 'hordeMetricTest' Type: " + DogStatsd.GetType());
		}

		/// <summary>
		/// Display metrics related to the .NET runtime
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/dotnet-metrics")]
		public ActionResult DotNetMetrics()
		{
			ThreadPool.GetMaxThreads(out int MaxWorkerThreads, out int MaxIoThreads);
			ThreadPool.GetAvailableThreads(out int FreeWorkerThreads, out int FreeIoThreads);
			ThreadPool.GetMinThreads(out int MinWorkerThreads, out int MinIoThreads);

			int BusyIoThreads = MaxIoThreads - FreeIoThreads;
			int BusyWorkerThreads = MaxWorkerThreads - FreeWorkerThreads;

			StringBuilder Content = new StringBuilder();
			Content.AppendLine("Threads:");
			Content.AppendLine("-------------------------------------------------------------");
			Content.AppendLine("Worker busy={0,-5} free={1,-5} min={2,-5} max={3,-5}", BusyWorkerThreads, FreeWorkerThreads, MinWorkerThreads, MaxWorkerThreads);
			Content.AppendLine("  IOCP busy={0,-5} free={1,-5} min={2,-5} max={3,-5}", BusyIoThreads, FreeIoThreads, MinIoThreads, MaxWorkerThreads);


			NumberFormatInfo Nfi = (NumberFormatInfo)CultureInfo.InvariantCulture.NumberFormat.Clone();
			Nfi.NumberGroupSeparator = " ";

			string FormatBytes(long Number)
			{
				return (Number / 1024 / 1024).ToString("#,0", Nfi) + " MB";
			}

			GCMemoryInfo GcMemoryInfo = GC.GetGCMemoryInfo();
			Content.AppendLine("");
			Content.AppendLine("");
			Content.AppendLine("Garbage collection (GC):");
			Content.AppendLine("-------------------------------------------------------------");
			Content.AppendLine("              Latency mode: " + GCSettings.LatencyMode);
			Content.AppendLine("              Is server GC: " + GCSettings.IsServerGC);
			Content.AppendLine("              Total memory: " + FormatBytes(GC.GetTotalMemory(false)));
			Content.AppendLine("           Total allocated: " + FormatBytes(GC.GetTotalAllocatedBytes(false)));
			Content.AppendLine("                 Heap size: " + FormatBytes(GcMemoryInfo.HeapSizeBytes));
			Content.AppendLine("                Fragmented: " + FormatBytes(GcMemoryInfo.FragmentedBytes));
			Content.AppendLine("               Memory Load: " + FormatBytes(GcMemoryInfo.MemoryLoadBytes));
			Content.AppendLine("    Total available memory: " + FormatBytes(GcMemoryInfo.TotalAvailableMemoryBytes));
			Content.AppendLine("High memory load threshold: " + FormatBytes(GcMemoryInfo.HighMemoryLoadThresholdBytes));

			return Ok(Content.ToString());
		}

		/// <summary>
		/// Force a full GC of all generations
		/// </summary>
		/// <returns>Prints time taken in ms</returns>
		[HttpGet]
		[Route("/api/v1/debug/force-gc")]
		public ActionResult ForceTriggerGc()
		{
			Stopwatch Timer = new Stopwatch();
			Timer.Start();
			GC.Collect();
			Timer.Stop();
			return Ok($"Time taken: {Timer.Elapsed.TotalMilliseconds} ms");
		}

		/// <summary>
		/// Lists requests in progress
		/// </summary>
		/// <returns>HTML result</returns>
		[HttpGet]
		[Route("/api/v1/debug/requests-in-progress")]
		public ActionResult GetRequestsInProgress()
		{
			StringBuilder Content = new StringBuilder();
			Content.AppendLine("<html><body>");
			Content.AppendLine("<h1>Requests in progress</h1>");
			Content.AppendLine("<table border=\"1\">");
			Content.AppendLine("<tr>");
			Content.AppendLine("<th>Request Trace ID</th>");
			Content.AppendLine("<th>Path</th>");
			Content.AppendLine("<th>Started At</th>");
			Content.AppendLine("<th>Age</th>");
			Content.AppendLine("</tr>");

			List<KeyValuePair<string, TrackedRequest>> Requests = RequestTrackerService.GetRequestsInProgress().ToList();
			Requests.Sort((A, B) => A.Value.StartedAt.CompareTo(B.Value.StartedAt));

			foreach (KeyValuePair<string, TrackedRequest> Entry in Requests)
			{
				Content.Append("<tr>");
				Content.AppendLine($"<td>{Entry.Key}</td>");
				Content.AppendLine($"<td>{Entry.Value.Request.Path}</td>");
				Content.AppendLine($"<td>{Entry.Value.StartedAt}</td>");
				Content.AppendLine($"<td>{Entry.Value.GetTimeSinceStartInMs()} ms</td>");
				Content.Append("</tr>");
			}
			Content.Append("</table>\n</body>\n</html>");

			return new ContentResult { ContentType = "text/html", StatusCode = (int)HttpStatusCode.OK, Content = Content.ToString() };
		}

		/*
		// Used during development only
		[HttpGet]
		[Route("/api/v1/debug/stop")]
		public ActionResult StopApp()
		{
			Task.Run(async () =>
			{
				await Task.Delay(100);
				ApplicationLifetime.StopApplication();
			});
			
			return new ContentResult { ContentType = "text/plain", StatusCode = (int)HttpStatusCode.OK, Content = "App stopping..." };
		}
		/**/
	}
#endif
#if ENABLE_SECURE_DEBUG_CONTROLLER
	/// <summary>
	/// Controller managing account status
	/// </summary>
	[ApiController]
	[Authorize]
	public class SecureDebugController : ControllerBase
	{
		/// <summary>
		/// The ACL service singleton
		/// </summary>
		private readonly AclService _aclService;

		/// <summary>
		/// The database service instance
		/// </summary>
		private readonly MongoService _mongoService;

		/// <summary>
		/// The job task source singleton
		/// </summary>
		private readonly JobTaskSource _jobTaskSource;

		/// <summary>
		/// Collection of template documents
		/// </summary>
		private readonly ITemplateCollection _templateCollection;
		
		/// <summary>
		/// The graph collection singleton
		/// </summary>
		private readonly IGraphCollection _graphCollection;

		/// <summary>
		/// The log file collection singleton
		/// </summary>
		private readonly ILogFileCollection _logFileCollection;

		/// <summary>
		/// Settings
		/// </summary>
		private readonly IOptionsMonitor<ServerSettings> _settings;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="aclService">The ACL service singleton</param>
		/// <param name="mongoService">The database service instance</param>
		/// <param name="jobTaskSource">The dispatch service singleton</param>
		/// <param name="templateCollection">Collection of template documents</param>
		/// <param name="graphCollection">The graph collection</param>
		/// <param name="logFileCollection">The log file collection</param>
		/// <param name="settings">Settings</param>
		public SecureDebugController(AclService aclService, MongoService mongoService, JobTaskSource jobTaskSource,
			ITemplateCollection templateCollection, IGraphCollection graphCollection,
			ILogFileCollection logFileCollection, IOptionsMonitor<ServerSettings> settings)
		{
			_aclService = aclService;
			_mongoService = mongoService;
			_jobTaskSource = jobTaskSource;
			_templateCollection = templateCollection;
			_graphCollection = graphCollection;
			_logFileCollection = logFileCollection;
			_settings = settings;
		}

		/// <summary>
		/// Prints all the environment variables
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/environment")]
		public async Task<ActionResult> GetServerEnvVars()
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			StringBuilder content = new StringBuilder();
			content.AppendLine("<html><body><pre>");
			foreach (System.Collections.DictionaryEntry? pair in System.Environment.GetEnvironmentVariables())
			{
				if (pair != null)
				{
					content.AppendLine(HttpUtility.HtmlEncode($"{pair.Value.Key}={pair.Value.Value}"));
				}
			}
			content.Append("</pre></body></html>");
			return new ContentResult { ContentType = "text/html", StatusCode = (int)HttpStatusCode.OK, Content = content.ToString() };
		}

		/// <summary>
		/// Returns diagnostic information about the current state of the queue
		/// </summary>
		/// <returns>Information about the queue</returns>
		[HttpGet]
		[Route("/api/v1/debug/queue")]
		public async Task<ActionResult<object>> GetQueueStatusAsync()
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			return _jobTaskSource.GetStatus();
		}

		/// <summary>
		/// Returns the complete config Horde uses
		/// </summary>
		/// <returns>Information about the config</returns>
		[HttpGet]
		[Route("/api/v1/debug/config")]
		public async Task<ActionResult> GetConfig()
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			JsonSerializerOptions options = new JsonSerializerOptions
			{
				WriteIndented = true
			};

			return Ok(JsonSerializer.Serialize(_settings.CurrentValue, options));
		}

		/// <summary>
		/// Queries for all graphs
		/// </summary>
		/// <returns>The graph definitions</returns>
		[HttpGet]
		[Route("/api/v1/debug/graphs")]
		[ProducesResponseType(200, Type = typeof(GetGraphResponse))]
		public async Task<ActionResult<List<object>>> GetGraphsAsync([FromQuery] int? index = null, [FromQuery] int? count = null, [FromQuery] PropertyFilter? filter = null)
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			List<IGraph> graphs = await _graphCollection.FindAllAsync(null, index, count);
			return graphs.ConvertAll(x => new GetGraphResponse(x).ApplyFilter(filter));
		}

		/// <summary>
		/// Queries for a particular graph by hash
		/// </summary>
		/// <returns>The graph definition</returns>
		[HttpGet]
		[Route("/api/v1/debug/graphs/{GraphId}")]
		[ProducesResponseType(200, Type = typeof(GetGraphResponse))]
		public async Task<ActionResult<object>> GetGraphAsync(string graphId, [FromQuery] PropertyFilter? filter = null)
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			IGraph graph = await _graphCollection.GetAsync(ContentHash.Parse(graphId));
			return new GetGraphResponse(graph).ApplyFilter(filter);
		}

		/// <summary>
		/// Query all the job templates.
		/// </summary>
		/// <param name="filter">Filter for properties to return</param>
		/// <returns>Information about all the job templates</returns>
		[HttpGet]
		[Route("/api/v1/debug/templates")]
		[ProducesResponseType(typeof(List<GetTemplateResponse>), 200)]
		public async Task<ActionResult<object>> GetTemplatesAsync([FromQuery] PropertyFilter? filter = null)
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			List<ITemplate> templates = await _templateCollection.FindAllAsync();
			return templates.ConvertAll(x => new GetTemplateResponse(x).ApplyFilter(filter));
		}

		/// <summary>
		/// Retrieve information about a specific job template.
		/// </summary>
		/// <param name="templateHash">Id of the template to get information about</param>
		/// <param name="filter">List of properties to return</param>
		/// <returns>Information about the requested template</returns>
		[HttpGet]
		[Route("/api/v1/debug/templates/{TemplateHash}")]
		[ProducesResponseType(typeof(GetTemplateResponse), 200)]
		public async Task<ActionResult<object>> GetTemplateAsync(string templateHash, [FromQuery] PropertyFilter? filter = null)
		{
			ContentHash templateHashValue = ContentHash.Parse(templateHash);
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			ITemplate? template = await _templateCollection.GetAsync(templateHashValue);
			if (template == null)
			{
				return NotFound();
			}
			return template.ApplyFilter(filter);
		}

		/// <summary>
		/// Retrieve metadata about a specific log file
		/// </summary>
		/// <param name="logFileId">Id of the log file to get information about</param>
		/// <param name="filter">Filter for the properties to return</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/debug/logs/{LogFileId}")]
		public async Task<ActionResult<object>> GetLogAsync(ObjectId<ILogFile> logFileId, [FromQuery] PropertyFilter? filter = null)
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			ILogFile? logFile = await _logFileCollection.GetLogFileAsync(logFileId, CancellationToken.None);
			if (logFile == null)
			{
				return NotFound();
			}

			return logFile.ApplyFilter(filter);
		}

		/// <summary>
		/// Populate the database with test data
		/// </summary>
		/// <returns>Async task</returns>
		[HttpGet]
		[Route("/api/v1/debug/collections/{Name}")]
		public async Task<ActionResult<object>> GetDocumentsAsync(string name, [FromQuery] string? filter = null, [FromQuery] int index = 0, [FromQuery] int count = 10)
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			IMongoCollection<Dictionary<string, object>> collection = _mongoService.GetCollection<Dictionary<string, object>>(name);
			List<Dictionary<string, object>> documents = await collection.Find(filter ?? "{}").Skip(index).Limit(count).ToListAsync();
			return documents;
		}
		
		/// <summary>
		/// Starts the profiler session
		/// </summary>
		/// <returns>Text message</returns>
		[HttpGet]
		[Route("/api/v1/debug/profiler/start")]
		public async Task<ActionResult> StartProfiler()
		{
			await DotTrace.EnsurePrerequisiteAsync();

			string snapshotDir = Path.Join(Path.GetTempPath(), "horde-profiler-snapshots");
			if (!Directory.Exists(snapshotDir))
			{
				Directory.CreateDirectory(snapshotDir);
			}

			DotTrace.Config config = new ();
			config.SaveToDir(snapshotDir);
			DotTrace.Attach(config);
			DotTrace.StartCollectingData();
			
			return new ContentResult { ContentType = "text/plain", StatusCode = (int)HttpStatusCode.OK, Content = "Profiling session started. Using dir " + snapshotDir };
		}
		
		/// <summary>
		/// Stops the profiler session
		/// </summary>
		/// <returns>Text message</returns>
		[HttpGet]
		[Route("/api/v1/debug/profiler/stop")]
		public ActionResult StopProfiler()
		{
			DotTrace.SaveData();
			DotTrace.Detach();
			return new ContentResult { ContentType = "text/plain", StatusCode = (int)HttpStatusCode.OK, Content = "Profiling session stopped" };
		}
		
		/// <summary>
		/// Downloads the captured profiling snapshots
		/// </summary>
		/// <returns>A .zip file containing the profiling snapshots</returns>
		[HttpGet]
		[Route("/api/v1/debug/profiler/download")]
		public ActionResult DownloadProfilingData()
		{
			string snapshotZipFile = DotTrace.GetCollectedSnapshotFilesArchive(false);
			if (!System.IO.File.Exists(snapshotZipFile))
			{
				return NotFound("The generated snapshot .zip file was not found");
			}
			
			return PhysicalFile(snapshotZipFile, "application/zip", Path.GetFileName(snapshotZipFile));
		}
	}
}

#endif
