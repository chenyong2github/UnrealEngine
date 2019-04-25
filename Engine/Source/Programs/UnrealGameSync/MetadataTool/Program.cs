// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Reflection;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using System.Net;
using System.IO;
using System.Web.Script.Serialization;

namespace MetadataTool
{
	class Program
	{
		static Dictionary<string, CommandHandler> CommandMap = new Dictionary<string, CommandHandler>(StringComparer.OrdinalIgnoreCase)
		{
			// Issues
			[ "AddIssue" ] = new CommandHandler_Http("POST", "/api/issues", typeof(CommandTypes.AddIssue)),
			[ "GetIssue" ] = new CommandHandler_Http("GET", "/api/issues/{id}"),
			[ "GetIssues" ] = new CommandHandler_Http("GET", "/api/issues", OptionalParams: new string[] { "user" }),
			[ "UpdateIssue" ] = new CommandHandler_Http("PUT", "/api/issues/{Id}", typeof(CommandTypes.UpdateIssue)),

			// Builds
			[ "AddBuild" ] = new CommandHandler_Http("POST", "/api/issues/{Issue}/builds", typeof(CommandTypes.AddBuild)),
			[ "GetBuilds" ] = new CommandHandler_Http("GET", "/api/issues/{Issue}/builds"),

			// Individual builds
			[ "GetBuild" ] = new CommandHandler_Http("GET", "/api/issuebuilds/{Build}"),
			[ "UpdateBuild" ] = new CommandHandler_Http("PUT", "/api/issuebuilds/{Build}", typeof(CommandTypes.UpdateBuild)),

			// Watchers
			[ "GetWatchers" ] = new CommandHandler_Http("GET", "/api/issues/{Issue}/watchers"),
			[ "AddWatcher" ] = new CommandHandler_Http("POST", "/api/issues/{Issue}/watchers", typeof(CommandTypes.Watcher)),
			[ "RemoveWatcher" ] = new CommandHandler_Http("DELETE", "/api/issues/{Issue}/watchers", typeof(CommandTypes.Watcher))
		};

		static int Main(string[] Args)
		{
			try
			{
				bool bResult = InnerMain(new CommandLineArguments(Args));
				return bResult? 0 : 1;
			}
			catch(Exception Ex)
			{
				Console.WriteLine("{0}", Ex.ToString());
				return 1;
			}
		}

		static bool InnerMain(CommandLineArguments Arguments)
		{
			// Find the command name
			string CommandName = null;
			for(int Idx = 0; Idx < Arguments.Count; Idx++)
			{
				if(!Arguments[Idx].StartsWith("-"))
				{
					CommandName = Arguments[Idx];
					Arguments.MarkAsUsed(Idx);
					break;
				}
			}

			// Make sure we've got a command name, and fall back to printing out help if not
			if(Arguments.Count == 0 || CommandName == null)
			{
				PrintCommands();
				return false;
			}

			// Register all the commands
			CommandHandler Command;
			if(!CommandMap.TryGetValue(CommandName, out Command))
			{
				Console.WriteLine("Unknown command '{0}'", CommandName);
				return false;
			}

			// Handle help for this command
			if (Arguments.HasOption("-Help"))
			{
				PrintCommandHelp(CommandName, Command);
				return false;
			}

			// Try to execute the command
			try
			{
				Command.Exec(Arguments);
				return true;
			}
			catch(CommandLineArgumentException Ex)
			{
				Console.WriteLine("{0}", Ex.Message);
				Console.WriteLine();
				PrintCommandHelp(CommandName, Command);
				return false;
			}
		}

		static void PrintCommands()
		{
			Console.WriteLine("SYNTAX:");
			Console.WriteLine("  MetadataTool.exe -Server=http://HostName:Port <Command> <Arguments...>");
			Console.WriteLine();
			Console.WriteLine("Global Options:");
			Console.WriteLine("  -Server=<url>     Specifies the endpoint for the metadata server (as http://hostname:port)");
			Console.WriteLine();
			Console.WriteLine("Http commands:");
			foreach(string CommandName in CommandMap.Keys)
			{
				Console.WriteLine("  {0}", CommandName);
			}
		}

		static void PrintCommandHelp(string Name, CommandHandler Command)
		{
			Console.WriteLine("Arguments for '{0}':", Name);
			Command.Help();
		}
	}
}
