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
	sealed class SNDBS : ActionExecutor
	{
		public override string Name => "SNDBS";

		private static readonly string SCERoot = Environment.GetEnvironmentVariable("SCE_ROOT_DIR");
		private static readonly string SNDBSExecutable = Path.Combine(SCERoot ?? string.Empty, "Common", "SN-DBS", "bin", "dbsbuild.exe");

		private static readonly DirectoryReference IntermediateDir = DirectoryReference.Combine(UnrealBuildTool.EngineDirectory, "Intermediate", "Build", "SNDBS");
		private static readonly FileReference IncludeRewriteRulesFile = FileReference.Combine(IntermediateDir, "include-rewrite-rules.ini");
		private static readonly FileReference ScriptFile = FileReference.Combine(IntermediateDir, "sndbs.json");

		private Dictionary<string, string> ActiveTemplates = BuiltInTemplates.ToDictionary(p => p.Key, p => p.Value);

		public bool EnableEcho { get; set; } = false;

		public SNDBS AddTemplate(string ExeName, string TemplateContents)
		{
			ActiveTemplates.Add(ExeName, TemplateContents);
			return this;
		}

		public static bool IsAvailable()
		{
			// Check the executable exists on disk
			if (SCERoot == null || !File.Exists(SNDBSExecutable))
			{
				return false;
			}

			// Check the service is running
			return ServiceController.GetServices().Any(s => s.ServiceName.StartsWith("SNDBS") && s.Status == ServiceControllerStatus.Running);
		}

		public override bool ExecuteActions(List<Action> Actions, bool bLogDetailedActionStats)
		{
			if (Actions.Count == 0)
				return true;

			// Clean the intermediate directory in case there are any leftovers from previous builds
			if (DirectoryReference.Exists(IntermediateDir))
			{
				DirectoryReference.Delete(IntermediateDir, true);
			}

			DirectoryReference.CreateDirectory(IntermediateDir);
			if (!DirectoryReference.Exists(IntermediateDir))
			{
				throw new BuildException($"Failed to create directory \"{IntermediateDir}\".");
			}

			// Build the json script file to describe all the actions and their dependencies
			var ActionIds = Actions.ToDictionary(a => a, a => Guid.NewGuid().ToString());
			File.WriteAllText(ScriptFile.FullName, Json.Serialize(new Dictionary<string, object>()
			{
				["jobs"] = Actions.ToDictionary(a => ActionIds[a], a =>
				{
					var Job = new Dictionary<string, object>()
					{
						["title"] = a.StatusDescription,
						["command"] = $"\"{a.CommandPath}\" {a.CommandArguments}",
						["working_directory"] = a.WorkingDirectory.FullName,
						["dependencies"] = a.PrerequisiteActions.Select(p => ActionIds[p]).ToArray(),
						["run_locally"] = !(a.bCanExecuteRemotely && a.bCanExecuteRemotelyWithSNDBS)
					};

					if (a.PrerequisiteItems.Count > 0)
					{
						Job["explicit_input_files"] = a.PrerequisiteItems.Select(i => new Dictionary<string, object>()
						{
							["filename"] = i.AbsolutePath
						}).ToList();
					}

					if (EnableEcho)
					{
						var EchoString = string.Join(" ", string.IsNullOrWhiteSpace(a.CommandDescription) ? string.Empty : $"[{a.CommandDescription}]", a.StatusDescription);
						if (!string.IsNullOrWhiteSpace(EchoString))
						{
							Job["echo"] = EchoString;
						}
					}

					return Job;
				})
			}));

			PrepareToolTemplates();
			bool bHasRewrites = GenerateSNDBSIncludeRewriteRules();

			var LocalProcess = new Process();
			LocalProcess.StartInfo = new ProcessStartInfo(SNDBSExecutable, $"-q -p \"Unreal Engine Tasks\" -s \"{ScriptFile}\" -templates \"{IntermediateDir}\"{(bHasRewrites ? $" --include-rewrite-rules \"{IncludeRewriteRulesFile}\"" : "")}");
			LocalProcess.OutputDataReceived += (Sender, Args) => Log.TraceInformation("{0}", Args.Data);
			LocalProcess.ErrorDataReceived += (Sender, Args) => Log.TraceInformation("{0}", Args.Data);
			return Utils.RunLocalProcess(LocalProcess) == 0;
		}

		private void PrepareToolTemplates()
		{
			foreach (var Template in ActiveTemplates)
			{
				var TemplateFile = FileReference.Combine(IntermediateDir, $"{Template.Key}.sn-dbs-tool.ini");
				var TemplateText = Template.Value;

				foreach (DictionaryEntry Variable in Environment.GetEnvironmentVariables(EnvironmentVariableTarget.Process))
				{
					TemplateText = TemplateText.Replace($"{{{Variable.Key}}}", Variable.Value.ToString());
				}

				File.WriteAllText(TemplateFile.FullName, TemplateText);
			}
		}

		private bool GenerateSNDBSIncludeRewriteRules()
		{
			// Get all registered, distinct platform names.
			var Platforms = UEBuildPlatform.GetRegisteredPlatforms()
				.Select(Platform => UEBuildPlatform.GetBuildPlatform(Platform).GetPlatformName())
				.Distinct()
				.ToList();

			if (Platforms.Count > 0)
			{
				// language=regex
				var Lines = new[]
				{
					@"pattern1=^COMPILED_PLATFORM_HEADER\(\s*([^ ,]+)\s*\)",
					$"expansions1={string.Join("|", Platforms.Select(Name => $"{Name}/{Name}$1|{Name}$1"))}",

					@"pattern2=^COMPILED_PLATFORM_HEADER_WITH_PREFIX\(\s*([^ ,]+)\s*,\s*([^ ,]+)\s*\)",
					$"expansions2={string.Join("|", Platforms.Select(Name => $"$1/{Name}/{Name}$2|$1/{Name}$2"))}",

					@"pattern3=^[A-Z]{5}_PLATFORM_HEADER_NAME\(\s*([^ ,]+)\s*\)",
					$"expansions3={string.Join("|", Platforms.Select(Name => $"{Name}/{Name}$1|{Name}$1"))}",

					@"pattern4=^[A-Z]{5}_PLATFORM_HEADER_NAME_WITH_PREFIX\(\s*([^ ,]+)\s*,\s*([^ ,]+)\s*\)",
					$"expansions4={string.Join("|", Platforms.Select(Name => $"$1/{Name}/{Name}$2|$1/{Name}$2"))}"
				};

				File.WriteAllText(IncludeRewriteRulesFile.FullName, string.Join(Environment.NewLine, new[] { "[computed-include-rules]" }.Concat(Lines)));
				return true;
			}
			else
			{
				return false;
			}
		}

		/// <summary>
		/// SN-DBS templates that are automatically included in the build.
		/// </summary>
		private static readonly IReadOnlyDictionary<string, string> BuiltInTemplates = new Dictionary<string, string>()
		{
			["cl-filter.exe"] = @"
[tool]
family=msvc
vc_major_version=14
use_surrogate=true
force_synchronous_pdb_writes=true
error_report_mode=prompt

[group]
server={VC_COMPILER_DIR}\mspdbsrv.exe

[files]
main=cl-filter.exe
file01={VC_COMPILER_DIR}\c1.dll
file01={VC_COMPILER_DIR}\c1ui.dll
file02={VC_COMPILER_DIR}\c1xx.dll
file03={VC_COMPILER_DIR}\c2.dll
file04={VC_COMPILER_DIR}\mspdb140.dll
file05={VC_COMPILER_DIR}\mspdbcore.dll
file06={VC_COMPILER_DIR}\mspdbsrv.exe
file07={VC_COMPILER_DIR}\mspft140.dll
file08={VC_COMPILER_DIR}\vcmeta.dll
file09={VC_COMPILER_DIR}\*\clui.dll
file10={VC_COMPILER_DIR}\*\mspft140ui.dll
file11={VC_COMPILER_DIR}\localespc.dll
file12={VC_COMPILER_DIR}\cppcorecheck.dll
file13={VC_COMPILER_DIR}\experimentalcppcorecheck.dll
file14={VC_COMPILER_DIR}\espxengine.dll
file15={VC_COMPILER_DIR}\c1.exe

[output-file-patterns]
outputfile01=\s*""([^ "",]+\.cpp\.txt)\""

[output-file-rules]
rule01=*\sqmcpp*.log|discard=true
rule02=*\vctoolstelemetry*.dat|discard=true
rule03=*\Microsoft\Windows\Temporary Internet Files\*|discard=true
rule04=*\Microsoft\Windows\INetCache\*|discard=true

[input-file-rules]
rule01=*\sqmcpp*.log|ignore_transient_errors=true;ignore_unexpected_input=true
rule02=*\vctoolstelemetry*.dat|ignore_transient_errors=true;ignore_unexpected_input=true
rule03=*\Microsoft\Windows\Temporary Internet Files\*|ignore_transient_errors=true;ignore_unexpected_input=true
rule04=*\Microsoft\Windows\INetCache\*|ignore_transient_errors=true;ignore_unexpected_input=true

[system-file-filters]
filter01=msvcr*.dll
filter02=msvcp*.dll
filter03=vcruntime140*.dll
filter04=appcrt140*.dll
filter05=desktopcrt140*.dll
filter06=concrt140*.dll",
			["clang++.exe"] = @"
[tool]
family=clang-cl
include_path01=..\include
include_path02=..\include\c++\v1
include_path03=..\lib\clang\*\include

[files]
main=clang++.exe
file01=clang-shared.dll
file02=libclang.dll
file03=NXMangledNamePrinter.dll
file04=..\lib\*

[output-file-patterns]
outputfile01=\s*""([^ "",]+\.cpp\.txt)\""

[output-file-rules]
rule01=*.log|discard=true
rule02=*.dat|discard=true
rule03=*.tmp|discard=true

[system-file-filters]
filter01=msvcr*.dll
filter02=msvcp*.dll
filter03=vcruntime140*.dll
filter04=appcrt140*.dll
filter05=desktopcrt140*.dll
filter06=concrt140*.dll",
		};
	}
}
