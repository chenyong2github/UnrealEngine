// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Console;
using P4VUtils.Commands;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using System.Xml;

namespace P4VUtils
{
	class CustomToolInfo
	{
		public string Name { get; set; }
		public string Arguments { get; set; }
		public bool AddToContextMenu { get; set; } = true;
		public bool ShowConsole { get; set; }
		public bool RefreshUI { get; set; } = true;
		public string Shortcut { get; set; } = "";

		public CustomToolInfo(string Name, string Arguments)
		{
			this.Name = Name;
			this.Arguments = Arguments;
		}
	}

	abstract class Command
	{
		public abstract string Description { get; }

		public abstract CustomToolInfo CustomTool { get; }

		public abstract Task<int> Execute(string[] Args, IReadOnlyDictionary<string, string> ConfigValues, ILogger Logger);
	}

	class Program
	{
		public static IReadOnlyDictionary<string, Command> Commands { get; } = new Dictionary<string, Command>(StringComparer.OrdinalIgnoreCase)
		{
			["cherrypick"] = new CherryPickCommand(),
			["compile"] = new CompileCommand(),
			["converttoedit"] = new ConvertToEditCommand(),
			["describe"] = new DescribeCommand(),
			["edigrate"] = new EdigrateCommand(),
			["preflight"] = new PreflightCommand(),
			["preflightandsubmit"] = new PreflightAndSubmitCommand(),
			["movewriteablepreflightandsubmit"] = new MoveWriteableFilesthenPreflightAndSubmitCommand(),
			["findlastedit"] = new FindLastEditCommand(),
			["snapshot"] = new SnapshotCommand(),
			["backout"] = new BackoutCommand(),
			["copyclnum"] = new CopyCLCommand(),
		};

		static void PrintHelp(ILogger Logger)
		{
			Logger.LogInformation("P4VUtils");
			Logger.LogInformation("Provides useful shortcuts for working with P4V");
			Logger.LogInformation("");
			Logger.LogInformation("Usage:");
			Logger.LogInformation("  P4VUtils [Command] [Arguments...]");
			Logger.LogInformation("");

			List<KeyValuePair<string, string>> Table = new List<KeyValuePair<string, string>>();
			foreach (KeyValuePair<string, Command> Pair in Commands)
			{
				Table.Add(new KeyValuePair<string, string>(Pair.Key, Pair.Value.Description));
			}

			Logger.LogInformation("Commands:");
			HelpUtils.PrintTable(Table, 2, 15, Logger);
		}

		static async Task<int> Main(string[] Args)
		{
			using ILoggerFactory Factory = LoggerFactory.Create(Builder => Builder.AddEpicDefault());//.AddSimpleConsole(Options => { Options.SingleLine = true; Options.IncludeScopes = false; }));
			ILogger Logger = Factory.CreateLogger<Program>();
			Log.Logger = Logger;

			try
			{
				return await InnerMain(Args, Logger);
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Unhandled exception: {Ex}", Ex.ToString());
				return 1;
			}
		}

		static async Task<int> InnerMain(string[] Args, ILogger Logger)
		{
			if (Args.Length == 0 || Args[0].Equals("-help", StringComparison.OrdinalIgnoreCase))
			{
				PrintHelp(Logger);
				return 0;
			}
			else if (Args[0].StartsWith("-"))
			{
				Console.WriteLine("Missing command name");
				PrintHelp(Logger);
				return 1;
			}
			else if (Args[0].Equals("install", StringComparison.OrdinalIgnoreCase))
			{
				Logger.LogInformation("Adding custom tools...");
				return await UpdateCustomToolRegistration(true, Logger);
			}
			else if (Args[0].Equals("uninstall", StringComparison.OrdinalIgnoreCase))
			{
				Logger.LogInformation("Removing custom tools...");
				return await UpdateCustomToolRegistration(false, Logger);
			}
			else if (Commands.TryGetValue(Args[0], out Command? Command))
			{
				if (Args.Any(x => x.Equals("-help", StringComparison.OrdinalIgnoreCase)))
				{
					List<KeyValuePair<string, string>> Parameters = CommandLineArguments.GetParameters(Command.GetType());
					HelpUtils.PrintHelp(Args[0], Command.GetType(), Logger);
					return 0;
				}

				Dictionary<string, string> ConfigValues = ReadConfig();
				return await Command.Execute(Args, ConfigValues, Logger);
			}
			else
		{
				Console.WriteLine("Unknown command: {0}", Args[0]);
				PrintHelp(Logger);
				return 1;
			}
		}

		static Dictionary<string, string> ReadConfig()
		{
			Dictionary<string, string> ConfigValues = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

			string BasePath = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location)!;
			AppendConfig(Path.Combine(BasePath, "P4VUtils.ini"), ConfigValues);
			AppendConfig(Path.Combine(BasePath, "NotForLicensees", "P4VUtils.ini"), ConfigValues);

			return ConfigValues;
		}

		static void AppendConfig(string SourcePath, Dictionary<string, string> ConfigValues)
		{
			if (File.Exists(SourcePath))
			{
				string[] Lines = File.ReadAllLines(SourcePath);
				foreach (string Line in Lines)
				{
					int EqualsIdx = Line.IndexOf('=');
					if (EqualsIdx != -1)
					{
						string Key = Line.Substring(0, EqualsIdx).Trim();
						string Value = Line.Substring(EqualsIdx + 1).Trim();
						ConfigValues[Key] = Value;
					}
				}
			}
		}

		public static bool TryLoadXmlDocument(FileReference Location, XmlDocument Document)
		{
			if (FileReference.Exists(Location))
			{
				try
				{
					Document.Load(Location.FullName);
					return true;
				}
				catch
				{
				}
			}
			return false;
		}

		public static async Task<int> UpdateCustomToolRegistration(bool bInstall, ILogger Logger)
		{
			DirectoryReference? ConfigDir = DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.UserProfile);
			if (ConfigDir == null)
			{
				Logger.LogError("Unable to find config directory.");
				return 1;
			}

			FileReference ConfigFile = FileReference.Combine(ConfigDir, ".p4qt", "customtools.xml");

			XmlDocument Document = new XmlDocument();
			if (!TryLoadXmlDocument(ConfigFile, Document))
			{
				DirectoryReference.CreateDirectory(ConfigFile.Directory);
				using (StreamWriter Writer = new StreamWriter(ConfigFile.FullName))
				{
					await Writer.WriteLineAsync(@"<?xml version=""1.0"" encoding=""UTF-8""?>");
					await Writer.WriteLineAsync(@"<!--perforce-xml-version=1.0-->");
					await Writer.WriteLineAsync(@"<CustomToolDefList varName=""customtooldeflist"">");
					await Writer.WriteLineAsync(@"</CustomToolDefList>");
				}
				Document.Load(ConfigFile.FullName);
			}

			XmlElement? Root = Document.SelectSingleNode("CustomToolDefList") as XmlElement;
			if (Root == null)
			{
				Logger.LogError("Unknown schema for {ConfigFile}", ConfigFile);
				return 1;
			}

			FileReference DotNetLocation = FileReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.ProgramFiles)!, "dotnet", "dotnet.exe");
			FileReference AssemblyLocation = new FileReference(Assembly.GetExecutingAssembly().GetOriginalLocation());

			foreach (XmlNode? ChildNode in Root.SelectNodes("CustomToolDef"))
			{
				XmlElement? ChildElement = ChildNode as XmlElement;
				if (ChildElement != null)
				{
					XmlElement? CommandElement = ChildElement.SelectSingleNode("Definition/Command") as XmlElement;
					if (CommandElement != null && new FileReference(CommandElement.InnerText) == DotNetLocation)
					{
						XmlElement? ArgumentsElement = ChildElement.SelectSingleNode("Definition/Arguments") as XmlElement;
						if (ArgumentsElement != null)
						{
							string[] Arguments = CommandLineArguments.Split(ArgumentsElement.InnerText);
							if (Arguments.Length > 0 && new FileReference(Arguments[0]) == AssemblyLocation)
							{
								Root.RemoveChild(ChildElement);
							}
						}
					}
				}
			}

			// Insert new entries
			if (bInstall)
			{
				foreach (KeyValuePair<string, Command> Pair in Commands)
				{
					CustomToolInfo CustomTool = Pair.Value.CustomTool;

					XmlElement ToolDef = Document.CreateElement("CustomToolDef");
					{
						XmlElement Definition = Document.CreateElement("Definition");
						{
							XmlElement Description = Document.CreateElement("Name");
							Description.InnerText = CustomTool.Name;
							Definition.AppendChild(Description);

							XmlElement Command = Document.CreateElement("Command");
							Command.InnerText = DotNetLocation.FullName;
							Definition.AppendChild(Command);

							XmlElement Arguments = Document.CreateElement("Arguments");
							Arguments.InnerText = $"{AssemblyLocation.FullName.QuoteArgument()} {Pair.Key} {CustomTool.Arguments}";
							Definition.AppendChild(Arguments);

							if (CustomTool.Shortcut.Length > 1)
							{
								XmlElement Shortcut = Document.CreateElement("Shortcut");
								Shortcut.InnerText = CustomTool.Shortcut;
								Definition.AppendChild(Shortcut);
							}
						}
						ToolDef.AppendChild(Definition);

						if (CustomTool.ShowConsole)
						{
							XmlElement Console = Document.CreateElement("Console");
							{
								XmlElement CloseOnExit = Document.CreateElement("CloseOnExit");
								CloseOnExit.InnerText = "false";
								Console.AppendChild(CloseOnExit);
							}
							ToolDef.AppendChild(Console);
						}

						if (CustomTool.RefreshUI)
						{
							XmlElement Refresh = Document.CreateElement("Refresh");
							Refresh.InnerText = CustomTool.RefreshUI ? "true" : "false";
							ToolDef.AppendChild(Refresh);
						}

						XmlElement AddToContext = Document.CreateElement("AddToContext");
						AddToContext.InnerText = CustomTool.AddToContextMenu ? "true" : "false";
						ToolDef.AppendChild(AddToContext);
					}
					Root.AppendChild(ToolDef);
				}
			}

			// Save the new document
			Document.Save(ConfigFile.FullName);
			Logger.LogInformation("Written {ConfigFile}", ConfigFile.FullName);
			return 0;
		}
	}
}
