// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using System.Text.RegularExpressions;
using EpicGames.Core;
using UnrealBuildTool;

namespace Turnkey.Commands
{
	class ExecuteBuild : TurnkeyCommand
	{
		protected override CommandGroup Group => CommandGroup.Builds;

		// could move this to TurnkeyUtils!
		static string GetStructEntry(string Input, string Property, bool bIsArrayProperty)
		{
			string PrimaryRegex;
			string AltRegex = null;
			if (bIsArrayProperty)
			{
				PrimaryRegex = string.Format("{0}\\s*=\\s*\\((.*?)\\)", Property);
			}
			else
			{
				// handle quoted strings, allowing for quoited quotation marks (basically doing " followed by whatever, until we see a quote that was not proceeded by a \, and gather the whole mess in an outer group)
				PrimaryRegex = string.Format("{0}\\s*=\\s*\"((.*?)[^\\\\])\"", Property);
				AltRegex = string.Format("{0}\\s*=\\s*(.*?)\\,", Property);
			}

			// attempt to match it!
			Match Result = Regex.Match(Input, PrimaryRegex);
			if (!Result.Success && AltRegex != null)
			{
				Result = Regex.Match(Input, AltRegex);
			}

			// if we got a success, return the main match value
			if (Result.Success)
			{
				return Result.Groups[1].Value.ToString();
			}

			return null;
		}

		protected override void Execute(string[] CommandOptions)
		{
			// we need a platform to execute
			List<UnrealTargetPlatform> Platforms = TurnkeyUtils.GetPlatformsFromCommandLineOrUser(CommandOptions, null);
			FileReference ProjectFile = TurnkeyUtils.GetProjectFromCommandLineOrUser(CommandOptions);

			// we need a project file, so if canceled, abore this command
			if (ProjectFile == null)
			{
				return;
			}

			string DesiredBuild = TurnkeyUtils.ParseParamValue("Build", null, CommandOptions);

			// get a list of builds from config
			foreach (UnrealTargetPlatform Platform in Platforms)
			{
				ConfigHierarchy GameConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, ProjectFile.Directory, Platform);

				List<string> EngineBuilds;
				List<string> ProjectBuilds;
				GameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "EngineCustomBuilds", out EngineBuilds);
				GameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "ProjectCustomBuilds", out ProjectBuilds);

				List<string> Builds = new List<string>();
				if (EngineBuilds != null)
				{
					Builds.AddRange(EngineBuilds);
				}
				if (ProjectBuilds != null)
				{
					Builds.AddRange(ProjectBuilds);
				}

				Dictionary<string, string> BuildCommands = new Dictionary<string, string>(StringComparer.InvariantCultureIgnoreCase);
				if (Builds != null)
				{
					foreach (string Build in Builds)
					{
						string Name = GetStructEntry(Build, "Name", false);
						string SpecificPlatforms = GetStructEntry(Build, "SpecificPlatforms", true);
						string Params = GetStructEntry(Build, "BuildCookRunParams", false);

						// make sure required entries are there
						if (Name == null || Params == null)
						{
							continue;
						}

						// if platforms are specified, and this platform isn't one of them, skip it
						if (!string.IsNullOrEmpty(SpecificPlatforms))
						{
							string[] PlatformList = SpecificPlatforms.Split(",\"".ToCharArray(), StringSplitOptions.RemoveEmptyEntries);
							// case insensitive Contains
							if (PlatformList.Length > 0 && !PlatformList.Any(x => x.Equals(Platform.ToString(), StringComparison.OrdinalIgnoreCase)))
							{
								continue;
							}
						}

						// add to list of commands
						BuildCommands.Add(Name, Params);
					}
				}

				if (BuildCommands.Count == 0)
				{
					TurnkeyUtils.Log("Unable to find a build for platform {0} and project {1}", Platform, ProjectFile.GetFileNameWithoutAnyExtensions());
					continue;
				}

				string FinalParams;
				if (!string.IsNullOrEmpty(DesiredBuild) && BuildCommands.ContainsKey(DesiredBuild))
				{
					FinalParams = BuildCommands[DesiredBuild];
				}
				else
				{
					List<string> BuildNames = BuildCommands.Keys.ToList();
					int Choice = TurnkeyUtils.ReadInputInt("Choose a build to execute", BuildNames, true);
					if (Choice == 0)
					{
						continue;
					}

					FinalParams = BuildCommands[BuildNames[Choice - 1]];
				}

				FinalParams = FinalParams.Replace("{Project}", "\"" + ProjectFile.FullName + "\"");
				FinalParams = FinalParams.Replace("{Platform}", Platform.ToString());
				FinalParams = PerformIniReplacements(FinalParams, ProjectFile.Directory, Platform);

				TurnkeyUtils.Log("Executing '{0}'...", FinalParams);

				ExecuteBuildCookRun(FinalParams);
			}
		}

		private static void ExecuteBuildCookRun(string Params)
		{
			// split the params on whitespace not inside quotes (see https://stackoverflow.com/questions/4780728/regex-split-string-preserving-quotes/4780801#4780801 to explain the regex)
			Regex Matcher = new Regex("(?<=^[^\"]*(?:\"[^\"]*\"[^\"]*)*)\\s(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)");
			// split the string, removing empty results
			List<string> Arguments = Matcher.Split(Params).Where(x => x != "").ToList();

			UnrealBuildBase.CommandInfo BCRCommand = new UnrealBuildBase.CommandInfo("BuildCookRun");
			// chop off the first - character in all the commands (see Automation.ParseParam)
			BCRCommand.Arguments = Arguments.Select(x => x.Substring(1)).ToList();

			// use the BCR's exitcode as Turnkey's exitcode
			TurnkeyUtils.ExitCode = Automation.Execute(new List<UnrealBuildBase.CommandInfo>() { BCRCommand }, ScriptManager.Commands);
		}

		string GetIniSetting(string Spec, DirectoryReference ProjectDir, UnrealTargetPlatform Platform)
		{
			// handle these cases:
			//  iniif:-option:Engine:/Script/Module.Class:bUseOption
			//  iniif:-option:bUseOption   [convenience for ProjectPackagingSettings setting]
			//  inivalue:Engine:/Script/Module.Class:SomeSetting
			//  inivalue:SomeSetting       [convenience for ProjectPackagingSettings setting]

			string[] CommandAndModifiers = Spec.Split("|".ToCharArray(), StringSplitOptions.RemoveEmptyEntries);
			string[] Tokens = CommandAndModifiers[0].Split(":".ToCharArray());

			string ConfigName;
			string SectionName;
			string Key;
			string IniIfValue = "";
			if (Tokens[0].Equals("iniif", StringComparison.InvariantCultureIgnoreCase))
			{
				if (Tokens.Length == 3)
				{
					ConfigName = "Game";
					SectionName = "/Script/UnrealEd.ProjectPackagingSettings";
					Key = Tokens[2];
					IniIfValue = Tokens[1];
				}
				else if (Tokens.Length == 5)
				{
					ConfigName = Tokens[2];
					SectionName = Tokens[3];
					Key = Tokens[4];
					IniIfValue = Tokens[1];
				}
				else
				{
					TurnkeyUtils.Log("Found a bad iniif spec: {0}", Spec);
					return "";
				}
			}
			else if (Tokens[0].Equals("inivalue", StringComparison.InvariantCultureIgnoreCase))
			{
				if (Tokens.Length == 2)
				{
					ConfigName = "Game";
					SectionName = "/Script/UnrealEd.ProjectPackagingSettings";
					Key = Tokens[1];
				}
				else if (Tokens.Length == 4)
				{
					ConfigName = Tokens[1];
					SectionName = Tokens[2];
					Key = Tokens[3];
				}
				else
				{
					TurnkeyUtils.Log("Found a bad inivalue spec: {0}", Spec);
					return "";
				}
			}
			else
			{
				TurnkeyUtils.Log("Found a bad ini spec: {0}", Spec);
				return "";
			}

			// get the value, if it exists (or empty string if not)
			ConfigHierarchyType ConfigType;
			if (!ConfigHierarchyType.TryParse(ConfigName, out ConfigType))
			{
				TurnkeyUtils.Log("Found a bad config name {0} in spec {1}", ConfigName, Spec);
				return "";
			}
			ConfigHierarchy Config = ConfigCache.ReadHierarchy(ConfigType, ProjectDir, Platform);
			string FoundValue;
			Config.GetString(SectionName, Key, out FoundValue);


			if (Tokens[0].Equals("iniif", StringComparison.InvariantCultureIgnoreCase))
			{
				bool bIsTrue;
				if (bool.TryParse(FoundValue, out bIsTrue) && bIsTrue)
				{
					FoundValue = IniIfValue;
				}
				else
				{
					return "";
				}
			}

			// look to see if we have a replace modifier to update the ini value, and apply it if so
			if (CommandAndModifiers.Length > 1)
			{
				string[] SearchAndReplace = CommandAndModifiers[1].Split("=".ToCharArray());
				if (SearchAndReplace.Length != 2)
				{
					TurnkeyUtils.Log("Found a search/replace modifier {0} in spec {1}", CommandAndModifiers, Spec);
					return "";
				}
				FoundValue = FoundValue.Replace(SearchAndReplace[0], SearchAndReplace[1]);
			}

			return FoundValue;
		}

		private string PerformIniReplacements(string Params, DirectoryReference ProjectDir, UnrealTargetPlatform Platform)
		{
			Regex IniMatch = new Regex("({(ini.*?)})+");
			foreach (Match Match in IniMatch.Matches(Params))
			{
				if (Match.Success)
				{
					// group[1] is {ini.....}, groups[2] is the same but without the {}
					Params = Params.Replace(Match.Groups[1].Value, GetIniSetting(Match.Groups[2].Value, ProjectDir, Platform));
				}
			}

			return Params;
		}
	}
}
