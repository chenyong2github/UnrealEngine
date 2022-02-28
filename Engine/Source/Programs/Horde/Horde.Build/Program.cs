// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Channels;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Server.Kestrel.Core;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Driver;
using OpenTracing;
using OpenTracing.Util;
using Serilog;
using Serilog.Configuration;
using Serilog.Core;
using Serilog.Enrichers.OpenTracing;
using Serilog.Events;
using Serilog.Filters;
using Serilog.Formatting.Json;
using Serilog.Sinks.SystemConsole.Themes;
using OpenTracing.Propagation;
using Microsoft.Extensions.DependencyInjection;
using Horde.Build.Commands;

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

		public static LoggerConfiguration WithHordeConfig(this LoggerConfiguration Configuration, ServerSettings Settings)
		{
			if (Settings.WithDatadog)
			{
				Configuration = Configuration.Enrich.With<DatadogLogEnricher>();
			}
			return Configuration;
		}
	}

	class DatadogLogEnricher : ILogEventEnricher
	{
		public void Enrich(Serilog.Events.LogEvent LogEvent, ILogEventPropertyFactory PropertyFactory)
		{
			ISpan? Span = GlobalTracer.Instance?.ActiveSpan;
			if (Span != null)
			{
				LogEvent.AddPropertyIfAbsent(PropertyFactory.CreateProperty("dd.trace_id", Span.Context.TraceId));
				LogEvent.AddPropertyIfAbsent(PropertyFactory.CreateProperty("dd.span_id", Span.Context.SpanId));
			}
		}
	}

	class TestTracer : ITracer
	{
		ITracer Inner;

		public TestTracer(ITracer Inner)
		{
			this.Inner = Inner;
		}

		public IScopeManager ScopeManager => Inner.ScopeManager;

		public ISpan ActiveSpan => Inner.ActiveSpan;

		public ISpanBuilder BuildSpan(string operationName)
		{
			Serilog.Log.Debug("Creating span {Name}", operationName);
			return Inner.BuildSpan(operationName);
		}

		public ISpanContext Extract<TCarrier>(IFormat<TCarrier> format, TCarrier carrier)
		{
			return Inner.Extract<TCarrier>(format, carrier);
		}

		public void Inject<TCarrier>(ISpanContext spanContext, IFormat<TCarrier> format, TCarrier carrier)
		{
			Inner.Inject<TCarrier>(spanContext, format, carrier);
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

		public static async Task<int> Main(string[] Args)
		{
			CommandLineArguments Arguments = new CommandLineArguments(Args);

			IConfiguration Config = new ConfigurationBuilder()
				.SetBasePath(AppDir.FullName)
				.AddJsonFile("appsettings.json", optional: false) 
				.AddJsonFile("appsettings.Build.json", optional: true) // specific settings for builds (installer/dockerfile)
				.AddJsonFile($"appsettings.{Environment.GetEnvironmentVariable("ASPNETCORE_ENVIRONMENT")}.json", optional: true) // environment variable overrides, also used in k8s setups with Helm
				.AddJsonFile("appsettings.User.json", optional: true)
				.AddJsonFile(UserConfigFile.FullName, optional: true, reloadOnChange: true)
				.AddEnvironmentVariables()
				.Build();

			ServerSettings HordeSettings = new ServerSettings();
			Config.GetSection("Horde").Bind(HordeSettings);

			InitializeDefaults(HordeSettings);

			DirectoryReference LogDir = AppDir;
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				LogDir = DirectoryReference.Combine(DataDir);
			}

			Serilog.Log.Logger = new LoggerConfiguration()
				.WithHordeConfig(HordeSettings)
				.Enrich.FromLogContext()
				.WriteTo.Console(HordeSettings)
				.WriteTo.File(Path.Combine(LogDir.FullName, "Log.txt"), outputTemplate: "[{Timestamp:HH:mm:ss} {Level:w3}] {Indent}{Message:l}{NewLine}{Exception} [{SourceContext}]", rollingInterval: RollingInterval.Day, rollOnFileSizeLimit: true, fileSizeLimitBytes: 20 * 1024 * 1024, retainedFileCountLimit: 10)
				.WriteTo.File(new JsonFormatter(renderMessage: true), Path.Combine(LogDir.FullName, "Log.json"), rollingInterval: RollingInterval.Day, rollOnFileSizeLimit: true, fileSizeLimitBytes: 20 * 1024 * 1024, retainedFileCountLimit: 10)
				.ReadFrom.Configuration(Config)
				.CreateLogger();

			if (HordeSettings.WithDatadog)
			{
				using var _ = Datadog.Trace.Tracer.Instance.StartActive("Trace Test");
				Serilog.Log.Logger.Information("Enabling datadog tracing");
				GlobalTracer.Register(Datadog.Trace.OpenTracing.OpenTracingTracerFactory.WrapTracer(Datadog.Trace.Tracer.Instance));
				using IScope Scope = GlobalTracer.Instance.BuildSpan("OpenTrace Test").StartActive();
				Scope.Span.SetTag("TestProp", "hello");
				Serilog.Log.Logger.Information("Enabling datadog tracing (OpenTrace)");
			}

			IServiceCollection Services = new ServiceCollection();
			Services.AddCommandsFromAssembly(Assembly.GetExecutingAssembly());
			Services.AddLogging(Builder => Builder.AddSerilog());
			Services.AddSingleton<IConfiguration>(Config);

#pragma warning disable ASP0000 // Do not call 'IServiceCollection.BuildServiceProvider' in 'ConfigureServices'
			IServiceProvider ServiceProvider = Services.BuildServiceProvider();
			return await CommandHost.RunAsync(Arguments, ServiceProvider, typeof(ServerCommand));
#pragma warning restore ASP0000 // Do not call 'IServiceCollection.BuildServiceProvider' in 'ConfigureServices'
		}

		// Used by WebApplicationFactory in controller tests. Uses reflection to call this exact function signature.
		public static IHostBuilder CreateHostBuilder(string[] Args) => ServerCommand.CreateHostBuilderForTesting(Args);

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

		/// <summary>
		/// Handles bootstrapping of defaults for local servers, which can't be generated during build/installation process (or are better handled here where they can be updated)
		/// This stuff will change as we get settings into database and could be considered discovery for installer/dockerfile builds 
		/// </summary>		
		static void InitializeDefaults(ServerSettings Settings)
		{			
			if (Settings.SingleInstance)
			{
				FileReference GlobalConfig = FileReference.Combine(Program.DataDir, "Config/globals.json");

				if (!FileReference.Exists(GlobalConfig))
				{
					DirectoryReference.CreateDirectory(GlobalConfig.Directory);
					FileReference.WriteAllText(GlobalConfig, "{}");
				}

				FileReference PrivateCertFile = FileReference.Combine(Program.DataDir, "Agent/ServerToAgent.pfx");
				string PrivateCertFileJsonPath = PrivateCertFile.ToString().Replace("\\", "/", StringComparison.Ordinal);

				if (!FileReference.Exists(UserConfigFile))
				{
					// create new user configuration
					DirectoryReference.CreateDirectory(UserConfigFile.Directory);
					FileReference.WriteAllText(UserConfigFile, $"{{\"Horde\": {{ \"ConfigPath\" : \"{GlobalConfig.ToString().Replace("\\", "/", StringComparison.Ordinal)}\", \"ServerPrivateCert\" : \"{PrivateCertFileJsonPath}\", \"HttpPort\": 8080}}}}");
				}

				// make sure the cert exists
				if (!FileReference.Exists(PrivateCertFile))
				{
					string DnsName = System.Net.Dns.GetHostName();
					Serilog.Log.Logger.Information("Creating certificate for {DnsName}", DnsName);

					byte[] PrivateCertData = CertificateUtils.CreateSelfSignedCert(DnsName, "Horde Server");
					
					Serilog.Log.Logger.Information("Writing private cert: {PrivateCert}", PrivateCertFile.FullName);

					if (!DirectoryReference.Exists(PrivateCertFile.Directory))
					{
						DirectoryReference.CreateDirectory(PrivateCertFile.Directory);
					}

					FileReference.WriteAllBytes(PrivateCertFile, PrivateCertData);
				}

				// note: this isn't great, though we need it early in server startup, and this is only hit on first server boot where the grpc cert isn't generated/set 
				if (Settings.ServerPrivateCert == null)
				{
					Settings.ServerPrivateCert = PrivateCertFile.ToString();
				}				

			}
		}
	}
}

