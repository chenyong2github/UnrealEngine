// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.IO;
using Callisto.Implementation;
using JsonSubTypes;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;

namespace Callisto
{
    // ReSharper disable once ClassNeverInstantiated.Global
    public class CallistoStartup : BaseStartup
    {
        public CallistoStartup(IConfiguration configuration, IWebHostEnvironment environment) : base(configuration, environment)
        {
        }

        protected override void OnAddService(IServiceCollection services)
        {
            services.AddOptions<CallistoSettings>().Configure(o => Configuration.GetSection("Callisto").Bind(o)).ValidateDataAnnotations();

            services.AddSingleton(serviceType: typeof(ITransactionLogs), TransactionLogFactory);
        }

        protected override void OnAddHealthChecks(IServiceCollection services, IHealthChecksBuilder healthChecks)
        {
            ServiceProvider provider = services.BuildServiceProvider();
            CallistoSettings settings = provider.GetService<IOptionsMonitor<CallistoSettings>>()!.CurrentValue;
            string? driveRoot = Path.GetPathRoot(settings!.TransactionLogRoot);
            const long expectedFreeMegaBytes = 512;
            healthChecks.AddDiskStorageHealthCheck(options => options.AddDrive(driveRoot, expectedFreeMegaBytes));
        }

        protected override void OnAddAuthorization(AuthorizationOptions authorizationOptions, List<string> defaultSchemes)
        {
            authorizationOptions.AddPolicy("TLog.read", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("transactionLog", "read", "readwrite", "full");
            });

            authorizationOptions.AddPolicy("TLog.write", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("transactionLog", "write", "readwrite", "full");
            });

            authorizationOptions.AddPolicy("TLog.delete", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("transactionLog", "delete", "full");
            });

            authorizationOptions.AddPolicy("Admin", policy =>
            {
                policy.AuthenticationSchemes = defaultSchemes;
                policy.RequireClaim("Admin");
            });
        }

        private ITransactionLogs TransactionLogFactory(IServiceProvider provider)
        {
            IOptionsMonitor<CallistoSettings> settings = provider.GetService<IOptionsMonitor<CallistoSettings>>()!;
            switch (settings.CurrentValue.TransactionLogImplementation)
            {
                case CallistoSettings.TransactionLogImplementations.File:
                    return new FileTransactionLogProxy(settings);
                case CallistoSettings.TransactionLogImplementations.Memory:
                    return new MemoryTransactionLogs();
                default:
                    throw new NotImplementedException();
            }
        }
    }

    public class CallistoSettings
    {
        public enum TransactionLogImplementations
        {
            File,
            Memory
        }

        [Required] public TransactionLogImplementations TransactionLogImplementation { get; set; }

        // Name of the current site, has to be globally unique across all deployments, used to avoid replication loops
        [Required] public string CurrentSite { get; set; } = "";

        [Required]
        public string TransactionLogRoot
        {
            get { return Environment.ExpandEnvironmentVariables(_transactionLogRoot); }
            set { _transactionLogRoot = value; }
        }

        public bool VerifySerialization { get; set; } = false;

        private string _transactionLogRoot = "";
    }
}
