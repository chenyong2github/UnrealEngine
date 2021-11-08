// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using Serilog;
using Serilog.Core;
using Serilog.Events;
using Serilog.Extensions.Logging;
using Serilog.Formatting.Json;
using Serilog.Sinks.SystemConsole.Themes;
using System;
using System.IO;
using System.Runtime.InteropServices;
using EpicGames.Core;
using System.Collections.Generic;
using System.Threading;
using OpenTracing.Util;
using OpenTracing;
using Serilog.Formatting;
using System.Text.Json;
using System.Buffers;
using MessageTemplate = EpicGames.Core.MessageTemplate;

namespace HordeAgent
{
	static class Logging
	{
		static string Env = "default";

		public static void SetEnv(string NewEnv)
		{
			Env = NewEnv;
		}

		static Lazy<Serilog.ILogger> Logger = new Lazy<Serilog.ILogger>(CreateSerilogLogger, true);
		
		public static LoggingLevelSwitch LogLevelSwitch =  new LoggingLevelSwitch();

		private class DatadogLogEnricher : ILogEventEnricher
		{
			public void Enrich(Serilog.Events.LogEvent LogEvent, ILogEventPropertyFactory PropertyFactory)
			{
				LogEvent.AddOrUpdateProperty(PropertyFactory.CreateProperty("dd.env", Env));
				LogEvent.AddOrUpdateProperty(PropertyFactory.CreateProperty("dd.service", "hordeagent"));
				LogEvent.AddOrUpdateProperty(PropertyFactory.CreateProperty("dd.version", Program.Version));

				ISpan? Span = GlobalTracer.Instance?.ActiveSpan;
				if (Span != null)
				{
					LogEvent.AddPropertyIfAbsent(PropertyFactory.CreateProperty("dd.trace_id", Span.Context.TraceId));
					LogEvent.AddPropertyIfAbsent(PropertyFactory.CreateProperty("dd.span_id", Span.Context.SpanId));
				}
			}
		}
		
		public class HordeLoggerProvider : ILoggerProvider
		{
			SerilogLoggerProvider Inner;

			public HordeLoggerProvider()
			{
				Inner = new SerilogLoggerProvider(Logger.Value);
			}

			public Microsoft.Extensions.Logging.ILogger CreateLogger(string CategoryName)
			{
				Microsoft.Extensions.Logging.ILogger Logger = Inner.CreateLogger(CategoryName);
				Logger = new DefaultLoggerIndentHandler(Logger);
				return Logger;
			}

			public void Dispose()
			{
				Inner.Dispose();
			}
		}

		static Serilog.ILogger CreateSerilogLogger()
		{
			DirectoryReference.CreateDirectory(Program.DataDir);

			ConsoleTheme Theme;
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows) && Environment.OSVersion.Version < new Version(10, 0))
			{
				Theme = SystemConsoleTheme.Literate;
			}
			else
			{
				Theme = AnsiConsoleTheme.Code;
			}

			return new LoggerConfiguration()
				.MinimumLevel.Debug()
				.MinimumLevel.Override("Microsoft", LogEventLevel.Information)
				.MinimumLevel.Override("System.Net.Http.HttpClient", LogEventLevel.Warning)
				.MinimumLevel.Override("Microsoft.AspNetCore.Routing.EndpointMiddleware", LogEventLevel.Warning)
				.MinimumLevel.Override("Microsoft.AspNetCore.Authorization.DefaultAuthorizationService", LogEventLevel.Warning)
				.MinimumLevel.Override("HordeServer.Authentication.HordeJwtBearerHandler", LogEventLevel.Warning)
				.MinimumLevel.Override("HordeServer.Authentication.OktaHandler", LogEventLevel.Warning)
				.MinimumLevel.Override("Microsoft.AspNetCore.Hosting.Diagnostics", LogEventLevel.Warning)
				.MinimumLevel.Override("Microsoft.AspNetCore.Mvc.Infrastructure.ControllerActionInvoker", LogEventLevel.Warning)
				.MinimumLevel.Override("Serilog.AspNetCore.RequestLoggingMiddleware", LogEventLevel.Warning)
				.MinimumLevel.ControlledBy(LogLevelSwitch)
				.Enrich.FromLogContext()
				.Enrich.With<DatadogLogEnricher>()
				.WriteTo.Console(outputTemplate: "[{Timestamp:HH:mm:ss} {Level:w3}] {Indent}{Message:l}{NewLine}{Exception}", theme: Theme)
				.WriteTo.File(FileReference.Combine(Program.DataDir, "Log-.txt").FullName, fileSizeLimitBytes: 50 * 1024 * 1024, rollingInterval: RollingInterval.Day, rollOnFileSizeLimit: true, retainedFileCountLimit: 10)
				.WriteTo.File(new JsonFormatter(renderMessage: true), FileReference.Combine(Program.DataDir, "Log-.json").FullName, fileSizeLimitBytes: 50 * 1024 * 1024, rollingInterval: RollingInterval.Day, rollOnFileSizeLimit: true, retainedFileCountLimit: 10)
				.CreateLogger();
		}
	}
}
