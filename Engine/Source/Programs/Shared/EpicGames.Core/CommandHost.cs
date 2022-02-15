// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Attribute used to specify names of program modes, and help text
	/// </summary>
	public class CommandAttribute : Attribute
	{
		/// <summary>
		/// Names for this command
		/// </summary>
		public string[] Names;

		/// <summary>
		/// Short description for the mode. Will be displayed in the help text.
		/// </summary>
		public string Description;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of the mode</param>
		/// <param name="Description">Short description for display in the help text</param>
		public CommandAttribute(string Name, string Description)
		{
			this.Names = new string[] { Name };
			this.Description = Description;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Category">Category for this command</param>
		/// <param name="Name">Name of the mode</param>
		/// <param name="Description">Short description for display in the help text</param>
		public CommandAttribute(string Category, string Name, string Description)
		{
			this.Names = new string[] { Category, Name };
			this.Description = Description;
		}
	}

	/// <summary>
	/// Base class for all commands that can be executed by HordeAgent
	/// </summary>
	public abstract class Command
	{
		/// <summary>
		/// Configure this object with the given command line arguments
		/// </summary>
		/// <param name="Arguments">Command line arguments</param>
		/// <param name="Logger">Logging output device</param>
		public virtual void Configure(CommandLineArguments Arguments, ILogger Logger)
		{
			Arguments.ApplyTo(this, Logger);
		}

		/// <summary>
		/// Gets all command line parameters to show in help for this command
		/// </summary>
		/// <param name="Arguments">The command line arguments</param>
		/// <returns>List of name/description pairs</returns>
		public virtual List<KeyValuePair<string, string>> GetParameters(CommandLineArguments Arguments)
		{
			return CommandLineArguments.GetParameters(GetType());
		}

		/// <summary>
		/// Execute this command
		/// </summary>
		/// <param name="Logger">The logger to use for this command</param>
		/// <returns>Exit code</returns>
		public abstract Task<int> ExecuteAsync(ILogger Logger);
	}

	/// <summary>
	/// Entry point for dispatching commands
	/// </summary>
	public static class CommandHost
	{
		/// <summary>
		/// Entry point for executing registered command types in a particular assembly
		/// </summary>
		/// <param name="Args">Command line arguments</param>
		/// <param name="DefaultCommandType">The default command type</param>
		/// <param name="Logger">Logging device to pass to the command object</param>
		/// <returns>Return code from the command</returns>
		public static Task<int> RunAsync(CommandLineArguments Args, Type? DefaultCommandType, ILogger Logger)
		{
			return RunAsync(Args, Assembly.GetEntryAssembly()!, DefaultCommandType, Logger);
		}

		/// <summary>
		/// Entry point for executing registered command types in a particular assembly
		/// </summary>
		/// <param name="Args">Command line arguments</param>
		/// <param name="CommandAssembly">Assembly to scan for command types</param>
		/// <param name="DefaultCommandType">The default command type</param>
		/// <param name="Logger">Logging device to pass to the command object</param>
		/// <returns>Return code from the command</returns>
		public static async Task<int> RunAsync(CommandLineArguments Args, Assembly CommandAssembly, Type? DefaultCommandType, ILogger Logger)
		{
			// Find all the command types
			List<(CommandAttribute, Type)> Commands = new List<(CommandAttribute, Type)>();
			foreach (Type Type in CommandAssembly.GetTypes())
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

			// Parse the positional arguments for the command name
			string[] PositionalArgs = Args.GetPositionalArguments();
			if (PositionalArgs.Length == 0)
			{
				if (DefaultCommandType == null || Args.HasOption("-Help"))
				{
					Console.WriteLine(CommandAssembly.GetName().Name);
					Console.WriteLine("");

					AssemblyDescriptionAttribute? Description = CommandAssembly.GetCustomAttribute<AssemblyDescriptionAttribute>();
					if (Description != null)
					{
						Console.WriteLine(Description.Description);
					}

					Console.WriteLine("Usage:");
					Console.WriteLine("    [Command] [-Option1] [-Option2]...");
					Console.WriteLine("");
					Console.WriteLine("Commands:");

					PrintCommands(Commands.Select(x => x.Item1));

					Console.WriteLine("");
					Console.WriteLine("Specify \"<CommandName> -Help\" for command-specific help");
					return 0;
				}
				else 
				{
					CommandType = DefaultCommandType;
				}
			}
			else
			{
				foreach ((CommandAttribute Attribute, Type Type) in Commands.OrderBy(x => x.Item1.Names.Length))
				{
					if (Attribute.Names.SequenceEqual(PositionalArgs, StringComparer.OrdinalIgnoreCase))
					{
						CommandType = Type;
						CommandAttribute = Attribute;
						break;
					}
				}
				if (CommandType == null)
				{
					ConsoleUtils.WriteError($"Invalid command '{String.Join(" ", PositionalArgs)}'");
					Console.WriteLine("");
					Console.WriteLine("Available commands:");

					PrintCommands(Commands.Select(x => x.Item1));
					return 1;
				}
			}

			// Create the command instance
			Command Command = (Command)Activator.CreateInstance(CommandType)!;

			// If the help flag is specified, print the help info and exit immediately
			if (Args.HasOption("-Help"))
			{
				if (CommandAttribute == null)
				{
					HelpUtils.PrintHelp(null, null, Command.GetParameters(Args));
				}
				else
				{
					HelpUtils.PrintHelp(String.Join(" ", CommandAttribute.Names), CommandAttribute.Description, Command.GetParameters(Args));
				}
				return 1;
			}

			// Configure the command
			try
			{
				Command.Configure(Args, Logger);
				Args.CheckAllArgumentsUsed(Logger);
			}
			catch (CommandLineArgumentException Ex)
			{
				ConsoleUtils.WriteError(Ex.Message);
				Console.WriteLine("");
				Console.WriteLine("Valid parameters:");

				HelpUtils.PrintTable(Command.GetParameters(Args), 4, 24);
				return 1;
			}

			// Execute all the commands
			try
			{
				return await Command.ExecuteAsync(Logger);
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

		/// <summary>
		/// Print a formatted list of all the available commands
		/// </summary>
		/// <param name="Attributes">List of command attributes</param>
		static void PrintCommands(IEnumerable<CommandAttribute> Attributes)
		{
			List<KeyValuePair<string, string>> Commands = new List<KeyValuePair<string, string>>();
			foreach (CommandAttribute Attribute in Attributes)
			{
				Commands.Add(new KeyValuePair<string, string>(String.Join(" ", Attribute.Names), Attribute.Description));
			}
			HelpUtils.PrintTable(Commands.OrderBy(x => x.Key).ToList(), 4, 20);
		}
	}
}
