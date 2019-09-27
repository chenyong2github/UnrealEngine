// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
			int FirstModeIndex = 0;
			while (FirstModeIndex < Args.Length && Args[FirstModeIndex].StartsWith("-"))
			{
				FirstModeIndex++;
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
			if (FirstModeIndex == Args.Length)
			{
				Log.TraceInformation("BuildAgent");
				Log.TraceInformation("");
				Log.TraceInformation("Utility for managing automated processes on build machines.");
				Log.TraceInformation("");
				Log.TraceInformation("Usage:");
				Log.TraceInformation("    BuildAgent.exe [CommandA] [-OptionA1] [-OptionA2] [CommandB] [-OptionB1]...");
				Log.TraceInformation("");
				Log.TraceInformation("Commands:");

				PrintCommands(ModeNameToType);
				return 0;
			}

			// Parse the arguments for all commands
			List<ProgramMode> Modes = new List<ProgramMode>();
			for (int NextCommandIdx = FirstModeIndex; NextCommandIdx < Args.Length;)
			{
				string CommandName = Args[NextCommandIdx++];

				// Get the command type
				Type CommandType;
				if (!ModeNameToType.TryGetValue(CommandName, out CommandType))
				{
					Log.TraceError("Unknown command '{0}'.", CommandName);
					Log.TraceInformation("");
					Log.TraceInformation("Available commands");
					PrintCommands(ModeNameToType);
					return 1;
				}

				// Build an argument list for the command, including all the global arguments as well as arguments until the next command
				List<string> ArgumentList = new List<string>();
				for (int Idx = 0; Idx < FirstModeIndex; Idx++)
				{
					ArgumentList.Add(Args[Idx]);
				}
				while (NextCommandIdx < Args.Length && Args[NextCommandIdx].StartsWith("-"))
				{
					ArgumentList.Add(Args[NextCommandIdx++]);
				}
				CommandLineArguments CommandArguments = new CommandLineArguments(ArgumentList.ToArray());

				// Create the command instance
				ProgramMode Command = (ProgramMode)Activator.CreateInstance(CommandType);
				Modes.Add(Command);

				// If the help flag is specified, print the help info and exit immediately
				if (CommandArguments.HasOption("-Help"))
				{
					HelpUtils.PrintHelp(CommandName, HelpUtils.GetDescription(CommandType), Command.GetParameters());
					return 1;
				}

				// Configure the command
				try
				{
					Command.Configure(CommandArguments);
					CommandArguments.CheckAllArgumentsUsed();
				}
				catch (CommandLineArgumentException Ex)
				{
					Log.TraceError("{0}: {1}", CommandName, Ex.Message);
					Log.TraceInformation("");
					Log.TraceInformation("Arguments for {0}:", CommandName);

					HelpUtils.PrintTable(Command.GetParameters(), 4, 24);
					return 1;
				}
			}

			// Execute all the commands
			foreach (ProgramMode Mode in Modes)
			{
				Mode.Execute();
			}

			return 0;
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
