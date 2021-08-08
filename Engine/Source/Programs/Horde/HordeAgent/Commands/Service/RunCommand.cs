// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Commands;
using HordeAgent.Services;
using HordeAgent.Utility;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Polly;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Net.Security;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using System.IO;
using System.Reflection;
using OpenTracing;
using OpenTracing.Util;
using Datadog.Trace.Configuration;
using Datadog.Trace;

namespace HordeAgent.Modes.Service
{
	/// <summary>
	/// 
	/// </summary>
	[Command("Service", "Run", "Runs the service in listen mode")]
	class RunCommand : Command
	{
		/// <summary>
		/// The logger instance
		/// </summary>
		ILogger Logger = null!;

		/// <summary>
		/// Override for the server to use
		/// </summary>
		[CommandLine("-Server=")]
		string? Server = null;

		/// <summary>
		/// Override the working directory
		/// </summary>
		[CommandLine("-WorkingDir=")]
		string? WorkingDir = null;

		/// <summary>
		/// Runs the service indefinitely
		/// </summary>
		/// <param name="Logger">Logger to use</param>
		/// <returns>Exit code</returns>
		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			this.Logger = Logger;

			IHostBuilder HostBuilder = Host.CreateDefaultBuilder();

			// Attempt to setup this process as a Windows service. A race condition inside Microsoft.Extensions.Hosting.WindowsServices.WindowsServiceHelpers.IsWindowsService
			// can result in accessing the parent process after it's terminated, so catch any exceptions that it throws.
			try
			{
				HostBuilder = HostBuilder.UseWindowsService();
			}
			catch (InvalidOperationException)
			{
			}

			HostBuilder = HostBuilder
				.ConfigureAppConfiguration(Builder =>
				{
					Dictionary<string, string> Overrides = new Dictionary<string, string>();
					if (Server != null)
					{
						Overrides.Add($"{AgentSettings.SectionName}:{nameof(AgentSettings.Server)}", Server);
					}
					if (WorkingDir != null)
					{
						Overrides.Add($"{AgentSettings.SectionName}:{nameof(AgentSettings.WorkingDir)}", WorkingDir);
					}
					Builder.AddInMemoryCollection(Overrides);
				})
				.ConfigureLogging(Builder =>
				{
					Builder.ClearProviders();
					Builder.AddProvider(new Logging.HordeLoggerProvider());
					Builder.AddFilter<Logging.HordeLoggerProvider>(null, LogLevel.Trace);
				})
				.ConfigureServices((HostContext, Services) =>
				{
					Services.Configure<HostOptions>(Options =>
					{
						// Allow the worker service to terminate any before shutting down
						Options.ShutdownTimeout = TimeSpan.FromSeconds(30);
					});

					IConfigurationSection ConfigSection = HostContext.Configuration.GetSection(AgentSettings.SectionName);
					Services.AddOptions<AgentSettings>().Configure(Options => ConfigSection.Bind(Options)).ValidateDataAnnotations();

					AgentSettings Settings = new AgentSettings();
					ConfigSection.Bind(Settings);

					ServerProfile ServerProfile = Settings.GetCurrentServerProfile();
					ConfigureTracing(ServerProfile.Environment, Program.Version);

					Logging.SetEnv(ServerProfile.Environment);

					Services.AddHttpClient(Program.HordeServerClientName, Config =>
					{
						Config.BaseAddress = new Uri(ServerProfile.Url);
						Config.DefaultRequestHeaders.Add("Accept", "application/json");
						Config.Timeout = TimeSpan.FromSeconds(300); // Need to make sure this doesn't cancel any long running gRPC streaming calls (eg. session update)
					})
					.ConfigurePrimaryHttpMessageHandler(() =>
					{
						HttpClientHandler Handler = new HttpClientHandler();
						Handler.ServerCertificateCustomValidationCallback += (Sender, Cert, Chain, Errors) => CertificateHelper.CertificateValidationCallBack(Logger, Sender, Cert, Chain, Errors, ServerProfile);
						return Handler;
					})
					.AddTransientHttpErrorPolicy(Builder =>
					{
						return Builder.WaitAndRetryAsync(new[] { TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(10) });
					});

					Services.AddSingleton<GrpcService>();
					Services.AddHostedService<WorkerService>();
				});

			try
			{
				await HostBuilder.Build().RunAsync();
			}
			catch (OperationCanceledException)
			{
				// Need to catch this to prevent it propagating to the awaiter when pressing ctrl-c
			}

			return 0;
		}

		static void ConfigureTracing(string Environment, string Version)
		{
			TracerSettings Settings = TracerSettings.FromDefaultSources();
			Settings.Environment = Environment;
			Settings.ServiceName = "hordeagent";
			Settings.ServiceVersion = Version;
			Settings.LogsInjectionEnabled = true;

			Tracer.Instance = new Tracer(Settings);

			ITracer OpenTracer = Datadog.Trace.OpenTracing.OpenTracingTracerFactory.WrapTracer(Tracer.Instance);
			GlobalTracer.Register(OpenTracer);
		}
	}
}
