// Copyright Epic Games, Inc. All Rights Reserved.

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
	partial class Program
	{
		static List<CommandHandler> Commands = new List<CommandHandler>
		{
			// Issues
			new CommandHandler_Http("AddIssue", "POST", "/api/issues", typeof(CommandTypes.AddIssue)),
			new CommandHandler_Http("GetIssue", "GET", "/api/issues/{id}"),
			new CommandHandler_Http("GetIssues", "GET", "/api/issues", OptionalParams: new string[] { "includeresolved", "maxresults", "user" }),
			new CommandHandler_Http("UpdateIssue", "PUT", "/api/issues/{Id}", typeof(CommandTypes.UpdateIssue)),
			new CommandHandler_Http("DeleteIssue", "DELETE", "/api/issues/{Id}"),

			// Builds
			new CommandHandler_Http("AddBuild", "POST", "/api/issues/{Issue}/builds", typeof(CommandTypes.AddBuild)),
			new CommandHandler_Http("GetBuilds", "GET", "/api/issues/{Issue}/builds"),

			// Individual builds
			new CommandHandler_Http("GetBuild", "GET", "/api/issuebuilds/{Build}"),
			new CommandHandler_Http("UpdateBuild", "PUT", "/api/issuebuilds/{Build}", typeof(CommandTypes.UpdateBuild)),

			// Diagnostics
			new CommandHandler_Http("AddDiagnostic", "POST", "/api/issues/{Issue}/diagnostics", typeof(CommandTypes.AddDiagnostic)),
			new CommandHandler_Http("GetDiagnostics", "GET", "/api/issues/{Issue}/diagnostics"),

			// Watchers
			new CommandHandler_Http("GetWatchers", "GET", "/api/issues/{Issue}/watchers"),
			new CommandHandler_Http("AddWatcher", "POST", "/api/issues/{Issue}/watchers", typeof(CommandTypes.Watcher)),
			new CommandHandler_Http("RemoveWatcher", "DELETE", "/api/issues/{Issue}/watchers", typeof(CommandTypes.Watcher)),

			// Build Health
			new CommandHandler_BuildHealth()
		};

		static int Main(string[] Args)
		{
			try
			{
				bool bResult = InnerMain(new CommandLineArguments(Args));
				return bResult? 0 : 1;
			}
			catch (Exception Ex)
			{
				Log.WriteException(Ex, null);

				WebException WebEx = Ex as WebException;
				if(WebEx != null)
				{
					try
					{
						Dictionary<string, object> Response;
						using (StreamReader ResponseReader = new StreamReader(WebEx.Response.GetResponseStream(), Encoding.UTF8))
						{
							string ResponseContent = ResponseReader.ReadToEnd();
							Response = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(ResponseContent);
						}

						StringBuilder Message = new StringBuilder("Additional context:");
						Message.AppendFormat("\n  Summary:\n    {0}", WebEx.Message);
						foreach(KeyValuePair<string, object> Pair in Response)
						{
							Message.AppendFormat("\n  {0}:\n    {1}", Pair.Key, Pair.Value.ToString().Replace("\n", "\n    "));
						}

						Log.TraceInformation("{0}", Message.ToString());
						return 1;
					}
					catch
					{
					}
				}
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
			CommandHandler Command = Commands.FirstOrDefault(x => String.Compare(x.Name, CommandName, StringComparison.OrdinalIgnoreCase) == 0);
			if(Command == null)
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
			Console.WriteLine("Valid commands:");
			foreach(CommandHandler Command in Commands)
			{
				Console.WriteLine("  {0}", Command.Name);
			}
		}

		static void PrintCommandHelp(string Name, CommandHandler Command)
		{
			Console.WriteLine("Arguments for '{0}':", Name);
			Command.Help();
		}
	}
}
