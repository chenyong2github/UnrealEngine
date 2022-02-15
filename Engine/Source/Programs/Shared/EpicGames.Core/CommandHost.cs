// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.DependencyInjection;
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
	/// Interface for commands
	/// </summary>
	public interface ICommand
	{
		/// <summary>
		/// Configure this object with the given command line arguments
		/// </summary>
		/// <param name="Arguments">Command line arguments</param>
		void Configure(CommandLineArguments Arguments, ILogger Logger);

		/// <summary>
		/// Gets all command line parameters to show in help for this command
		/// </summary>
		/// <param name="Arguments">The command line arguments</param>
		/// <returns>List of name/description pairs</returns>
		List<KeyValuePair<string, string>> GetParameters(CommandLineArguments Arguments);

		/// <summary>
		/// Execute this command
		/// </summary>
		/// <param name="Logger">The logger to use for this command</param>
		/// <returns>Exit code</returns>
		Task<int> ExecuteAsync(ILogger Logger);
	}

	/// <summary>
	/// Interface describing a command that can be exectued
	/// </summary>
	public interface ICommandFactory
	{
		/// <summary>
		/// Names for this command
		/// </summary>
		public string[] Names { get; }

		/// <summary>
		/// Short description for the mode. Will be displayed in the help text.
		/// </summary>
		public string Description { get; }

		/// <summary>
		/// Create a command instance
		/// </summary>
		public ICommand CreateInstance(IServiceProvider ServiceProvider);
	}

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
	public abstract class Command : ICommand
	{
		/// <inheritdoc/>
		public virtual void Configure(CommandLineArguments Arguments, ILogger Logger)
		{
			Arguments.ApplyTo(this, Logger);
		}

		/// <inheritdoc/>
		public virtual List<KeyValuePair<string, string>> GetParameters(CommandLineArguments Arguments)
		{
			return CommandLineArguments.GetParameters(GetType());
		}

		/// <inheritdoc/>
		public abstract Task<int> ExecuteAsync(ILogger Logger);
	}

	/// <summary>
	/// Default implementation of a command factory
	/// </summary>
	class CommandFactory : ICommandFactory
	{
		public string[] Names { get; }
		public string Description { get; }

		public Type Type;

		public CommandFactory(string[] Names, string Description, Type Type)
		{
			this.Names = Names;
			this.Description = Description;
			this.Type = Type;
		}

		public ICommand CreateInstance(IServiceProvider ServiceProvider) => (ICommand)ServiceProvider.GetRequiredService(Type);
		public override string ToString() => String.Join(" ", Names);
	}

	/// <summary>
	/// Entry point for dispatching commands
	/// </summary>
	public static class CommandHost
	{
		/// <summary>
		/// Adds services for executing the 
		/// </summary>
		/// <param name="Services"></param>
		/// <param name="Assembly"></param>
		public static void AddCommandsFromAssembly(this IServiceCollection Services, Assembly Assembly)
		{
			List<(CommandAttribute, Type)> Commands = new List<(CommandAttribute, Type)>();
			foreach (Type Type in Assembly.GetTypes())
			{
				if (typeof(ICommand).IsAssignableFrom(Type) && !Type.IsAbstract)
				{
					CommandAttribute? Attribute = Type.GetCustomAttribute<CommandAttribute>();
					if (Attribute != null)
					{
						Services.AddTransient(Type);
						Services.AddTransient(typeof(ICommandFactory), SP => new CommandFactory(Attribute.Names, Attribute.Description, Type));
					}
				}
			}
		}

		/// <summary>
		/// Entry point for executing registered command types in a particular assembly
		/// </summary>
		/// <param name="Args">Command line arguments</param>
		/// <param name="ServiceProvider">The service provider for the application</param>
		/// <param name="DefaultCommandType">The default command type</param>
		/// <returns>Return code from the command</returns>
		public static async Task<int> RunAsync(CommandLineArguments Args, IServiceProvider ServiceProvider, Type? DefaultCommandType)
		{
			// Find all the command types
			List<ICommandFactory> CommandFactories = ServiceProvider.GetServices<ICommandFactory>().ToList();

			// Check if there's a matching command
			ICommand? Command = null;
			ICommandFactory? CommandFactory = null;

			// Parse the positional arguments for the command name
			string[] PositionalArgs = Args.GetPositionalArguments();
			if (PositionalArgs.Length == 0)
			{
				if (DefaultCommandType == null || Args.HasOption("-Help"))
				{
					Console.WriteLine("Usage:");
					Console.WriteLine("    [Command] [-Option1] [-Option2]...");
					Console.WriteLine("");
					Console.WriteLine("Commands:");

					PrintCommands(CommandFactories);

					Console.WriteLine("");
					Console.WriteLine("Specify \"<CommandName> -Help\" for command-specific help");
					return 0;
				}
				else 
				{
					Command = (ICommand)ServiceProvider.GetService(DefaultCommandType);
				}
			}
			else
			{
				foreach (ICommandFactory Factory in CommandFactories)
				{
					if (Factory.Names.SequenceEqual(PositionalArgs, StringComparer.OrdinalIgnoreCase))
					{
						Command = Factory.CreateInstance(ServiceProvider);
						CommandFactory = Factory;
						break;
					}
				}
				if (Command == null)
				{
					ConsoleUtils.WriteError($"Invalid command '{String.Join(" ", PositionalArgs)}'");
					Console.WriteLine("");
					Console.WriteLine("Available commands:");

					PrintCommands(CommandFactories);
					return 1;
				}
			}

			// If the help flag is specified, print the help info and exit immediately
			if (Args.HasOption("-Help"))
			{
				if (CommandFactory == null)
				{
					HelpUtils.PrintHelp(null, null, Command.GetParameters(Args));
				}
				else
				{
					HelpUtils.PrintHelp(String.Join(" ", CommandFactory.Names), CommandFactory.Description, Command.GetParameters(Args));
				}
				return 1;
			}

			// Configure the command
			ILogger Logger = ServiceProvider.GetRequiredService<ILoggerProvider>().CreateLogger("CommandHost");
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
		static void PrintCommands(IEnumerable<ICommandFactory> Attributes)
		{
			List<KeyValuePair<string, string>> Commands = new List<KeyValuePair<string, string>>();
			foreach (ICommandFactory Attribute in Attributes)
			{
				Commands.Add(new KeyValuePair<string, string>(String.Join(" ", Attribute.Names), Attribute.Description));
			}
			HelpUtils.PrintTable(Commands.OrderBy(x => x.Key).ToList(), 4, 20);
		}
	}
}
