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

namespace HordeAgent
{
	static class Logging
	{
		static Lazy<Serilog.ILogger> Logger = new Lazy<Serilog.ILogger>(CreateSerilogLogger, true);
		
		/// <summary>
		/// Current version of the HordeAgent
		/// </summary>
		public static string Version = "";

		private class VersionLogEnricher : ILogEventEnricher
		{
			public void Enrich(LogEvent LogEvent, ILogEventPropertyFactory PropertyFactory)
			{
				LogEvent.AddOrUpdateProperty(PropertyFactory.CreateProperty("Version", Version));
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
			Directory.CreateDirectory(Program.SavedDir);

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
				.Enrich.FromLogContext()
				.Enrich.With<VersionLogEnricher>()
				.WriteTo.Console(outputTemplate: "[{Timestamp:HH:mm:ss} {Level:w3}] {Indent}{Message:l}{NewLine}{Exception}", theme: Theme)
				.WriteTo.File(Path.Combine(Program.SavedDir, "Log-.txt"), fileSizeLimitBytes: 50 * 1024 * 1024, rollingInterval: RollingInterval.Day, rollOnFileSizeLimit: true, retainedFileCountLimit: 10)
				.WriteTo.File(new JsonFormatter(renderMessage: true), Path.Combine(Program.SavedDir, "Log-.json"), fileSizeLimitBytes: 50 * 1024 * 1024, rollingInterval: RollingInterval.Day, rollOnFileSizeLimit: true, retainedFileCountLimit: 10)
				.CreateLogger();
		}
	}
}
