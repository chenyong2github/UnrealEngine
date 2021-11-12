// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Reflection;
using System.Security.Claims;
using System.Threading.Tasks;
using Datadog.Trace;
using EpicGames.Core;
using HordeCommon;
using HordeServer;
using HordeServer.Collections;
using HordeServer.Collections.Impl;
using HordeServer.Controllers;
using HordeServer.Jobs;
using HordeServer.Logs;
using HordeServer.Logs.Builder;
using HordeServer.Logs.Readers;
using HordeServer.Logs.Storage.Impl;
using HordeServer.Models;
using HordeServer.Notifications;
using HordeServer.Notifications.Impl;
using HordeServer.Services;
using HordeServer.Services.Impl;
using HordeServer.Storage;
using HordeServer.Storage.Backends;
using HordeServer.Storage.Collections;
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
	public class TestSetup : DatabaseIntegrationTest
	{
		public IServiceProvider ServiceProvider { get; }
		public FakeClock Clock { get; set; } = null!;

		public IGraphCollection GraphCollection => ServiceProvider.GetRequiredService<IGraphCollection>();
		public INotificationTriggerCollection NotificationTriggerCollection => ServiceProvider.GetRequiredService<INotificationTriggerCollection>();
		public IStreamCollection StreamCollection => ServiceProvider.GetRequiredService<IStreamCollection>();
		public IProjectCollection ProjectCollection => ServiceProvider.GetRequiredService<IProjectCollection>();
		public IJobCollection JobCollection => ServiceProvider.GetRequiredService<IJobCollection>();
		public IAgentCollection AgentCollection => ServiceProvider.GetRequiredService<IAgentCollection>();
		public IJobStepRefCollection JobStepRefCollection => ServiceProvider.GetRequiredService<IJobStepRefCollection>();
		public IJobTimingCollection JobTimingCollection => ServiceProvider.GetRequiredService<IJobTimingCollection>();
		public IUgsMetadataCollection UgsMetadataCollection => ServiceProvider.GetRequiredService<IUgsMetadataCollection>();
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
		public ITemplateCollection TemplateCollection => ServiceProvider.GetRequiredService<ITemplateCollection>();
		internal PerforceServiceStub PerforceService => (PerforceServiceStub)ServiceProvider.GetRequiredService<IPerforceService>();
		public ILogFileService LogFileService => ServiceProvider.GetRequiredService<ILogFileService>();
		public ProjectService ProjectService => ServiceProvider.GetRequiredService<ProjectService>();
		public StreamService StreamService => ServiceProvider.GetRequiredService<StreamService>();
		public ISubscriptionCollection SubscriptionCollection => ServiceProvider.GetRequiredService<ISubscriptionCollection>();
		public INotificationService NotificationService => ServiceProvider.GetRequiredService<INotificationService>();
		public IIssueService IssueService => ServiceProvider.GetRequiredService<IIssueService>();
		public JobTaskSource JobTaskSource => ServiceProvider.GetRequiredService<JobTaskSource>();
		public JobService JobService => ServiceProvider.GetRequiredService<JobService>();
		public IArtifactCollection ArtifactCollection => ServiceProvider.GetRequiredService<IArtifactCollection>();
		public IDowntimeService DowntimeService => ServiceProvider.GetRequiredService<IDowntimeService>();
		public RpcService RpcService => ServiceProvider.GetRequiredService<RpcService>();
		public CredentialService CredentialService => ServiceProvider.GetRequiredService<CredentialService>();
		public PoolService PoolService => ServiceProvider.GetRequiredService<PoolService>();
		public LifetimeService LifetimeService => ServiceProvider.GetRequiredService<LifetimeService>();
		public ScheduleService ScheduleService => ServiceProvider.GetRequiredService<ScheduleService>();

		public ServerSettings ServerSettings => ServiceProvider.GetRequiredService<IOptions<ServerSettings>>().Value;
		public IOptionsMonitor<ServerSettings> ServerSettingsMon => ServiceProvider.GetRequiredService<IOptionsMonitor<ServerSettings>>();

		public JobsController JobsController => GetJobsController();
		public AgentsController AgentsController => GetAgentsController();

		private static bool IsDatadogWriterPatched;

		public TestSetup()
		{
			PatchDatadogWriter();

			IConfiguration Config = new ConfigurationBuilder().Build();

			IServiceCollection Services = new ServiceCollection();
			Services.AddSingleton(GetDatabaseService());
			Services.Configure<ServerSettings>(ConfigureSettings);
			Services.AddSingleton<IConfiguration>(Config);

			ConfigureServices(Services);

			ServiceProvider = Services.BuildServiceProvider();
		}

		protected virtual void ConfigureSettings(ServerSettings Settings)
		{
			DirectoryReference BaseDir = DirectoryReference.Combine(Program.DataDir, "Tests");
			try
			{
				FileUtils.ForceDeleteDirectoryContents(BaseDir);
			}
			catch
			{
			}

			Settings.AdminClaimType = HordeClaimTypes.Role;
			Settings.AdminClaimValue = "app-horde-admins";
		}

		protected virtual void ConfigureServices(IServiceCollection Services)
		{
			Services.AddLogging(Builder => Builder.AddConsole());

			Services.AddSingleton<RedisService>();
			Services.AddSingleton<StackExchange.Redis.IDatabase>(new Mock<StackExchange.Redis.IDatabase>().Object);

			Services.AddSingleton(typeof(IAuditLogFactory<>), typeof(AuditLogFactory<>));
			Services.AddSingleton<IAuditLog<AgentId>>(SP => SP.GetRequiredService<IAuditLogFactory<AgentId>>().Create("Agents.Log", "AgentId"));

			Services.AddSingleton<IAgentCollection, AgentCollection>();
			Services.AddSingleton<IAgentSoftwareCollection, AgentSoftwareCollection>();
			Services.AddSingleton<IArtifactCollection, ArtifactCollection>();
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
			Services.AddSingleton<IStreamCollection, StreamCollection>();
			Services.AddSingleton<ITemplateCollection, TemplateCollection>();
			Services.AddSingleton<ITestDataCollection, TestDataCollection>();
			Services.AddSingleton<ITelemetryCollection, TelemetryCollection>();
			Services.AddSingleton<ITemplateCollection, TemplateCollection>();
			Services.AddSingleton<IUgsMetadataCollection, UgsMetadataCollection>();
			Services.AddSingleton<IUserCollection, UserCollectionV1>();

			Clock = new FakeClock();
			Services.AddSingleton<IClock>(Clock);
			Services.AddSingleton<IHostApplicationLifetime, AppLifetimeStub>();

			Services.AddSingleton<AclService>();
			Services.AddSingleton<AgentService>();
			Services.AddSingleton<AgentSoftwareService>();
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
			Services.AddSingleton<StreamService>();
			Services.AddSingleton<UpgradeService>();

			Services.AddSingleton<ConformTaskSource>();

			Services.AddSingleton<IBlobCollection, BlobCollection>();
			Services.AddSingleton<IObjectCollection, ObjectCollection>();
			Services.AddSingleton<IRefCollection, RefCollection>();
			Services.AddSingleton<INamespaceCollection, NamespaceCollection>();
			Services.AddSingleton<IBucketCollection, BucketCollection>();

			Services.AddSingleton(new Startup.StorageBackendSettings<PersistentLogStorage> { Type = StorageProviderType.Transient });
			Services.AddSingleton(new Startup.StorageBackendSettings<ArtifactCollection> { Type = StorageProviderType.Transient });
			Services.AddSingleton(new Startup.StorageBackendSettings<BlobCollection> { Type = StorageProviderType.Transient });
			Services.AddSingleton(typeof(IStorageBackend<>), typeof(Startup.StorageBackendFactory<>));

			Services.AddSingleton<IStorageService, SimpleStorageService>();

			Services.AddSingleton<ISingletonDocument<GlobalPermissions>>(new SingletonDocumentStub<GlobalPermissions>());
			Services.AddSingleton<ISingletonDocument<AgentSoftwareChannels>>(new SingletonDocumentStub<AgentSoftwareChannels>());
		}

		public Task<Fixture> CreateFixtureAsync()
		{
			return Fixture.Create(GraphCollection, TemplateCollection, JobService, ArtifactCollection, StreamService, AgentService, PerforceService);
		}

		private JobsController GetJobsController()
        {
			ILogger<JobsController> Logger = ServiceProvider.GetRequiredService<ILogger<JobsController>>();
			JobsController JobsCtrl = new JobsController(AclService, GraphCollection, PerforceService, StreamService, JobService,
		        TemplateCollection, ArtifactCollection, UserCollection, NotificationService, Logger);
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