// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Threading.Tasks;
using Datadog.Trace;
using Datadog.Trace.Configuration;
using Datadog.Trace.OpenTracing;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Horde.Agent.Execution;
using Horde.Agent.Execution.Interfaces;
using Horde.Agent.Leases;
using Horde.Agent.Leases.Handlers;
using Horde.Agent.Services;
using Horde.Agent.Utility;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using OpenTracing;
using OpenTracing.Util;
using Polly;
using Serilog.Events;

namespace Horde.Agent.Modes.Service
{
	using ITracer = OpenTracing.ITracer;

	/// <summary>
	/// 
	/// </summary>
	[Command("Service", "Run", "Runs the service in listen mode")]
	class RunCommand : Command
	{
		/// <summary>
		/// Override for the server to use
		/// </summary>
		[CommandLine("-Server=")]
		string? Server { get; set; } = null;

		/// <summary>
		/// Override the working directory
		/// </summary>
		[CommandLine("-WorkingDir=")]
		string? WorkingDir { get; set; } = null;

		/// <summary>
		/// Log verbosity level (use normal Serilog levels such as debug, warning or info)
		/// </summary>
		[CommandLine("-LogLevel")]
		public string LogLevelStr { get; set; } = "information";

		readonly DefaultServices _defaultServices;

		/// <summary>
		/// Constructor
		/// </summary>
		public RunCommand(DefaultServices defaultServices)
		{
			_defaultServices = defaultServices;
		}

		/// <summary>
		/// Runs the service indefinitely
		/// </summary>
		/// <returns>Exit code</returns>
		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			if (Enum.TryParse(LogLevelStr, true, out LogEventLevel logEventLevel))
			{
				Logging.LogLevelSwitch.MinimumLevel = logEventLevel;
			}
			else
			{
				Console.WriteLine($"Unable to parse log level: {LogLevelStr}");
				return 0;
			}
			
			IHostBuilder hostBuilder = Host.CreateDefaultBuilder();

			// Attempt to setup this process as a Windows service. A race condition inside Microsoft.Extensions.Hosting.WindowsServices.WindowsServiceHelpers.IsWindowsService
			// can result in accessing the parent process after it's terminated, so catch any exceptions that it throws.
			try
			{
				hostBuilder = hostBuilder.UseWindowsService();
			}
			catch (InvalidOperationException)
			{
			}

			hostBuilder = hostBuilder
				.ConfigureAppConfiguration(builder =>
				{
					builder.AddConfiguration(_defaultServices.Configuration);

					Dictionary<string, string> overrides = new Dictionary<string, string>();
					if (Server != null)
					{
						overrides.Add($"{AgentSettings.SectionName}:{nameof(AgentSettings.Server)}", Server);
					}
					if (WorkingDir != null)
					{
						overrides.Add($"{AgentSettings.SectionName}:{nameof(AgentSettings.WorkingDir)}", WorkingDir);
					}

					builder.AddInMemoryCollection(overrides);
				})
				.ConfigureLogging(builder =>
				{
					// We add our logger through ConfigureServices, inherited from _defaultServices
					builder.ClearProviders();
				})
				.ConfigureServices((hostContext, services) =>
				{
					services.Configure<HostOptions>(options =>
					{
						// Allow the worker service to terminate any before shutting down
						options.ShutdownTimeout = TimeSpan.FromSeconds(30);
					});

					foreach (ServiceDescriptor descriptor in _defaultServices.Descriptors)
					{
						services.Add(descriptor);
					}
				});

			try
			{
				await hostBuilder.Build().RunAsync();
			}
			catch (OperationCanceledException)
			{
				// Need to catch this to prevent it propagating to the awaiter when pressing ctrl-c
			}

			return 0;
		}
	}
}
