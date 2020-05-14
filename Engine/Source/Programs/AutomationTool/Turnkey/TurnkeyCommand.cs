using System;
using System.Collections.Generic;
using System.Reflection;
using System.Linq;
using System.Text;
using Tools.DotNETCommon;

namespace Turnkey
{
	class CommandInfo
	{
		public TurnkeyCommand Command;
		public string DisplayString;
		public string[] Options; // this is in commandline format, like -opt=val

		public CommandInfo(TurnkeyCommand InCommand)
		{
			Command = InCommand;
			DisplayString = InCommand.GetType().Name;
			Options = new string[0];
		}
		public CommandInfo(TurnkeyCommand InCommand, string InDisplayString, string[] InOptions)
		{
			Command = InCommand;
			DisplayString = string.Format("{0} [{1}]", InDisplayString, string.Join(" ", InOptions));
			Options = InOptions;
		}
	}

	abstract class TurnkeyCommand
	{

		protected abstract void Execute(string[] CommandOptions);
		protected virtual Dictionary<string, string[]> GetExtendedCommandsWithOptions()
		{
			return null;
		}
		internal void InternalExecute(string[] CommandOptions)
		{
			Execute(CommandOptions);
		}


		// cached commands
		private static Dictionary<string, TurnkeyCommand> CachedCommandsByName = new Dictionary<string, TurnkeyCommand>(StringComparer.OrdinalIgnoreCase);
		private static List<CommandInfo> CachedCommandsByIndex = new List<CommandInfo>();

		static TurnkeyCommand()
		{
			// look for all subclasses, and cache by their ProviderToken
			foreach (Type AssemType in Assembly.GetExecutingAssembly().GetTypes())
			{
				if (typeof(TurnkeyCommand).IsAssignableFrom(AssemType) && AssemType != typeof(TurnkeyCommand))
				{
					TurnkeyCommand Provider = (TurnkeyCommand)Activator.CreateInstance(AssemType);
					CachedCommandsByName[AssemType.Name] = Provider;

					// always add base option
					CachedCommandsByIndex.Add(new CommandInfo(Provider));
					Dictionary<string, string[]> ExtendedCommands = Provider.GetExtendedCommandsWithOptions();
					if (ExtendedCommands != null)
					{
						foreach (var Pair in ExtendedCommands)
						{
							CachedCommandsByIndex.Add(new CommandInfo(Provider, Pair.Key, Pair.Value));
						}
					}
				}
			}
		}

		public static bool ExecuteCommand(string CommandString=null)
		{
			TurnkeyCommand Command;
			string[] CommandOptions;
			if (CommandString == null)
			{
				int CommandIndex = PromptForCommand();

				// zero means to exit
				if (CommandIndex == 0)
				{
					return false;
				}
				// we know this is okay because PromptForCommand() will validate the input
				Command = CachedCommandsByIndex[CommandIndex - 1].Command;
				CommandOptions = CachedCommandsByIndex[CommandIndex - 1].Options;
			}
			else
			{
				if (!CachedCommandsByName.TryGetValue(CommandString, out Command))
				{
					TurnkeyUtils.Log("Invalid command");
					return false;
				}

				// no extra options, everything would be on the commandline
				CommandOptions = new string[0];
			}

			try
			{
				TurnkeyUtils.Log("");

				// run the command!
				Command.Execute(CommandOptions);

				TurnkeyUtils.Log("");
			}
			catch (System.Exception)
			{
				throw;
			}

			// true if we should keep going
			return true;
		}

		private static int PromptForCommand()
		{
			return TurnkeyUtils.ReadInputInt("Enter command", CachedCommandsByIndex.ConvertAll(x => x.DisplayString), true);
		}
	}
}
