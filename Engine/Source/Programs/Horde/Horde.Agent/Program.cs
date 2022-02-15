// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Commands;
using HordeAgent.Services;
using HordeAgent.Utility;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Hosting.WindowsServices;
using Microsoft.Extensions.Logging;
using Polly;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Management;
using System.Net;
using System.Net.Http.Headers;
using System.Reflection;
using System.Runtime.InteropServices;
using System.ServiceProcess;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using System.Text;
using EpicGames.Perforce;
using System.Security.Cryptography;

namespace HordeAgent
{
	/// <summary>
	/// Entry point
	/// </summary>
	public static class Program
	{
		/// <summary>
		/// Name of the http client
		/// </summary>
		public const string HordeServerClientName = "HordeServer";

		/// <summary>
		/// Path to the root application directory
		/// </summary>
		public static DirectoryReference AppDir { get; } = GetAppDir();

		/// <summary>
		/// Path to the default data directory
		/// </summary>
		public static DirectoryReference DataDir { get; } = GetDataDir();

		/// <summary>
		/// The launch arguments
		/// </summary>
		public static string[] Args { get; private set; } = null!;

		/// <summary>
		/// The current application version
		/// </summary>
		public static string Version { get; } = GetVersion();

		/// <summary>
		/// Entry point
		/// </summary>
		/// <param name="Args">Command-line arguments</param>
		/// <returns>Exit code</returns>
		public static async Task<int> Main(string[] Args)
		{
			Program.Args = Args;

			IServiceCollection Services = new ServiceCollection();
			Services.AddCommandsFromAssembly(Assembly.GetExecutingAssembly());
			Services.AddLogging(Builder => Builder.AddProvider(new Logging.HordeLoggerProvider()));

			// Enable unencrypted HTTP/2 for gRPC channel without TLS
			AppContext.SetSwitch("System.Net.Http.SocketsHttpHandler.Http2UnencryptedSupport", true);

			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				// Prioritize agent execution time over any job its running.
				// We've seen file copying starving the agent communication to the Horde server, causing a disconnect.
				// Increasing the process priority is speculative fix to combat this.
				using (Process Process = Process.GetCurrentProcess())
				{
					Process.PriorityClass = ProcessPriorityClass.High;
				}
			}

			// Execute all the commands
			IServiceProvider ServiceProvider = Services.BuildServiceProvider();
			return await CommandHost.RunAsync(new CommandLineArguments(Args), ServiceProvider, typeof(HordeAgent.Modes.Service.RunCommand));
		}

		/// <summary>
		/// Gets the version of the current assembly
		/// </summary>
		/// <returns></returns>
		static string GetVersion()
		{
			try
			{
				return FileVersionInfo.GetVersionInfo(Assembly.GetExecutingAssembly().Location).ProductVersion;
			}
			catch
			{
				return "unknown";
			}
		}

		/// <summary>
		/// Gets the application directory
		/// </summary>
		/// <returns></returns>
		static DirectoryReference GetAppDir()
		{
			return new DirectoryReference(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location)!);
		}

		/// <summary>
		/// Gets the default data directory
		/// </summary>
		/// <returns></returns>
		static DirectoryReference GetDataDir()
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				DirectoryReference? ProgramDataDir = DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.CommonApplicationData);
				if (ProgramDataDir != null)
				{
					return DirectoryReference.Combine(ProgramDataDir, "HordeAgent");
				}
			}
			return GetAppDir();
		}
	}
}
