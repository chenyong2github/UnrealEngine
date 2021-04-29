// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using HordeServer;
using HordeServer.Collections.Impl;
using HordeServer.Services;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;

namespace HordeServerTests
{
    public class TestWebApplicationFactory<TStartup> : WebApplicationFactory<TStartup> where TStartup : class
    {
        protected override void ConfigureWebHost(IWebHostBuilder builder)
        {
            var Dict = new Dictionary<string, string?>
            {
                {"Horde:DatabaseConnectionString", DatabaseIntegrationTest.GetMongoDbRunner().GetConnectionString()},
                {"Horde:DatabaseName", DatabaseIntegrationTest.MongoDbDatabaseName},
                {"Horde:LogServiceWriteCacheType", "inmemory"},
                {"Horde:DisableAuth", "true"},
                {"Horde:OidcAuthority", null},
                {"Horde:OidcClientId", null},
            };
            builder.ConfigureAppConfiguration((hostingContext, config) => { config.AddInMemoryCollection(Dict); });
        }
    }

    public class ControllerIntegrationTest
    {
        private static HttpClient? _client;
        private static TestWebApplicationFactory<Startup>? _factory;
        protected readonly HttpClient client;
        private static Fixture? _fixture;

        public ControllerIntegrationTest()
        {
            client = GetClientForTestServer();
        }

        public static async Task<Fixture> GetFixture()
        {
            if (_fixture != null) return _fixture;

            IServiceProvider Services = GetFactory().Services;
            DatabaseService DatabaseService = Services.GetRequiredService<DatabaseService>();
            TemplateService TemplateService = Services.GetRequiredService<TemplateService>();
            JobService JobService = Services.GetRequiredService<JobService>();
            ArtifactService ArtifactService = Services.GetRequiredService<ArtifactService>();
            StreamService StreamService = Services.GetRequiredService<StreamService>();
            AgentService AgentService = Services.GetRequiredService<AgentService>();
            IPerforceService PerforceService = Services.GetRequiredService<IPerforceService>();
            GraphCollection GraphCollection = new GraphCollection(DatabaseService);

            _fixture = new Fixture();
            _fixture = await Fixture.Create(false, GraphCollection, TemplateService, JobService, ArtifactService, StreamService, AgentService, PerforceService);
            return _fixture;
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public static HttpClient GetClientForTestServer()
        {
            if (_client != null) return _client;

            _factory = GetFactory();
            _client = _factory.CreateClient();
            _client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Test");
            return _client;
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        static TestWebApplicationFactory<Startup> GetFactory()
        {
            if (_factory != null) return _factory;
            _factory = new TestWebApplicationFactory<Startup>();
            return _factory;
        }
    }
}