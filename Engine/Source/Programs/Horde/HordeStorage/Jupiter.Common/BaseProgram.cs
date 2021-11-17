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
    public abstract class BaseHttpConnection
    {
        protected readonly IServiceProvider ServiceProvider;
        protected readonly PipeReader Reader;
        protected readonly PipeWriter Writer;
        protected readonly Socket Socket;

        public BaseHttpConnection(IServiceProvider serviceProvider, PipeReader reader, PipeWriter writer, Socket socket)
        {
            ServiceProvider = serviceProvider;
            Reader = reader;
            Writer = writer;
            Socket = socket;
        }

        /// <summary>
        /// A function that can process a connection.
        /// </summary>
        /// <param name="connection">A <see cref="ConnectionContext" /> representing the connection.</param>
        /// <returns>A <see cref="Task"/> that represents the connection lifetime. When the task completes, the connection will be closed.</returns>
        public abstract Task ExecuteAsync(ConnectionContext connection);
    }
    
    // ReSharper disable once UnusedMember.Global
    public static class BaseProgram<T> where T : BaseStartup
    {
        public delegate BaseHttpConnection HttpConnectionFactory(IServiceProvider sp, PipeReader reader, PipeWriter writer, Socket socket);

        private static IConfiguration Configuration { get; } = GetConfigurationBuilder();

        private static IConfiguration GetConfigurationBuilder()
        {
            string env = Environment.GetEnvironmentVariable("ASPNETCORE_ENVIRONMENT") ?? "Production";
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
                .AddYamlFile(Path.Combine(configRoot, "appsettings.Local.yaml"), optional: true, reloadOnChange: true)
                .AddEnvironmentVariables()
                .Build();

        }

        public static int BaseMain(string[] args, HttpConnectionFactory? httpConnectionFactory = null)
        {
            Log.Logger = new LoggerConfiguration()
                .ReadFrom.Configuration(Configuration)
                .CreateLogger();

            try
            {
                Log.Information("Creating ASPNET Host");
                CreateHostBuilder(args, httpConnectionFactory).Build().Run();
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

        private static IHostBuilder CreateHostBuilder(string[] args, HttpConnectionFactory? httpConnectionFactory)
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
