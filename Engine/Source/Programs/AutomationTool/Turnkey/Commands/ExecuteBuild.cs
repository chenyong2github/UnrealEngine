// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace Turnkey.Commands
{
	class ExecuteBuild : TurnkeyCommand
	{
		protected override void Execute(string[] CommandOptions)
		{
			// we need a platform to execute
			List<UnrealTargetPlatform> Platforms = TurnkeyUtils.GetPlatformsFromCommandLineOrUser(CommandOptions, null);

			string Project = TurnkeyUtils.GetVariableValue("Project");
			if (string.IsNullOrEmpty(Project))
			{
				Project = TurnkeyUtils.ReadInput("Enter a project to build");
			}
			
			FileReference ProjectFile = ProjectUtils.FindProjectFileFromName(Project);

			string DesiredBuild = TurnkeyUtils.ParseParamValue("Build", null, CommandOptions);

			// get a list of builds from config
			foreach (UnrealTargetPlatform Platform in Platforms)
			{
 				ConfigHierarchy GameConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, ProjectFile.Directory, Platform);

				List<string> Builds;
				GameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "ExtraProjectBuilds", out Builds);

				Dictionary<string, string> BuildCommands = new Dictionary<string, string>(StringComparer.InvariantCultureIgnoreCase);
				foreach (string Build in Builds)
				{
					Match NameResult = Regex.Match(Build, "Name\\s*=\\s*\"(.*?)\"");
					Match PlatformsResult = Regex.Match(Build, "SpecificPlatforms\\s*=\\s*(.*?)");
					Match ParamsResult = Regex.Match(Build, "BuildCookRunParams=\\s*\"(.*?)\"");

					// make sure required entries are there
					if (!NameResult.Success || !ParamsResult.Success)
					{
						continue;
					}

					// if platforms are specified, and this platform isn't one of them, skip it
					if (PlatformsResult.Success)
					{
						string[] SpecificPlatforms = PlatformsResult.Groups[1].Value.Split(",\"".ToCharArray());
						if (SpecificPlatforms.Length > 0 && !SpecificPlatforms.Contains(Platform.ToString()))
						{
							continue;
						}
					}

					// add to list of commands
					BuildCommands.Add(NameResult.Groups[1].Value, ParamsResult.Groups[1].Value);
				}

				if (BuildCommands.Count == 0)
				{
					TurnkeyUtils.Log("Unable to find a build for platform {0} and project {1}", Platform, Project);
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

			AutomationTool.CommandInfo BCRCommand = new AutomationTool.CommandInfo();
			BCRCommand.CommandName = "BuildCookRun";
			// chop off the first - character in all the commands (see Automation.ParseParam)
			BCRCommand.Arguments = Arguments.Select(x => x.Substring(1)).ToList();

			// use the BCR's exitcode as Turnkey's exitcode
			TurnkeyUtils.ExitCode = Automation.Execute(new List<AutomationTool.CommandInfo>() { BCRCommand }, ScriptCompiler.Commands);
		}

		string GetIniSetting(string Spec, DirectoryReference ProjectDir, UnrealTargetPlatform Platform)
		{
			// handle these cases:
			//  iniif:-option:Engine:/Script/Module.Class:bUseOption
			//  iniif:-option:bUseOption   [convenience for ProjectPackagingSettings setting]
			//  inivalue:Engine:/Script/Module.Class:SomeSetting
			//  inivalue:SomeSetting       [convenience for ProjectPackagingSettings setting]

			string[] Tokens = Spec.Split(":".ToCharArray());

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
					return IniIfValue;
				}
				return "";
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
