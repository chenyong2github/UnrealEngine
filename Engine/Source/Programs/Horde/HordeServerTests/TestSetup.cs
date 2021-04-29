using System;
using System.Collections.Generic;
using System.Reflection;
using System.Security.Claims;
using System.Threading.Tasks;
using Datadog.Trace;
using HordeCommon;
using HordeServer;
using HordeServer.Collections;
using HordeServer.Collections.Impl;
using HordeServer.Controllers;
using HordeServer.Logs;
using HordeServer.Logs.Builder;
using HordeServer.Logs.Storage.Impl;
using HordeServer.Models;
using HordeServer.Notifications;
using HordeServer.Notifications.Impl;
using HordeServer.Rpc;
using HordeServer.Services;
using HordeServer.Services.Impl;
using HordeServer.Storage;
using HordeServer.Storage.Impl;
using HordeServer.Storage.Services;
using HordeServer.Tasks.Impl;
using HordeServer.Utilities;
using HordeServerTests.Stubs.Collections;
using HordeServerTests.Stubs.Services;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Logging.Console;
using Microsoft.Extensions.Options;
using Moq;
using StatsdClient;

namespace HordeServerTests
{
	/// <summary>
	/// Handles set up of collections, services, fixtures etc during testing
	///
	/// Easier to pass all these things around in a single object.
	/// </summary>
	public class TestSetup
	{
		public IServiceProvider ServiceProvider { get; }
		public FakeClock Clock { get; }

		public IGraphCollection GraphCollection => ServiceProvider.GetRequiredService<IGraphCollection>();
		public INotificationTriggerCollection NotificationTriggerCollection => ServiceProvider.GetRequiredService<INotificationTriggerCollection>();
		public IStreamCollection StreamCollection => ServiceProvider.GetRequiredService<IStreamCollection>();
		public IProjectCollection ProjectCollection => ServiceProvider.GetRequiredService<IProjectCollection>();
		public IJobCollection JobCollection => ServiceProvider.GetRequiredService<IJobCollection>();
		public IAgentCollection AgentCollection => ServiceProvider.GetRequiredService<IAgentCollection>();
		public IJobStepRefCollection JobStepRefCollection => ServiceProvider.GetRequiredService<IJobStepRefCollection>();
		public IJobTimingCollection JobTimingCollection => ServiceProvider.GetRequiredService<IJobTimingCollection>();
		public IUgsMetadataCollection UgsMetadataCollection => ServiceProvider.GetRequiredService<IUgsMetadataCollection>();
		public ICounterCollection CounterCollection => ServiceProvider.GetRequiredService <ICounterCollection>();
		public IIssueCollection IssueCollection => ServiceProvider.GetRequiredService <IIssueCollection>();
		public IPoolCollection PoolCollection => ServiceProvider.GetRequiredService <IPoolCollection>();
		public ILeaseCollection LeaseCollection => ServiceProvider.GetRequiredService <ILeaseCollection>();
		public ISessionCollection SessionCollection => ServiceProvider.GetRequiredService <ISessionCollection>();
		public IAgentSoftwareCollection AgentSoftwareCollection => ServiceProvider.GetRequiredService <IAgentSoftwareCollection>();
		public ITestDataCollection TestDataCollection => ServiceProvider.GetRequiredService<ITestDataCollection>();
		public IUserCollection UserCollection => ServiceProvider.GetRequiredService<IUserCollection>();

		public AclService AclService => ServiceProvider.GetRequiredService<AclService>();
		public AgentSoftwareService AgentSoftwareService => ServiceProvider.GetRequiredService<AgentSoftwareService>();
		public AgentService AgentService => ServiceProvider.GetRequiredService<AgentService>();
		public DatabaseService DatabaseService => ServiceProvider.GetRequiredService<DatabaseService>();
		public TemplateService TemplateService => ServiceProvider.GetRequiredService<TemplateService>();
		internal PerforceServiceStub PerforceService => (PerforceServiceStub)ServiceProvider.GetRequiredService<IPerforceService>();
		public ILogFileService LogFileService => ServiceProvider.GetRequiredService<ILogFileService>();
		public ProjectService ProjectService => ServiceProvider.GetRequiredService<ProjectService>();
		public StreamService StreamService => ServiceProvider.GetRequiredService<StreamService>();
		public ISubscriptionCollection SubscriptionCollection => ServiceProvider.GetRequiredService<ISubscriptionCollection>();
		public INotificationService NotificationService => ServiceProvider.GetRequiredService<INotificationService>();
		public IIssueService IssueService => ServiceProvider.GetRequiredService<IIssueService>();
		public JobTaskSource JobTaskSource => ServiceProvider.GetRequiredService<JobTaskSource>();
		public JobService JobService => ServiceProvider.GetRequiredService<JobService>();
		public ArtifactService ArtifactService => ServiceProvider.GetRequiredService<ArtifactService>();
		public IDowntimeService DowntimeService => ServiceProvider.GetRequiredService<IDowntimeService>();
		public RpcService RpcService => ServiceProvider.GetRequiredService<RpcService>();
		public CredentialService CredentialService => ServiceProvider.GetRequiredService<CredentialService>();
		public PoolService PoolService => ServiceProvider.GetRequiredService<PoolService>();
		public LifetimeService LifetimeService => ServiceProvider.GetRequiredService<LifetimeService>();

		public ServerSettings ServerSettings => ServiceProvider.GetRequiredService<IOptions<ServerSettings>>().Value;
		public IOptionsMonitor<ServerSettings> ServerSettingsMon => ServiceProvider.GetRequiredService<IOptionsMonitor<ServerSettings>>();
		public Fixture? Fixture;

		public JobsController JobsController => GetJobsController();
		public AgentsController AgentsController => GetAgentsController();

		private static bool IsDatadogWriterPatched;

		public TestSetup(DatabaseService DbService)
		{
			PatchDatadogWriter();

			IConfiguration Config = new ConfigurationBuilder().Build();

			IServiceCollection Services = new ServiceCollection();
			Services.Configure<ServerSettings>(Settings =>
			{
				Settings.AdminClaimType = HordeClaimTypes.Role;
				Settings.AdminClaimValue = "app-horde-admins";
			});
			Services.AddLogging(Builder => Builder.AddConsole());
			Services.AddSingleton(DbService);
			Services.AddSingleton<IConfiguration>(Config);

			Services.AddSingleton<StackExchange.Redis.IDatabase>(new Mock<StackExchange.Redis.IDatabase>().Object);

			Services.AddSingleton<IAgentCollection, AgentCollection>();
			Services.AddSingleton<IAgentSoftwareCollection, AgentSoftwareCollection>();
			Services.AddSingleton<ICounterCollection, CounterCollection>();
			Services.AddSingleton<IGraphCollection, GraphCollection>();
			Services.AddSingleton<IIssueCollection, IssueCollection>();
			Services.AddSingleton<IJobCollection, JobCollection>();
			Services.AddSingleton<IJobStepRefCollection, JobStepRefCollection>();
			Services.AddSingleton<IJobTimingCollection, JobTimingCollection>();
			Services.AddSingleton<ILeaseCollection, LeaseCollection>();
			Services.AddSingleton<ILogEventCollection, LogEventCollection>();
			Services.AddSingleton<ILogFileCollection, LogFileCollection>();
			Services.AddSingleton<INotificationTriggerCollection, NotificationTriggerCollection>();
			Services.AddSingleton<IPoolCollection, PoolCollection>();
			Services.AddSingleton<IProjectCollection, ProjectCollection>();
			Services.AddSingleton<ISessionCollection, SessionCollection>();
			Services.AddSingleton<ISubscriptionCollection, SubscriptionCollection>();
			Services.AddSingleton<ISoftwareCollection, SoftwareCollection>();
			Services.AddSingleton<IStreamCollection, StreamCollection>();
			Services.AddSingleton<ITemplateCollection, TemplateCollection>();
			Services.AddSingleton<ITestDataCollection, TestDataCollection>();
			Services.AddSingleton<ITelemetryCollection, TelemetryCollection>();
			Services.AddSingleton<IUgsMetadataCollection, UgsMetadataCollection>();
			Services.AddSingleton<IUserCollection, UserCollectionV1>();

			Clock = new FakeClock();
			Services.AddSingleton<IClock>(Clock);
			Services.AddSingleton<IHostApplicationLifetime, AppLifetimeStub>();

			Services.AddSingleton<AclService>();
			Services.AddSingleton<AgentService>();
			Services.AddSingleton<AgentSoftwareService>();
			Services.AddSingleton<ArtifactService>();
			Services.AddSingleton<AutoscaleService>();
			Services.AddSingleton<AwsFleetManager, AwsFleetManager>();
			Services.AddSingleton<ConsistencyService>();
			Services.AddSingleton<RequestTrackerService>();
			Services.AddSingleton<CredentialService>();
			Services.AddSingleton<JobTaskSource>();
			Services.AddSingleton<IDowntimeService, DowntimeServiceStub>();
			Services.AddSingleton<IDogStatsd, NoOpDogStatsd>();
			Services.AddSingleton<IIssueService, IssueService>();
			Services.AddSingleton<IFleetManager, AwsFleetManager>();
			Services.AddSingleton<JobService>();
			Services.AddSingleton<LifetimeService>();
			Services.AddSingleton<ILogStorage, NullLogStorage>();
			Services.AddSingleton<ILogBuilder, LocalLogBuilder>();
			Services.AddSingleton<ILogFileService, LogFileService>();
			Services.AddSingleton<INotificationService, NotificationService>();
			Services.AddSingleton<IPerforceService, PerforceServiceStub>();
			Services.AddSingleton<PerforceLoadBalancer>();
			Services.AddSingleton<PoolService>();
			Services.AddSingleton<ProjectService>();
			Services.AddSingleton<RpcService>();
			Services.AddSingleton<ScheduleService>();
			Services.AddSingleton<SoftwareService>();
			Services.AddSingleton<StreamService>();
			Services.AddSingleton<TemplateService>();
			Services.AddSingleton<UpgradeService>();

			Services.AddSingleton<ActionTaskSource>();
			Services.AddSingleton<ConformTaskSource>();

			Services.AddSingleton<ActionCacheService>();
			Services.AddSingleton<ByteStreamService>();
			Services.AddSingleton<CapabilitiesService>();
			Services.AddSingleton<ContentStorageService>();
			Services.AddSingleton<ExecutionService>();

			Services.AddSingleton<FileSystemStorageBackend>();
			Services.AddSingleton<IStorageService, SimpleStorageService>(SP => new SimpleStorageService(SP.GetRequiredService<FileSystemStorageBackend>()));

			Services.AddSingleton<ISingletonDocument<GlobalPermissions>>(new SingletonDocumentStub<GlobalPermissions>());
			Services.AddSingleton<ISingletonDocument<AgentSoftwareChannels>>(new SingletonDocumentStub<AgentSoftwareChannels>());

			ServiceProvider = Services.BuildServiceProvider();
		}

		public async Task CreateFixture(bool ForceNewFixture = false)
		{
			if (!DatabaseService.ReadOnlyMode)
			{
				Fixture = await Fixture.Create(ForceNewFixture, GraphCollection, TemplateService, JobService, ArtifactService, StreamService, AgentService, PerforceService);
			}
		}

		private JobsController GetJobsController()
        {
			ILogger<JobsController> Logger = ServiceProvider.GetRequiredService<ILogger<JobsController>>();
			JobsController JobsCtrl = new JobsController(AclService, GraphCollection, PerforceService, StreamService, JobService,
		        TemplateService, ArtifactService, NotificationService, Logger);
	        JobsCtrl.ControllerContext = GetControllerContext();
	        return JobsCtrl;
        }

		private AgentsController GetAgentsController()
		{
			AgentsController AgentCtrl = new AgentsController(AclService, AgentService, PoolService);
			AgentCtrl.ControllerContext = GetControllerContext();
			return AgentCtrl;
		}
		
		private ControllerContext GetControllerContext()
		{
			ControllerContext ControllerContext = new ControllerContext();
			ControllerContext.HttpContext = new DefaultHttpContext();
			ControllerContext.HttpContext.User = new ClaimsPrincipal(new ClaimsIdentity(
				new List<Claim> {new Claim(ServerSettings.AdminClaimType, ServerSettings.AdminClaimValue)}, "TestAuthType"));
			return ControllerContext;
		}

		/// <summary>
		/// Hack the Datadog tracing library to not block during shutdown of tests.
		/// Without this fix, the lib will try to send traces to a host that isn't running and block for +20 secs
		///
		/// Since so many of the interfaces and classes in the lib are internal it was difficult to replace Tracer.Instance
		/// </summary>
		private void PatchDatadogWriter()
		{
			if (IsDatadogWriterPatched)
			{
				return;
			}

			IsDatadogWriterPatched = true;

			string Msg = "Unable to patch Datadog agent writer! Tests will still work, but shutdown will block for +20 seconds.";
			
			FieldInfo? AgentWriterField = Tracer.Instance.GetType().GetField("_agentWriter", BindingFlags.NonPublic | BindingFlags.Instance);
			if (AgentWriterField == null)
			{
				Console.Error.WriteLine(Msg);
				return;
			}
			
			object? AgentWriterInstance = AgentWriterField.GetValue(Tracer.Instance);
			if (AgentWriterInstance == null)
			{
				Console.Error.WriteLine(Msg);
				return;	
			}
	        
			FieldInfo? ProcessExitField = AgentWriterInstance.GetType().GetField("_processExit", BindingFlags.NonPublic | BindingFlags.Instance);
			if (ProcessExitField == null)
			{
				Console.Error.WriteLine(Msg);
				return;
			}
			
			TaskCompletionSource<bool>? ProcessExitInstance = (TaskCompletionSource<bool>?) ProcessExitField.GetValue(AgentWriterInstance);
			if (ProcessExitInstance == null)
			{
				Console.Error.WriteLine(Msg);
				return;
			}
			
			ProcessExitInstance.TrySetResult(true);
		}
		
		private class DowntimeServiceStub : IDowntimeService
		{
			public bool IsDowntimeActive { get; } = false;
		}
	}
}