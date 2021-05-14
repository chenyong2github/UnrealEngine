// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Threading.Tasks;
using EpicGames.Core;
using HordeServer.Models;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Server.Kestrel.Core;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Driver;
using Serilog;
using Serilog.Configuration;
using Serilog.Events;
using Serilog.Formatting.Json;
using Serilog.Sinks.SystemConsole.Themes;

namespace HordeServer
{
	static class LoggerExtensions
	{
		public static LoggerConfiguration Console(this LoggerSinkConfiguration SinkConfig, ServerSettings Settings)
		{
			if (Settings.LogJsonToStdOut)
			{
				return SinkConfig.Console(new JsonFormatter(renderMessage: true));
			}
			else
			{
				ConsoleTheme Theme;
				if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows) && Environment.OSVersion.Version < new Version(10, 0))
				{
					Theme = SystemConsoleTheme.Literate;
				}
				else
				{
					Theme = AnsiConsoleTheme.Code;
				}
				return SinkConfig.Console(outputTemplate: "[{Timestamp:HH:mm:ss} {Level:w3}] {Indent}{Message:l}{NewLine}{Exception}", theme: Theme, restrictedToMinimumLevel: LogEventLevel.Debug);
			}
		}
	}

	class Program
	{
		public static DirectoryReference AppDir { get; } = new FileReference(Assembly.GetExecutingAssembly().Location).Directory;

		public static void Main(string[] Args)
		{
			IConfiguration Config = new ConfigurationBuilder()
				.SetBasePath(AppDir.FullName)
				.AddJsonFile("appsettings.json", optional: false)
				.AddJsonFile($"appsettings.{Environment.GetEnvironmentVariable("ASPNETCORE_ENVIRONMENT")}.json", optional: true)
				.AddJsonFile("appsettings.User.json", optional: true)
				.AddEnvironmentVariables()
				.Build();

			ServerSettings HordeSettings = new ServerSettings();
			Config.GetSection("Horde").Bind(HordeSettings);

			Serilog.Log.Logger = new LoggerConfiguration()
				.MinimumLevel.Debug()
//				.MinimumLevel.Override("HordeServer.Services.DatabaseService", LogEventLevel.Verbose) // For MongoDB query tracing
				.MinimumLevel.Override("Microsoft", LogEventLevel.Warning)
				.MinimumLevel.Override("Microsoft.AspNetCore", LogEventLevel.Warning)
				.MinimumLevel.Override("HordeServer.Authentication.OktaHandler", LogEventLevel.Warning)
				.MinimumLevel.Override("HordeServer.Authentication.OktaViaJwtHandler", LogEventLevel.Warning)
				.MinimumLevel.Override("HordeServer.Authentication.HordeJwtBearerHandler", LogEventLevel.Warning)
				.MinimumLevel.Override("HordeServer.Authentication.ServiceAccountAuthHandler", LogEventLevel.Warning)
				.MinimumLevel.Override("System.Net.Http.HttpClient", LogEventLevel.Warning)
				.MinimumLevel.Override("Grpc", LogEventLevel.Warning)
				.Enrich.FromLogContext()
				.WriteTo.Console(HordeSettings)
				.WriteTo.File(Path.Combine(AppDir.FullName, "Log.txt"), outputTemplate: "[{Timestamp:HH:mm:ss} {Level:w3}] {Indent}{Message:l}{NewLine}{Exception} [{SourceContext}]", rollingInterval: RollingInterval.Day, rollOnFileSizeLimit: true, fileSizeLimitBytes: 20 * 1024 * 1024, retainedFileCountLimit: 10)
				.WriteTo.File(new JsonFormatter(renderMessage: true), Path.Combine(AppDir.FullName, "Log.json"), rollingInterval: RollingInterval.Day, rollOnFileSizeLimit: true, fileSizeLimitBytes: 20 * 1024 * 1024, retainedFileCountLimit: 10)
				.CreateLogger();

			try
			{
				using (X509Certificate2? GrpcCertificate = ReadGrpcCertificate(HordeSettings))
				{
					CreateHostBuilderWithCert(Args, Config, GrpcCertificate).Build().Run();
				}
			}
#pragma warning disable CA1031 // Do not catch general exception types
			catch (Exception Ex)
#pragma warning restore CA1031 // Do not catch general exception types
			{
				Serilog.Log.Logger.Error(Ex, "Unhandled exception");
			}
		}

		// Used by WebApplicationFactory in controller tests. Uses reflection to call this exact function signature.
		public static IHostBuilder CreateHostBuilder(string[] Args)
		{
			return CreateHostBuilderWithCert(Args, new ConfigurationBuilder().Build(), null);
		}

		public static IHostBuilder CreateHostBuilderWithCert(string[] Args, IConfiguration Config, X509Certificate2? SslCert)
		{
			return Host.CreateDefaultBuilder(Args)
				.UseSerilog()
				.ConfigureAppConfiguration(Builder => Builder.AddConfiguration(Config))
				.ConfigureWebHostDefaults(WebBuilder => 
				{
					WebBuilder.ConfigureKestrel(Options =>
					{
						if (SslCert != null)
						{
							const int HttpPort = 80;
							Options.ListenAnyIP(HttpPort, Configure => { });
							
							const int Http2Port = 52103;
							Options.ListenAnyIP(Http2Port, Configure => { Configure.Protocols = HttpProtocols.Http2; });

							const int HttpsPort = 443;
							Options.ListenAnyIP(HttpsPort, Configure => { Configure.UseHttps(SslCert); });
						}
					});
					WebBuilder.UseStartup<Startup>(); 
				});
		}

		/// <summary>
		/// Gets the certificate to use for Grpc endpoints
		/// </summary>
		/// <returns>Custom certificate to use for Grpc endpoints, or null for the default.</returns>
		static X509Certificate2? ReadGrpcCertificate(ServerSettings HordeSettings)
		{
			if (HordeSettings.ServerPrivateCert == null)
			{
				return null;
			}
			else
			{
				return new X509Certificate2(FileReference.ReadAllBytes(FileReference.Combine(AppDir, HordeSettings.ServerPrivateCert)));
			}
		}
	}
}

