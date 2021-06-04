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
using System.Text.Json;
using System.Threading.Tasks;
using EpicGames.Core;
using HordeServer.Models;
using HordeServer.Utilities;
using Json.Schema;
using Json.Schema.Generation;
using Json.Schema.Generation.Intents;
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
		public static DirectoryReference AppDir { get; } = GetAppDir();

		public static DirectoryReference DataDir { get; } = GetDefaultDataDir();

		public static FileReference UserConfigFile { get; } = FileReference.Combine(GetDefaultDataDir(), "Horde.json");

		public static Type[] ConfigSchemas = FindSchemaTypes();

		static Type[] FindSchemaTypes()
		{
			List<Type> SchemaTypes = new List<Type>();
			foreach (Type Type in Assembly.GetExecutingAssembly().GetTypes())
			{
				if (Type.GetCustomAttribute<JsonSchemaAttribute>() != null)
				{
					SchemaTypes.Add(Type);
				}
			}
			return SchemaTypes.ToArray();
		}

		public static void Main(string[] Args)
		{
			CommandLineArguments Arguments = new CommandLineArguments(Args);

			IConfiguration Config = new ConfigurationBuilder()
				.SetBasePath(AppDir.FullName)
				.AddJsonFile("appsettings.json", optional: false)
				.AddJsonFile($"appsettings.{Environment.GetEnvironmentVariable("ASPNETCORE_ENVIRONMENT")}.json", optional: true)
				.AddJsonFile("appsettings.User.json", optional: true)
				.AddJsonFile(UserConfigFile.FullName, optional: true)
				.AddEnvironmentVariables()
				.AddCommandLine(Args)
				.Build();

			ServerSettings HordeSettings = new ServerSettings();
			Config.GetSection("Horde").Bind(HordeSettings);

			DirectoryReference LogDir = AppDir;
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				LogDir = DirectoryReference.Combine(DataDir);
			}

			Serilog.Log.Logger = new LoggerConfiguration()
				.Enrich.FromLogContext()
				.WriteTo.Console(HordeSettings)
				.WriteTo.File(Path.Combine(LogDir.FullName, "Log.txt"), outputTemplate: "[{Timestamp:HH:mm:ss} {Level:w3}] {Indent}{Message:l}{NewLine}{Exception} [{SourceContext}]", rollingInterval: RollingInterval.Day, rollOnFileSizeLimit: true, fileSizeLimitBytes: 20 * 1024 * 1024, retainedFileCountLimit: 10)
				.WriteTo.File(new JsonFormatter(renderMessage: true), Path.Combine(LogDir.FullName, "Log.json"), rollingInterval: RollingInterval.Day, rollOnFileSizeLimit: true, fileSizeLimitBytes: 20 * 1024 * 1024, retainedFileCountLimit: 10)
				.ReadFrom.Configuration(Config)
				.CreateLogger();

			if (Arguments.HasOption("-UpdateSchemas"))
			{
				DirectoryReference? SchemaDir = Arguments.GetDirectoryReferenceOrDefault("-SchemaDir=", null);
				if (SchemaDir == null)
				{
					SchemaDir = new DirectoryReference("Schemas");
				}

				Arguments.CheckAllArgumentsUsed();

				DirectoryReference.CreateDirectory(SchemaDir);
				foreach (Type SchemaType in ConfigSchemas)
				{
					FileReference OutputFile = FileReference.Combine(SchemaDir, $"{SchemaType.Name}.json");
					Schemas.WriteSchema(SchemaType, OutputFile);
				}
				return;
			}

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

		/// <summary>
		/// Get the application directory
		/// </summary>
		/// <returns></returns>
		static DirectoryReference GetAppDir()
		{
			return new FileReference(Assembly.GetExecutingAssembly().Location).Directory;
		}

		/// <summary>
		/// Gets the default directory for storing application data
		/// </summary>
		/// <returns>The default data directory</returns>
		static DirectoryReference GetDefaultDataDir()
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				DirectoryReference? Dir = DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.CommonApplicationData);
				if (Dir != null)
				{
					return DirectoryReference.Combine(Dir, "HordeServer");
				}
			}
			return DirectoryReference.Combine(GetAppDir(), "Data");
		}
	}
}

