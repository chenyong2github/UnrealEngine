// Copyright Epic Games, Inc. All Rights Reserved.

using McMaster.Extensions.CommandLineUtils;
using Microsoft.Extensions.Configuration;
using Serilog;
using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Linq;
using static SkeinCLI.SkeinCmdBase;

namespace SkeinCLI
{
	class Program
	{
		private static async Task<int> Main(string[] args)
		{
			// Using a named Mutex we can ensure that only one instance of the application is running on the machine
			using (var mutex = new Mutex(false, "SkeinCLI"))
			{
				bool IsAlreadyExecuting = !mutex.WaitOne(TimeSpan.Zero);
				if (IsAlreadyExecuting)
				{
					// Returning immediately if another instance of the Skein CLI is executing
					return (int)ReturnCodes.AlreadyExecuting;
				}

				string configFile = (args.Contains("--debug") || args.Contains("-d")) ? "appsettings-debug.json" : "appsettings.json";

				Environment.SetEnvironmentVariable("SKEIN_CLI_LOGPATH", GetLogsOutputFolder());

				var Configuration = new ConfigurationBuilder()
					.SetBasePath(Directory.GetCurrentDirectory())
					.AddJsonFile(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, configFile), optional: true, reloadOnChange: true)
					.AddEnvironmentVariables()
					.Build();

				Log.Logger = new LoggerConfiguration()
					.ReadFrom.Configuration(Configuration)
					.CreateLogger();

				try
				{
					return await CommandLineApplication.ExecuteAsync<SkeinCmd>(args);
				}
				catch (Exception ex)
				{
					Log.ForContext<Program>().Error(ex, "Oh no!");
					return (int)ReturnCodes.GenericFailure;
				}
				finally
				{
					Log.CloseAndFlush();
					mutex.ReleaseMutex();
				}
			}
		}

		private static string GetLogsOutputFolder()
		{
			// Windows: C:\Users\<username>\AppData\Roaming\skein\logs
			// Linux  : /home/.config/skein/logs
			return Path.Combine(System.Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "skein", "logs");
		}
	}
}
