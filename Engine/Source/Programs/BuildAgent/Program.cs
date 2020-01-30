// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent
{
	class Program
	{
		/// <summary>
		/// Entry point
		/// </summary>
		/// <param name="Args">Command-line arguments</param>
		/// <returns>Exit code</returns>
		static int Main(string[] Args)
		{
			try
			{
				Log.TraceInformation("Welcome to the Danger Zone!");
				int Result = GuardedMain(Args);
				return Result;
			}
			catch (FatalErrorException Ex)
			{
				Log.WriteException(Ex, null);
				return Ex.ExitCode;
			}
			catch (Exception Ex)
			{
				Log.WriteException(Ex, null);
				return 1;
			}
		}

		/// <summary>
		/// Actual Main function, without exception guards
		/// </summary>
		/// <param name="Args">Command-line arguments</param>
		/// <returns>Exit code</returns>
		static int GuardedMain(string[] Args)
		{
			// Find the index of the first command
			int ModeIndex = 0;
			while (ModeIndex < Args.Length && Args[ModeIndex].StartsWith("-"))
			{
				ModeIndex++;
			}

			// Find all the ToolMode types
			Dictionary<string, Type> ModeNameToType = new Dictionary<string, Type>(StringComparer.OrdinalIgnoreCase);
			foreach (Type Type in Assembly.GetExecutingAssembly().GetTypes())
			{
				ProgramModeAttribute Attribute = Type.GetCustomAttribute<ProgramModeAttribute>();
				if (Attribute != null)
				{
					ModeNameToType.Add(Attribute.Name, Type); 
				}
			}

			// Check if there are any commands specified on the command line.
			if (ModeIndex == Args.Length)
			{
				Log.TraceInformation("BuildAgent");
				Log.TraceInformation("");
				Log.TraceInformation("Utility for managing automated processes on build machines.");
				Log.TraceInformation("");
				Log.TraceInformation("Usage:");
				Log.TraceInformation("    BuildAgent.exe [Command] [-Option1] [-Option2]...");
				Log.TraceInformation("");
				Log.TraceInformation("Commands:");

				PrintCommands(ModeNameToType);

				Log.TraceInformation("");
				Log.TraceInformation("Specify \"Command -Help\" for command-specific help");
				return 0;
			}

			// Get the command name
			string ModeName = Args[ModeIndex];

			// Get the command type
			Type ModeType;
			if (!ModeNameToType.TryGetValue(ModeName, out ModeType))
			{
				Log.TraceError("Unknown command '{0}'.", ModeName);
				Log.TraceInformation("");
				Log.TraceInformation("Available commands");
				PrintCommands(ModeNameToType);
				return 1;
			}

			// Update the mode name to use the correct casing, in case we need it for error messages
			ModeName = ModeType.GetCustomAttribute<ProgramModeAttribute>().Name;

			// Build an argument list for the command, including all the global arguments as well as arguments until the next command
			List<string> ArgumentList = new List<string>();
			for (int Idx = 0; Idx < Args.Length; Idx++)
			{
				if (Idx != ModeIndex)
				{
					ArgumentList.Add(Args[Idx]);
				}
			}
			CommandLineArguments ModeArguments = new CommandLineArguments(ArgumentList.ToArray());

			// Create the command instance
			ProgramMode Mode = (ProgramMode)Activator.CreateInstance(ModeType);

			// If the help flag is specified, print the help info and exit immediately
			if (ModeArguments.HasOption("-Help"))
			{
				HelpUtils.PrintHelp(ModeName, HelpUtils.GetDescription(ModeType), Mode.GetParameters(ModeArguments));
				return 1;
			}

			// Configure the command
			try
			{
				Mode.Configure(ModeArguments);
				ModeArguments.CheckAllArgumentsUsed();
			}
			catch (CommandLineArgumentException Ex)
			{
				Log.TraceError("{0}: {1}", ModeName, Ex.Message);
				Log.TraceInformation("");
				Log.TraceInformation("Arguments for {0}:", ModeName);

				HelpUtils.PrintTable(Mode.GetParameters(ModeArguments), 4, 24);
				return 1;
			}

			// Execute all the commands
			return Mode.Execute();
		}

		/// <summary>
		/// Print a formatted list of all the available commands
		/// </summary>
		/// <param name="ModeNameToType">Map from command name to type</param>
		static void PrintCommands(Dictionary<string, Type> ModeNameToType)
		{
			List<KeyValuePair<string, string>> Commands = new List<KeyValuePair<string, string>>();
			foreach (KeyValuePair<string, Type> Pair in ModeNameToType.OrderBy(x => x.Key))
			{
				ProgramModeAttribute Attribute = Pair.Value.GetCustomAttribute<ProgramModeAttribute>();
				Commands.Add(new KeyValuePair<string, string>(Attribute.Name, Attribute.Description));
			}
			HelpUtils.PrintTable(Commands, 4, 20);
		}
	}
}
