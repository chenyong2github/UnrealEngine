// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Tasks.Impl;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Primitives;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Runtime;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Web;

using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;
using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;
using AgentSoftwareVersion = HordeServer.Utilities.StringId<HordeServer.Collections.IAgentSoftwareCollection>;

using IStream = HordeServer.Models.IStream;
using MongoDB.Driver;
using System.Text.Json;
using System.Threading;
using HordeServer.Services.Impl;
using Microsoft.Net.Http.Headers;
using HordeServer.Storage;
using Microsoft.Extensions.Hosting;
using StatsdClient;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Public endpoints for the debug controller
	/// </summary>
	[ApiController]
	[Route("[controller]")]
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
			this.RequestTrackerService = RequestTrackerService;
			this.ApplicationLifetime = ApplicationLifetime;
			this.DogStatsd = DogStatsd;
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

			List<KeyValuePair<string,TrackedRequest>> Requests = RequestTrackerService.GetRequestsInProgress().ToList();
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

	/// <summary>
	/// Controller managing account status
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class SecureDebugController : ControllerBase
	{
		/// <summary>
		/// The ACL service singleton
		/// </summary>
		AclService AclService;

		/// <summary>
		/// The database service instance
		/// </summary>
		DatabaseService DatabaseService;

		/// <summary>
		/// The job task source singelton
		/// </summary>
		JobTaskSource JobTaskSource;

		/// <summary>
		/// The globals singleton
		/// </summary>
		ISingletonDocument<Globals> GlobalsSingleton;

		/// <summary>
		/// Collection of pool documents
		/// </summary>
		IPoolCollection PoolCollection;

		/// <summary>
		/// Collection of project documents
		/// </summary>
		IProjectCollection ProjectCollection;

		/// <summary>
		/// Collection of agent documents
		/// </summary>
		IAgentCollection AgentCollection;

		/// <summary>
		/// Collection of session documents
		/// </summary>
		ISessionCollection SessionCollection;

		/// <summary>
		/// Collection of lease documents
		/// </summary>
		ILeaseCollection LeaseCollection;

		/// <summary>
		/// Collection of template documents
		/// </summary>
		ITemplateCollection TemplateCollection;

		/// <summary>
		/// Collection of stream documents
		/// </summary>
		IStreamCollection StreamCollection;

		/// <summary>
		/// The graph collection singleton
		/// </summary>
		IGraphCollection GraphCollection;

		/// <summary>
		/// The log file collection singleton
		/// </summary>
		ILogFileCollection LogFileCollection;

		/// <summary>
		/// The storage provider
		/// </summary>
		IStorageBackend StorageProvider;
		
		/// <summary>
		/// Perforce client
		/// </summary>
		IPerforceService Perforce;
		
		/// <summary>
		/// Fleet manager
		/// </summary>
		IFleetManager FleetManager;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service singleton</param>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="JobTaskSource">The dispatch service singleton</param>
		/// <param name="GlobalsSingleton">The globals singleton</param>
		/// <param name="PoolCollection">Collection of pool documents</param>
		/// <param name="ProjectCollection">Collection of project documents</param>
		/// <param name="AgentCollection">Collection of agent documents</param>
		/// <param name="SessionCollection">Collection of session documents</param>
		/// <param name="LeaseCollection">Collection of lease documents</param>
		/// <param name="TemplateCollection">Collection of template documents</param>
		/// <param name="StreamCollection">Collection of stream documents</param>
		/// <param name="GraphCollection">The graph collection</param>
		/// <param name="LogFileCollection">The log file collection</param>
		/// <param name="StorageProvider">The storage provider</param>
		/// <param name="Perforce">Perforce client</param>
		/// <param name="FleetManager">The default fleet manager</param>
		public SecureDebugController(AclService AclService, DatabaseService DatabaseService, JobTaskSource JobTaskSource, ISingletonDocument<Globals> GlobalsSingleton, IPoolCollection PoolCollection, IProjectCollection ProjectCollection, IAgentCollection AgentCollection, ISessionCollection SessionCollection, ILeaseCollection LeaseCollection, ITemplateCollection TemplateCollection, IStreamCollection StreamCollection, IGraphCollection GraphCollection, ILogFileCollection LogFileCollection, IStorageBackend StorageProvider, IPerforceService Perforce, IFleetManager FleetManager)
		{
			this.AclService = AclService;
			this.DatabaseService = DatabaseService;
			this.JobTaskSource = JobTaskSource;
			this.GlobalsSingleton = GlobalsSingleton;
			this.PoolCollection = PoolCollection;
			this.ProjectCollection = ProjectCollection;
			this.AgentCollection = AgentCollection;
			this.SessionCollection = SessionCollection;
			this.LeaseCollection = LeaseCollection;
			this.TemplateCollection = TemplateCollection;
			this.StreamCollection = StreamCollection;
			this.GraphCollection = GraphCollection;
			this.LogFileCollection = LogFileCollection;
			this.StorageProvider = StorageProvider;
			this.Perforce = Perforce;
			this.FleetManager = FleetManager;
		}

		/// <summary>
		/// Prints all the environment variables
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/environment")]
		public async Task<ActionResult> GetServerEnvVars()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			StringBuilder Content = new StringBuilder();
			Content.AppendLine("<html><body><pre>");
			foreach (System.Collections.DictionaryEntry? Pair in System.Environment.GetEnvironmentVariables())
			{
				if (Pair != null)
				{
					Content.AppendLine(HttpUtility.HtmlEncode($"{Pair.Value.Key}={Pair.Value.Value}"));
				}
			}
			Content.Append("</pre></body></html>");
			return new ContentResult { ContentType = "text/html", StatusCode = (int)HttpStatusCode.OK, Content = Content.ToString() };
		}

		/// <summary>
		/// Resets the test data to match the default from the database service
		/// </summary>
		[HttpPost]
		[Route("/api/v1/debug/createtestdata")]
		public async Task<ActionResult> CreateTestDataAsync([FromQuery] string? Instance = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			Globals Globals = await GlobalsSingleton.GetAsync();
			if (Instance == null)
			{
				return NotFound($"Missing instance query parameter. Set to {Globals.InstanceId} to create test data.");
			}
			if (Globals.InstanceId != Instance.ToObjectId())
			{
				return NotFound($"Incorrect instance query parameter. Should be {Globals.InstanceId}.");
			}

			using (StringWriter Writer = new StringWriter())
			{
				Writer.WriteLine($"Created test data at {DateTime.Now}");
				Writer.WriteLine();
				await CreateTestDataInternalAsync(Writer);
				return Ok(Writer.ToString());
			}
		}

		/// <summary>
		/// Populate the database with test data
		/// </summary>
		/// <returns>Async task</returns>
		async Task CreateTestDataInternalAsync(TextWriter Writer)
		{
			// Pools
			PoolId WindowsPoolId = new PoolId("windows");
			await PoolCollection.DeleteAsync(WindowsPoolId);
			IPool WindowsPool = await PoolCollection.AddAsync(WindowsPoolId, "Windows");
			Writer.WriteLine($"Created pool '{WindowsPool.Name}' with id {WindowsPool.Id}");

			PoolId MacPoolId = new PoolId("mac");
			await PoolCollection.DeleteAsync(MacPoolId);
			IPool MacPool = await PoolCollection.AddAsync(MacPoolId, "Mac");
			Writer.WriteLine($"Created pool '{MacPool.Name}' with id {MacPool.Id}");

			// Project
			ProjectId EngineProjectId = new ProjectId("ue4");
			await ProjectCollection.DeleteAsync(EngineProjectId);
			IProject EngineProject = (await ProjectCollection.TryAddAsync(EngineProjectId, "UE4"))!;
			Writer.WriteLine($"Created project '{EngineProject.Name}' with id {EngineProject.Id}");

			// Agent (when running locally)
			if (Environment.GetEnvironmentVariable("KUBERNETES_SERVICE_HOST") == null)
			{
				List<PoolId> AgentPools = new List<PoolId>();
				if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
				{
					AgentPools.Add(WindowsPool.Id);
				}
				else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
				{
					AgentPools.Add(MacPool.Id);
				}

				AgentId AgentId = new AgentId(Environment.MachineName);
				await AgentCollection.ForceDeleteAsync(AgentId);
				IAgent Agent = await AgentCollection.AddAsync(AgentId, true, false, null, AgentPools);
				Writer.WriteLine($"Created agent '{Agent.Id}');");

				// Dummy session history
				ObjectId SessionId = new ObjectId("5eab6c68122453ae272a6d98");
				await SessionCollection.DeleteAsync(SessionId);
				await SessionCollection.AddAsync(SessionId, Agent.Id, DateTime.UtcNow.AddHours(-1), null, new AgentSoftwareVersion("default"));
				Writer.WriteLine($"Created session with id {Agent.Id}");

				// Dummy lease history
				ObjectId LeaseId = new ObjectId("5eab6c858371995ff08dc21e");
				for (int x = -1; x >= -10; x--)
				{
					byte[] DummyPayload = new byte[] { 10, 34, 116, 121, 112, 101, 46, 103, 111, 111, 103, 108, 101, 97, 112, 105, 115, 46, 99, 111, 109, 47, 69, 120, 101, 99, 117, 116, 101, 74, 111, 98, 84, 97, 115, 107, 18, 32, 10, 24, 53, 101, 51, 50, 48, 51, 57, 55, 57, 102, 52, 55, 97, 100, 54, 102, 101, 48, 50, 98, 99, 51, 57, 53, 18, 4, 98, 97, 51, 54 };
					ILease Lease = await LeaseCollection.AddAsync(ObjectId.GenerateNewId(), "Test Job", Agent.Id, SessionId, null, null, null, DateTime.UtcNow.AddHours(x), DummyPayload);
					Writer.WriteLine($"Created lease '{Lease.Name}' with id {Lease.Id}");
				}
			}

			// Template
			ITemplate UnrealHeaderToolTemplate;
			{
				List<string> Arguments = new List<string>();
				Arguments.Add("-Script=Engine/Build/InstalledEngineBuild.xml");

				List<Parameter> Parameters = new List<Parameter>();
				Parameters.Add(new BoolParameter("Win64", "-Target=Compile UnrealHeaderTool Win64", null, true, null));
				Parameters.Add(new BoolParameter("Mac", "-Target=Compile UnrealHeaderTool Mac", null, true, null));

				UnrealHeaderToolTemplate = await TemplateCollection.AddAsync("UnrealHeaderTool (Win/Mac)", Priority.Normal, true, "Win64", null, new List<TemplateCounter>(), Arguments, Parameters);
				Writer.WriteLine($"Created template '{UnrealHeaderToolTemplate.Name}' with id {UnrealHeaderToolTemplate.Id}");
			}

			ITemplate InstalledBuildTemplate;
			{
				List<ListParameterItem> HostPlatformOptions = new List<ListParameterItem>();
				HostPlatformOptions.Add(new ListParameterItem(null, "Windows", "-Target=Make Installed Build Win64", null, true));
				HostPlatformOptions.Add(new ListParameterItem(null, "Mac", "-Target=Make Installed Build Mac", null, false));
				HostPlatformOptions.Add(new ListParameterItem(null, "Linux", "-Target=Make Installed Build Linux", null, false));

				List<ListParameterItem> TargetPlatformOptions = new List<ListParameterItem>();
				TargetPlatformOptions.Add(new ListParameterItem("Desktop", "Win64", "-set:WithWin64=true", "-set:WithWin64=false", true));
				TargetPlatformOptions.Add(new ListParameterItem("Desktop", "Win32", "-set:WithWin32=true", "-set:WithWin32=false", true));
				TargetPlatformOptions.Add(new ListParameterItem("Desktop", "Mac", "-set:WithMac=true", "-set:WithMac=false", false));
				TargetPlatformOptions.Add(new ListParameterItem("Mobile", "Android", "-set:WithAndroid=true", "-set:WithAndroid=false", true));
				TargetPlatformOptions.Add(new ListParameterItem("Mobile", "IOS", "-set:WithIOS=true", "-set:WithIOS=false", true));
				TargetPlatformOptions.Add(new ListParameterItem("Mobile", "TVOS", "-set:WithTVOS=true", "-set:WithTVOS=false", false));
				TargetPlatformOptions.Add(new ListParameterItem("Desktop", "Linux", "-set:WithLinux=true", "-set:WithLinux=false", true));
				TargetPlatformOptions.Add(new ListParameterItem("Desktop", "Linux (AArch64)", "-set:WithLinuxAArch64=true", "-set:WithLinuxAArch64=false", false));
				TargetPlatformOptions.Add(new ListParameterItem("Console", "PS4", "-set:WithPS4=true", null, false));
				TargetPlatformOptions.Add(new ListParameterItem("Console", "XboxOne", "-set:WithXboxOne=true", null, false));
				TargetPlatformOptions.Add(new ListParameterItem("Console", "Switch", "-set:WithSwitch=true", null, false));
				TargetPlatformOptions.Add(new ListParameterItem("XR", "Lumin (Win)", "-set:WithLumin=true", "-set:WithLumin=false", false));
				TargetPlatformOptions.Add(new ListParameterItem("XR", "Lumin (Mac)", "-set:WithLuminMac=true", "-set:WithLuminMac=false", false));
				TargetPlatformOptions.Add(new ListParameterItem("XR", "HoloLens", "-set:WithHoloLens=true", "-set:WithHoloLens=false", false));

				List<Parameter> Parameters = new List<Parameter>();
				Parameters.Add(new ListParameter("Host Platforms", ListParameterStyle.TagPicker, HostPlatformOptions, null));
				Parameters.Add(new ListParameter("Target Platforms", ListParameterStyle.MultiList, TargetPlatformOptions, null));
				Parameters.Add(new BoolParameter("Generate DDC", "-set:WithDDC=true", null, true, null));

				List<string> Arguments = new List<string>();
				Arguments.Add("-Script=Engine/Restricted/NotForLicensees/Build/InternalEngineBuild.xml");
				Arguments.Add("-set:RootPublishDir_Win=P:/Builds/UE4/Horde/Installed");
				Arguments.Add("-set:RootPublishDir_Mac=/Volumes/Root/Builds/UE4/Horde/Installed");

				InstalledBuildTemplate = await TemplateCollection.AddAsync("Installed Build", Priority.Normal, true, "Win64", null, new List<TemplateCounter>(), Arguments, Parameters);
				Writer.WriteLine($"Created template '{InstalledBuildTemplate.Name}' with id {InstalledBuildTemplate.Id}");
			}

			ITemplate TestExecutorTemplate;
			{
				List<string> Arguments = new List<string>();
				Arguments.Add("-Target=Publish FortniteClient Win64");

				TestExecutorTemplate = await TemplateCollection.AddAsync("Test Job", Priority.Normal, true, "Win64", null, new List<TemplateCounter>(), Arguments, new List<Parameter>());
				Writer.WriteLine($"Created template '{TestExecutorTemplate.Name}' with id {TestExecutorTemplate.Id}");
			}

			// Stream
			IStream Stream;
			{
				StringId<IStream> StreamId = StringId<IStream>.Sanitize("//UE4/Dev-Build");
				JobsTab InstalledBuildTab = new JobsTab("Installed Builds", false, new List<TemplateRefId> { TemplateRefId.Sanitize(InstalledBuildTemplate.Name) }, new List<string> { "Installed Build" }, new List<JobsTabColumn>());
				InstalledBuildTab.Columns!.Add(new JobsTabLabelColumn("Editors", "Host Platforms", 1));
				InstalledBuildTab.Columns!.Add(new JobsTabLabelColumn("Target Platforms", "Target Platforms", 2));

				JobsTab DefaultBuildTab = new JobsTab("Other Builds", true, null, null, null);

				const string WindowsTempStorageDir = @"\\epicgames.net\root\Builds\UE4\Horde\TempStorage";
				const string MacTempStorageDir = "/Volumes/Root/Builds/UE4/Horde/TempStorage";

				Dictionary<string, AgentType> AgentTypes = new Dictionary<string, AgentType>
				{
					["Win64"] = new AgentType(WindowsPool.Id, "Default", WindowsTempStorageDir),
					["Mac"] = new AgentType(MacPool.Id, "Default", MacTempStorageDir),
					["Win64_Licensee"] = new AgentType(WindowsPool.Id, "Default", WindowsTempStorageDir),
					["Mac_Licensee"] = new AgentType(MacPool.Id, "Default", MacTempStorageDir),
					["HoloLens"] = new AgentType(WindowsPool.Id, "Default", WindowsTempStorageDir),
					["Documentation"] = new AgentType(WindowsPool.Id, "Default", WindowsTempStorageDir)
				};
				Dictionary<string, WorkspaceType> WorkspaceTypes = new Dictionary<string, WorkspaceType>
				{
					["Default"] = new WorkspaceType { Stream = "//UE4/Dev-Build" }
				};
				List<StreamTab> Tabs = new List<StreamTab> { InstalledBuildTab, DefaultBuildTab };
				Dictionary<TemplateRefId, TemplateRef> TemplateRefs = new Dictionary<TemplateRefId, TemplateRef>
				{
					[TemplateRefId.Sanitize(InstalledBuildTemplate.Name)] = new TemplateRef(InstalledBuildTemplate, false, false, null, null, null),
					[TemplateRefId.Sanitize(UnrealHeaderToolTemplate.Name)] = new TemplateRef(UnrealHeaderToolTemplate, false, false, null, null, null),
					[TemplateRefId.Sanitize(TestExecutorTemplate.Name)] = new TemplateRef(TestExecutorTemplate, false, false, null, null, null)
				};
				DefaultBuildTab.Templates = new List<TemplateRefId>(TemplateRefs.Keys);
				await StreamCollection.DeleteAsync(StreamId);
				Stream = (await StreamCollection.TryCreateAsync(StreamId, "//UE4/Dev-Build", EngineProject.Id, Tabs: Tabs, AgentTypes: AgentTypes, WorkspaceTypes: WorkspaceTypes, TemplateRefs: TemplateRefs))!;
				Writer.WriteLine($"Created stream '{Stream.Name}' with id {Stream.Id}");
			}
		}

		/// <summary>
		/// Returns diagnostic information about the current state of the queue
		/// </summary>
		/// <returns>Information about the queue</returns>
		[HttpGet]
		[Route("/api/v1/debug/queue")]
		public async Task<ActionResult<object>> GetQueueStatusAsync()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			return await JobTaskSource.GetStatus();
		}

		/// <summary>
		/// Queries for all graphs
		/// </summary>
		/// <returns>The graph definitions</returns>
		[HttpGet]
		[Route("/api/v1/debug/graphs")]
		[ProducesResponseType(200, Type = typeof(GetGraphResponse))]
		public async Task<ActionResult<List<object>>> GetGraphsAsync([FromQuery] int? Index = null, [FromQuery] int? Count = null, [FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			List<IGraph> Graphs = await GraphCollection.FindAllAsync(null, Index, Count);
			return Graphs.ConvertAll(x => new GetGraphResponse(x).ApplyFilter(Filter));
		}

		/// <summary>
		/// Queries for a particular graph by hash
		/// </summary>
		/// <returns>The graph definition</returns>
		[HttpGet]
		[Route("/api/v1/debug/graphs/{GraphId}")]
		[ProducesResponseType(200, Type = typeof(GetGraphResponse))]
		public async Task<ActionResult<object>> GetGraphAsync(string GraphId, [FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			IGraph Graph = await GraphCollection.GetAsync(ContentHash.Parse(GraphId));
			return new GetGraphResponse(Graph).ApplyFilter(Filter);
		}

		/// <summary>
		/// Retrieves data from the configured storage provider
		/// </summary>
		/// <param name="Path">Filter for properties to return</param>
		/// <param name="Inline">Whether to show the data inline</param>
		/// <returns>Data from the given path</returns>
		[HttpGet]
		[Route("/api/v1/debug/storage")]
		[ProducesResponseType(typeof(List<GetTemplateResponse>), 200)]
		public async Task<ActionResult<object>> GetRawLogData(string Path, bool Inline = true)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			ReadOnlyMemory<byte>? Data = await StorageProvider.ReadAsync(Path);
			if(Data == null)
			{
				return NotFound();
			}

			Func<Stream, ActionContext, Task> CopyTask = async (OutputStream, Context) => await OutputStream.WriteAsync(Data.Value);
			string MimeType = Inline ? "text/plain" : "application/octet-stream";
			return new CustomFileCallbackResult(System.IO.Path.GetFileName(Path), MimeType, Inline, CopyTask);
		}

		/// <summary>
		/// Query all the job templates.
		/// </summary>
		/// <param name="Filter">Filter for properties to return</param>
		/// <returns>Information about all the job templates</returns>
		[HttpGet]
		[Route("/api/v1/debug/templates")]
		[ProducesResponseType(typeof(List<GetTemplateResponse>), 200)]
		public async Task<ActionResult<object>> GetTemplatesAsync([FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			List<ITemplate> Templates = await TemplateCollection.FindAllAsync();
			return Templates.ConvertAll(x => new GetTemplateResponse(x).ApplyFilter(Filter));
		}

		/// <summary>
		/// Retrieve information about a specific job template.
		/// </summary>
		/// <param name="TemplateHash">Id of the template to get information about</param>
		/// <param name="Filter">List of properties to return</param>
		/// <returns>Information about the requested template</returns>
		[HttpGet]
		[Route("/api/v1/debug/templates/{TemplateHash}")]
		[ProducesResponseType(typeof(GetTemplateResponse), 200)]
		public async Task<ActionResult<object>> GetTemplateAsync(string TemplateHash, [FromQuery] PropertyFilter? Filter = null)
		{
			ContentHash TemplateHashValue = ContentHash.Parse(TemplateHash);
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			ITemplate? Template = await TemplateCollection.GetAsync(TemplateHashValue);
			if (Template == null)
			{
				return NotFound();
			}
			return Template.ApplyFilter(Filter);
		}

		/// <summary>
		/// Retrieve metadata about a specific log file
		/// </summary>
		/// <param name="LogFileId">Id of the log file to get information about</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/debug/logs/{LogFileId}")]
		public async Task<ActionResult<object>> GetLogAsync(string LogFileId, [FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			ILogFile? LogFile = await LogFileCollection.GetLogFileAsync(LogFileId.ToObjectId());
			if (LogFile == null)
			{
				return NotFound();
			}

			return LogFile.ApplyFilter(Filter);
		}

		/// <summary>
		/// Populate the database with test data
		/// </summary>
		/// <returns>Async task</returns>
		[HttpGet]
		[Route("/api/v1/debug/collections/{Name}")]
		public async Task<ActionResult<object>> GetDocumentsAsync(string Name, [FromQuery] string? Filter = null, [FromQuery] int Index = 0, [FromQuery] int Count = 10)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			IMongoCollection<Dictionary<string, object>> Collection = DatabaseService.GetCollection<Dictionary<string, object>>(Name);
			List<Dictionary<string, object>> Documents = await Collection.Find(Filter ?? "{}").Skip(Index).Limit(Count).ToListAsync();
			return Documents;
		}
		
		/// <summary>
		/// Create P4 login ticket for the specified username
		/// </summary>
		/// <returns>Perforce ticket</returns>
		[HttpGet]
		[Route("/api/v1/debug/p4ticket")]
		public async Task<ActionResult<string>> CreateTicket([FromQuery] string? Username = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			if (Username == null)
			{
				return BadRequest();
			}

			return await Perforce.CreateTicket(Username);
		}
				
		/// <summary>
		/// Debugging fleet managers
		/// </summary>
		/// <returns>Nothing</returns>
		[HttpGet]
		[Route("/api/v1/debug/fleetmanager")]
		public async Task<ActionResult<string>> FleetManage([FromQuery] string? PoolId = null, [FromQuery] string? ChangeType = null, [FromQuery] int? Count = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}
			
			if (PoolId == null)
			{
				return BadRequest("Missing 'PoolId' query parameter");
			}

			IPool? Pool = await PoolCollection.GetAsync(new PoolId(PoolId));
			if (Pool == null)
			{
				return BadRequest($"Pool with ID '{PoolId}' not found");
			}
			
			if (!(ChangeType == "expand" || ChangeType == "shrink"))
			{
				return BadRequest("Missing 'ChangeType' query parameter and/or must be set to 'expand' or 'shrink'");
			}
			
			if (Count == null || Count.Value <= 0)
			{
				return BadRequest("Missing 'Count' query parameter and/or must be greater than 0");
			}

			List<IAgent> Agents = new List<IAgent>();
			string Message = "No operation";
				
			if (ChangeType == "expand")
			{
				await FleetManager.ExpandPool(Pool, Agents, Count.Value);
				Message = $"Expanded pool {Pool.Name} by {Count}";
			}
			else if (ChangeType == "shrink")
			{
				Agents = await AgentCollection.FindAsync();
				Agents = Agents.FindAll(a => a.InPool(Pool)).ToList();
				await FleetManager.ShrinkPool(Pool, Agents, Count.Value);
				Message = $"Shrank pool {Pool.Name} by {Count}";
			}
			
			return new ContentResult { ContentType = "text/plain", StatusCode = (int)HttpStatusCode.OK, Content = Message };
		}
	}
}
