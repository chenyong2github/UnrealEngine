// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.ServiceProcess;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	class SNDBS : ActionExecutor
	{
		private static readonly string SCERoot = Environment.GetEnvironmentVariable("SCE_ROOT_DIR");
		private static readonly string SNDBSExecutable = Path.Combine(SCERoot ?? string.Empty, "Common", "SN-DBS", "bin", "dbsbuild.exe");

		private static readonly string IncludeRewriteRulesDirectory = Path.Combine(UnrealBuildTool.EngineDirectory.FullName, "Build", "SNDBS");
		private static readonly string IncludeRewriteRulesFilePath = Path.Combine(IncludeRewriteRulesDirectory, "include-rewrite-rules.ini");

		private static readonly string ScriptFilePath = Path.Combine(UnrealBuildTool.EngineDirectory.FullName, "Intermediate", "Build", "sndbs.json");

		private static readonly string TemplatesInputDir = Path.Combine(UnrealBuildTool.EngineDirectory.FullName, "Build", "SNDBSTemplates");
		private static readonly string TemplatesOutputDir = Path.Combine(UnrealBuildTool.EngineDirectory.FullName, "Programs", "UnrealBuildTool", "SNDBSTemplates");

		public override string Name => "SNDBS";

		public static bool IsAvailable()
		{
			if(SCERoot == null || !File.Exists(SNDBSExecutable))
			{
				return false;
			}

			ServiceController[] services = ServiceController.GetServices();
			foreach (ServiceController service in services)
			{
				if (service.ServiceName.StartsWith("SNDBS") && service.Status == ServiceControllerStatus.Running)
				{
					return true;
				}
			}

			return false;
		}

		private sealed class SNDBSJob
		{
			public string title { get; set; }
			public string command { get; set; }
			public string working_directory { get; set; }
			public string[] dependencies { get; set; }
			public bool run_locally { get; set; }
		}

		private sealed class SNDBSScript
		{
			public IDictionary<string, SNDBSJob> jobs { get; set; }
		}

		public override bool ExecuteActions(List<Action> Actions, bool bLogDetailedActionStats)
		{
			if (Actions.Count == 0)
				return true;

			// Build the json script file to describe all the actions and their dependencies
			var ActionIds = Actions.ToDictionary(a => a, a => Guid.NewGuid().ToString());
			var Script = new SNDBSScript()
			{
				jobs = Actions.ToDictionary(a => ActionIds[a], a => new SNDBSJob
				{
					title = a.StatusDescription,
					command = $"\"{a.CommandPath}\" {a.CommandArguments}",
					working_directory = a.WorkingDirectory.FullName,
					dependencies = a.PrerequisiteActions.Select(p => ActionIds[p]).ToArray(),
					run_locally = !(a.bCanExecuteRemotely && a.bCanExecuteRemotelyWithSNDBS)
				})
			};

			var ScriptJson = fastJSON.JSON.Instance.ToJSON(Script, new fastJSON.JSONParameters() { UseExtensions = false });
			File.WriteAllText(ScriptFilePath, ScriptJson);

			GenerateSNDBSIncludeRewriteRules();
			PrepareToolTemplates(Actions);

			var LocalProcess = new Process();
			LocalProcess.StartInfo = new ProcessStartInfo(SNDBSExecutable, $"-q -p UE4Code -s \"{ScriptFilePath}\" -templates \"{TemplatesOutputDir}\" --include-rewrite-rules \"{IncludeRewriteRulesFilePath}\"");
			LocalProcess.OutputDataReceived += (Sender, Args) => Log.TraceInformation("{0}", Args.Data);
			LocalProcess.ErrorDataReceived += (Sender, Args) => Log.TraceInformation("{0}", Args.Data);
			return Utils.RunLocalProcess(LocalProcess) == 0;
		}

		private void PrepareToolTemplates(IEnumerable<Action> Actions)
		{
			if (!Directory.Exists(TemplatesOutputDir))
			{
				Directory.CreateDirectory(TemplatesOutputDir);
			}

			var UniqueTools = new HashSet<string>(Actions.Select(a => a.CommandPath.FullName));
			foreach (var Tool in UniqueTools)
			{
				var TemplateFileName = $"{Path.GetFileName(Tool)}.sn-dbs-tool.ini";

				var TemplateInputFilePath = Path.Combine(TemplatesInputDir, TemplateFileName);
				var TemplateOutputFilePath = Path.Combine(TemplatesOutputDir, TemplateFileName);

				// If no base template exists, don't try to generate one.
				if (!File.Exists(TemplateInputFilePath))
				{
					continue;
				}

				string TemplateText = File.ReadAllText(TemplateInputFilePath);

				TemplateText = TemplateText.Replace("{COMMAND_PATH}", Path.GetDirectoryName(Tool));
				foreach (DictionaryEntry Variable in Environment.GetEnvironmentVariables(EnvironmentVariableTarget.Process))
				{
					string VariableName = string.Format("{{{0}}}", Variable.Key);
					TemplateText = TemplateText.Replace(VariableName, Variable.Value.ToString());
				}

				File.WriteAllText(TemplateOutputFilePath, TemplateText);
			}
		}

		private void GenerateSNDBSIncludeRewriteRules()
		{
			// Get all registered platforms. Most will just use the name as is, but some may have an override so
			// add all distinct entries to the list.
			IEnumerable<UnrealTargetPlatform> Platforms = UEBuildPlatform.GetRegisteredPlatforms();
			List<string> PlatformNames = new List<string>();
			foreach (UnrealTargetPlatform Platform in Platforms)
			{
				UEBuildPlatform PlatformData = UEBuildPlatform.GetBuildPlatform(Platform);
				if (!PlatformNames.Contains(PlatformData.GetPlatformName()))
				{
					PlatformNames.Add(PlatformData.GetPlatformName());
				}
			}

			if (!Directory.Exists(IncludeRewriteRulesDirectory))
			{
				Directory.CreateDirectory(IncludeRewriteRulesDirectory);
			}

			List<string> IncludeRewriteRulesText = new List<string>();
			IncludeRewriteRulesText.Add("[computed-include-rules]");
			{
				IncludeRewriteRulesText.Add(@"pattern1=^COMPILED_PLATFORM_HEADER\(\s*([^ ,]+)\)");
				IEnumerable<string> PlatformExpansions = PlatformNames.Select(p => string.Format("{0}/{0}$1|{0}$1", p));
				IncludeRewriteRulesText.Add(string.Format("expansions1={0}", string.Join("|", PlatformExpansions)));
			}
			{
				IncludeRewriteRulesText.Add(@"pattern2=^COMPILED_PLATFORM_HEADER_WITH_PREFIX\(\s*([^ ,]+)\s*,\s*([^ ,]+)\)");
				IEnumerable<string> PlatformExpansions = PlatformNames.Select(p => string.Format("$1/{0}/{0}$2|$1/{0}$2", p));
				IncludeRewriteRulesText.Add(string.Format("expansions2={0}", string.Join("|", PlatformExpansions)));
			}
			{
				IncludeRewriteRulesText.Add(@"pattern3=^[A-Z]{5}_PLATFORM_HEADER_NAME\(\s*([^ ,]+)\)");
				IEnumerable<string> PlatformExpansions = PlatformNames.Select(p => string.Format("{0}/{0}$1|{0}$1", p));
				IncludeRewriteRulesText.Add(string.Format("expansions3={0}", string.Join("|", PlatformExpansions)));
			}
			{
				IncludeRewriteRulesText.Add(@"pattern4=^[A-Z]{5}_PLATFORM_HEADER_NAME_WITH_PREFIX\(\s*([^ ,]+)\s*,\s*([^ ,]+)\)");
				IEnumerable<string> PlatformExpansions = PlatformNames.Select(p => string.Format("$1/{0}/{0}$2|$1/{0}$2", p));
				IncludeRewriteRulesText.Add(string.Format("expansions4={0}", string.Join("|", PlatformExpansions)));
			}

			File.WriteAllText(IncludeRewriteRulesFilePath, string.Join(Environment.NewLine, IncludeRewriteRulesText));
		}
	}
}
