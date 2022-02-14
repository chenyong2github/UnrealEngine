// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipelines;
using System.Net.Sockets;
using System.Reflection;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Connections;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Serilog;

namespace Jupiter
{
    // ReSharper disable once UnusedMember.Global
    public static class BaseProgram<T> where T : BaseStartup
    {
        private static IConfiguration Configuration { get; } = GetConfigurationBuilder();

        private static IConfiguration GetConfigurationBuilder()
        {
            string env = Environment.GetEnvironmentVariable("ASPNETCORE_ENVIRONMENT") ?? "Production";
            string mode = Environment.GetEnvironmentVariable("HORDESTORAGE_MODE") ?? "DefaultMode";
            string configRoot = "/config";
            // set the config root to config under the current directory for windows
            if (OperatingSystem.IsWindows())
            {
                configRoot = Path.Combine(Directory.GetCurrentDirectory(), "config");
            }
            return new ConfigurationBuilder()
                .SetBasePath(Directory.GetCurrentDirectory())
                .AddJsonFile("appsettings.json", false, false)
                .AddJsonFile(
                    path:
                    $"appsettings.{env}.json",
                    true)
                .AddYamlFile(
                    path:
                    $"appsettings.{mode}.yaml",
                    true)
                .AddYamlFile(Path.Combine(configRoot, "appsettings.Local.yaml"), optional: true, reloadOnChange: true)
                .AddEnvironmentVariables()
                .Build();

        }

        public static int BaseMain(string[] args)
        {
            Log.Logger = new LoggerConfiguration()
                .ReadFrom.Configuration(Configuration)
                .CreateLogger();

            try
            {
                Log.Information("Creating ASPNET Host");
                CreateHostBuilder(args).Build().Run();
                return 0;
            }
            catch (Exception ex)
            {
                Log.Fatal(ex, "Host terminated unexpectedly");
                return 1;
            }
            finally
            {
                Log.CloseAndFlush();
            }
        }

        public static IHostBuilder CreateHostBuilder(string[] args)
        {
            return Host.CreateDefaultBuilder(args)
                .ConfigureWebHostDefaults(webBuilder =>
                {
                    webBuilder.UseStartup<T>();
                    webBuilder.UseConfiguration(Configuration);
                    webBuilder.UseSerilog();
                    // configure microsoft.extensions.logging to configure log4net to allow us to set it in our appsettings
                    webBuilder.ConfigureLogging((hostingContext, logging) =>
                    {
                        // configure log4net (used by aws sdk) to write to serilog so we get the logs in the system we want it in
                        Log4net.Appender.Serilog.Configuration.Configure();
                    });
                    // remove the server header from kestrel
                    webBuilder.ConfigureKestrel(options =>
                    {
                        options.AddServerHeader = false;
                    });
                });
        }
    }
}
