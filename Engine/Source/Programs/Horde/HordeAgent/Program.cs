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
		/// Width to use for printing out help
		/// </summary>
		static int HelpWidth
		{
			get { return HelpUtils.WindowWidth - 20; }
		}

		/// <summary>
		/// Entry point
		/// </summary>
		/// <param name="Args">Command-line arguments</param>
		/// <returns>Exit code</returns>
		public static async Task<int> Main(string[] Args)
		{
			Program.Args = Args;

			ILogger Logger = new Logging.HordeLoggerProvider().CreateLogger("HordeAgent");
			try
			{
				int Result = await GuardedMain(Args, Logger);
				return Result;
			}
			catch (FatalErrorException Ex)
			{
				Logger.LogCritical(Ex, "Fatal error.");
				return Ex.ExitCode;
			}
			catch (Exception Ex)
			{
				Logger.LogCritical(Ex, "Fatal error.");
				return 1;
			}
		}

		static bool MatchCommand(string[] Args, CommandAttribute Attribute)
		{
			if(Args.Length < Attribute.Names.Length)
			{
				return false;
			}

			for (int Idx = 0; Idx < Attribute.Names.Length; Idx++)
			{
				if (!Attribute.Names[Idx].Equals(Args[Idx], StringComparison.OrdinalIgnoreCase))
				{
					return false;
				}
			}

			return true;
		}

		/// <summary>
		/// Actual Main function, without exception guards
		/// </summary>
		/// <param name="Args">Command-line arguments</param>
		/// <param name="Logger">The logger interface</param>
		/// <returns>Exit code</returns>
		static async Task<int> GuardedMain(string[] Args, ILogger Logger)
		{
			// Enable unencrypted HTTP/2 for gRPC channel without TLS
			AppContext.SetSwitch("System.Net.Http.SocketsHttpHandler.Http2UnencryptedSupport", true);
			
			// Find all the command types
			List<(CommandAttribute, Type)> Commands = new List<(CommandAttribute, Type)>();
			foreach (Type Type in Assembly.GetExecutingAssembly().GetTypes())
			{
				CommandAttribute? Attribute = Type.GetCustomAttribute<CommandAttribute>();
				if (Attribute != null)
				{
					Commands.Add((Attribute, Type));
				}
			}

			// Check if there's a matching command
			Type? CommandType = null;
			CommandAttribute? CommandAttribute = null;

			string[] DefaultArgs = new string[] { "Service", "Run" };
;			Type? DefaultCommandType = null;
			CommandAttribute? DefaultCommandAttribute = null;

			foreach ((CommandAttribute Attribute, Type Type) in Commands.OrderBy(x => x.Item1.Names.Length))
			{
				if (MatchCommand(Args, Attribute))
				{
					CommandType = Type;
					CommandAttribute = Attribute;
				}

				if (MatchCommand(DefaultArgs, Attribute))
				{
					DefaultCommandType = Type;
					DefaultCommandAttribute = Attribute;
				}

			}

			// Check if there are any commands specified on the command line.
			if (CommandType == null)
			{
				if (Args.Length > 0 && !Args[0].StartsWith("-"))
				{
					Logger.LogError("Invalid command");
					Logger.LogInformation("");
					Logger.LogInformation("Available commands");

					PrintCommands(Commands.Select(x => x.Item1), Logger);
					return 1;
				}
				else if (Args.Length > 0 && Args[0].Equals("-Help", StringComparison.OrdinalIgnoreCase))
				{
					Logger.LogInformation("HordeAgent");
					Logger.LogInformation("");
					Logger.LogInformation("Utility for managing automated processes on build machines.");
					Logger.LogInformation("");
					Logger.LogInformation("Usage:");
					Logger.LogInformation("    HordeAgent.exe [Command] [-Option1] [-Option2]...");
					Logger.LogInformation("");
					Logger.LogInformation("Commands:");

					PrintCommands(Commands.Select(x => x.Item1), Logger);

					Logger.LogInformation("");
					Logger.LogInformation("Specify \"Command -Help\" for command-specific help");
					return 0;
				}
				else
				{
					CommandType = DefaultCommandType;
					CommandAttribute = DefaultCommandAttribute;

					if (CommandType == null || CommandAttribute == null)
					{
						Logger.LogError("Invalid default command");
						return 1;
					}
				}
			}

			// Extract the 
			string CommandName = String.Join(" ", CommandAttribute!.Names);

			// Build an argument list for the command, including all the global arguments as well as arguments until the next command
			CommandLineArguments CommandArguments = new CommandLineArguments(Args.Skip(CommandAttribute.Names.Length).ToArray());

			// Create the command instance
			Command Command = (Command)Activator.CreateInstance(CommandType)!;

			// If the help flag is specified, print the help info and exit immediately
			if (CommandArguments.HasOption("-Help"))
			{
				HelpUtils.PrintHelp(CommandName, CommandAttribute.Description, Command.GetParameters(CommandArguments), HelpWidth, Logger);
				return 1;
			}

			// Configure the command
			try
			{
				Command.Configure(CommandArguments, Logger);
				CommandArguments.CheckAllArgumentsUsed(Logger);
			}
			catch (CommandLineArgumentException Ex)
			{
				Logger.LogError("{0}: {1}", CommandName, Ex.Message);

				Logger.LogInformation("");
				Logger.LogInformation("Arguments for {0}:", CommandName);

				HelpUtils.PrintTable(Command.GetParameters(CommandArguments), 4, 24, HelpWidth, Logger);
				return 1;
			}

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
			return await Command.ExecuteAsync(Logger);
		}

		/// <summary>
		/// Print a formatted list of all the available commands
		/// </summary>
		/// <param name="Attributes">List of command attributes</param>
		/// <param name="Logger">The logging output device</param>
		static void PrintCommands(IEnumerable<CommandAttribute> Attributes, ILogger Logger)
		{
			List<KeyValuePair<string, string>> Commands = new List<KeyValuePair<string, string>>();
			foreach(CommandAttribute Attribute in Attributes)
			{
				Commands.Add(new KeyValuePair<string, string>(String.Join(" ", Attribute.Names), Attribute.Description));
			}
			HelpUtils.PrintTable(Commands.OrderBy(x => x.Key).ToList(), 4, 20, HelpWidth, Logger);
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
