// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using HordeServer;
using HordeServer.Collections;
using HordeServer.Collections.Impl;
using HordeServer.Jobs;
using HordeServer.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;

namespace HordeServerTests
{
	public class TestWebApplicationFactory<TStartup> : WebApplicationFactory<TStartup> where TStartup : class
    {
		MongoDbInstance MongoDbInstance;

		public TestWebApplicationFactory(MongoDbInstance MongoDbInstance)
		{
			this.MongoDbInstance = MongoDbInstance;
		}

		protected override void ConfigureWebHost(IWebHostBuilder builder)
        {
            var Dict = new Dictionary<string, string?>
            {
                {"Horde:DatabaseConnectionString", MongoDbInstance.ConnectionString},
                {"Horde:DatabaseName", MongoDbInstance.DatabaseName},
                {"Horde:LogServiceWriteCacheType", "inmemory"},
                {"Horde:DisableAuth", "true"},
                {"Horde:OidcAuthority", null},
                {"Horde:OidcClientId", null},
            };
            builder.ConfigureAppConfiguration((hostingContext, config) => { config.AddInMemoryCollection(Dict); });
			builder.ConfigureTestServices(Collection => Collection.AddSingleton<IPerforceService, Stubs.Services.PerforceServiceStub>());
        }
    }

    public class ControllerIntegrationTest : IDisposable
    {
		protected MongoDbInstance MongoDbInstance { get; }
		TestWebApplicationFactory<Startup> _factory { get; }
		protected HttpClient client { get; }
		private Lazy<Task<Fixture>> _fixture;

		public ControllerIntegrationTest()
		{
			MongoDbInstance = new MongoDbInstance();
			_factory = new TestWebApplicationFactory<Startup>(MongoDbInstance);
			client = _factory.CreateClient();

			_fixture = new Lazy<Task<Fixture>>(Task.Run(() => CreateFixture()));
		}

		public void Dispose()
		{
			MongoDbInstance.Dispose();
		}

		public Task<Fixture> GetFixture()
		{
			return _fixture.Value;
		}

		async Task<Fixture> CreateFixture()
		{
			IServiceProvider Services = _factory.Services;
			DatabaseService DatabaseService = Services.GetRequiredService<DatabaseService>();
			ITemplateCollection TemplateService = Services.GetRequiredService<ITemplateCollection>();
			JobService JobService = Services.GetRequiredService<JobService>();
			IArtifactCollection ArtifactCollection = Services.GetRequiredService<IArtifactCollection>();
			StreamService StreamService = Services.GetRequiredService<StreamService>();
			AgentService AgentService = Services.GetRequiredService<AgentService>();
			IPerforceService PerforceService = Services.GetRequiredService<IPerforceService>();
			GraphCollection GraphCollection = new GraphCollection(DatabaseService);

			return await Fixture.Create(GraphCollection, TemplateService, JobService, ArtifactCollection, StreamService, AgentService, PerforceService);
        }
    }
}