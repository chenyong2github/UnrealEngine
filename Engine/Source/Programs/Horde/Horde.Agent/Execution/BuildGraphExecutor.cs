// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Grpc.Core;
using HordeAgent.Execution.Interfaces;
using HordeAgent.Parser;
using HordeAgent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;
using OpenTracing;
using OpenTracing.Util;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Net.Security;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace HordeAgent.Execution
{
	abstract class BuildGraphExecutor : IExecutor
	{
		protected class ExportedNode
		{
			public string Name { get; set; } = String.Empty;
			public bool RunEarly { get; set; }
			public bool? Warnings { get; set; }
			public List<string> InputDependencies { get; set; } = new List<string>();
			public List<string> OrderDependencies { get; set; } = new List<string>();
		}

		protected class ExportedGroup
		{
			public List<string> Types { get; set; } = new List<string>();
			public List<ExportedNode> Nodes { get; set; } = new List<ExportedNode>();
		}

		protected class ExportedAggregate
		{
			public string Name { get; set; } = String.Empty;
			public List<string> Nodes { get; set; } = new List<string>();
		}

		protected class ExportedLabel
		{
			public string? Name { get; set; }
			public string? Category { get; set; }
			public string? UgsBadge { get; set; }
			public string? UgsProject { get; set; }
			public LabelChange Change { get; set; } = LabelChange.Current;
			public List<string> RequiredNodes { get; set; } = new List<string>();
			public List<string> IncludedNodes { get; set; } = new List<string>();
		}

		protected class ExportedBadge
		{
			public string Name { get; set; } = String.Empty;
			public string? Project { get; set; }
			public int Change { get; set; }
			public string? Dependencies { get; set; }
		}

		protected class ExportedGraph
		{
			public List<ExportedGroup> Groups { get; set; } = new List<ExportedGroup>();
			public List<ExportedAggregate> Aggregates { get; set; } = new List<ExportedAggregate>();
			public List<ExportedLabel> Labels { get; set; } = new List<ExportedLabel>();
			public List<ExportedBadge> Badges { get; set; } = new List<ExportedBadge>();
		}

		class TraceEvent
		{
			public string Name { get; set; } = "Unknown";
			public string? Service { get; set; }
			public string? Resource { get; set; }
			public DateTimeOffset StartTime { get; set; }
			public DateTimeOffset FinishTime { get; set; }
			public Dictionary<string, string>? Metadata { get; set; }

			[JsonIgnore]
			public int Index { get; set; }
		}

		class TraceEventList
		{
			public List<TraceEvent> Spans { get; set; } = new List<TraceEvent>();
		}

		class TraceSpan
		{
			public string? Name { get; set; }
			public string? Service { get; set; }
			public string? Resource { get; set; }
			public long Start { get; set; }
			public long Finish { get; set; }
			public Dictionary<string, string>? Properties { get; set; }
			public List<TraceSpan>? Children { get; set; }
		}

		class TestDataItem
		{
			public string Key { get; set; } = String.Empty;
			public Dictionary<string, object> Data { get; set; } = new Dictionary<string, object>();
		}

		class TestData
		{
			public List<TestDataItem> Items { get; set; } = new List<TestDataItem>();
		}

		class ReportData
		{
			public ReportScope Scope { get; set; }
			public ReportPlacement Placement { get; set; }
			public string Name { get; set; } = String.Empty;
			public string FileName { get; set; } = String.Empty;
		}

		const string ScriptArgumentPrefix = "-Script=";
		const string TargetArgumentPrefix = "-Target=";

		const string PreprocessedScript = "Engine/Saved/Horde/Preprocessed.xml";
		const string PreprocessedSchema = "Engine/Saved/Horde/Preprocessed.xsd";

		protected List<string> Targets = new List<string>();
		protected string? ScriptFileName;
		protected bool bPreprocessScript;

		protected string JobId;
		protected string BatchId;
		protected string AgentTypeName;

		protected GetJobResponse Job;
		protected GetStreamResponse Stream;
		protected GetAgentTypeResponse AgentType;

		protected List<string> AdditionalArguments = new List<string>();

		protected bool CompileAutomationTool = true;

		protected IRpcConnection RpcConnection;
		protected Dictionary<string, string> RemapAgentTypes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

		public BuildGraphExecutor(IRpcConnection RpcConnection, string JobId, string BatchId, string AgentTypeName)
		{
			this.RpcConnection = RpcConnection;

			this.JobId = JobId;
			this.BatchId = BatchId;
			this.AgentTypeName = AgentTypeName;

			this.Job = null!;
			this.Stream = null!;
			this.AgentType = null!;
		}

		public virtual async Task InitializeAsync(ILogger Logger, CancellationToken CancellationToken)
		{
			// Get the job settings
			Job = await RpcConnection.InvokeAsync(x => x.GetJobAsync(new GetJobRequest(JobId), null, null, CancellationToken), new RpcContext(), CancellationToken);

			// Get the stream settings
			Stream = await RpcConnection.InvokeAsync(x => x.GetStreamAsync(new GetStreamRequest(Job.StreamId), null, null, CancellationToken), new RpcContext(), CancellationToken);

			// Get the agent type to determine how to configure this machine
			AgentType = Stream.AgentTypes.FirstOrDefault(x => x.Key == AgentTypeName).Value;
			if (AgentType == null)
			{
				AgentType = new GetAgentTypeResponse();
			}

			Logger.LogInformation("Configured as agent type {AgentType}", AgentTypeName);

			// Figure out if we're running as an admin
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				if (IsUserAdministrator())
				{
					Logger.LogInformation("Running as an elevated user.");
				}
				else
				{
					Logger.LogInformation("Running as an restricted user.");
				}
			}

			// Get the BuildGraph arguments
			foreach (string Argument in Job.Arguments)
			{
				const string RemapAgentTypesPrefix = "-RemapAgentTypes=";
				if (Argument.StartsWith(RemapAgentTypesPrefix, StringComparison.OrdinalIgnoreCase))
				{
					foreach (string Map in Argument.Substring(RemapAgentTypesPrefix.Length).Split(','))
					{
						int ColonIdx = Map.IndexOf(':');
						if (ColonIdx != -1)
						{
							RemapAgentTypes[Map.Substring(0, ColonIdx)] = Map.Substring(ColonIdx + 1);
						}
					}
				}
				else if (Argument.StartsWith(ScriptArgumentPrefix, StringComparison.OrdinalIgnoreCase))
				{
					ScriptFileName = Argument.Substring(ScriptArgumentPrefix.Length);
				}
				else if (Argument.Equals("-Preprocess", StringComparison.OrdinalIgnoreCase))
				{
					bPreprocessScript = true;
				}
				else if (Argument.StartsWith(TargetArgumentPrefix, StringComparison.OrdinalIgnoreCase))
				{
					Targets.Add(Argument.Substring(TargetArgumentPrefix.Length));
				}
				else
				{
					AdditionalArguments.Add(Argument);
				}
			}
			if (Job.PreflightChange != 0)
			{
				AdditionalArguments.Add($"-set:PreflightChange={Job.PreflightChange}");
			}
		}

		public static bool IsUserAdministrator()
		{
			try
			{
				using(WindowsIdentity Identity = WindowsIdentity.GetCurrent())
				{
					WindowsPrincipal Principal = new WindowsPrincipal(Identity);
					return Principal.IsInRole(WindowsBuiltInRole.Administrator);
				}
			}
			catch
			{
				return false;
			}
		}

		public async Task<JobStepOutcome> RunAsync(BeginStepResponse Step, ILogger Logger, CancellationToken CancellationToken)
		{
			if (Step.Name == "Setup Build")
			{
				if (await SetupAsync(Step, Logger, CancellationToken))
				{
					return JobStepOutcome.Success;
				}
				else
				{
					return JobStepOutcome.Failure;
				}
			}
			else
			{
				if (await ExecuteAsync(Step, Logger, CancellationToken))
				{
					return JobStepOutcome.Success;
				}
				else
				{
					return JobStepOutcome.Failure;
				}
			}
		}

		public virtual Task FinalizeAsync(ILogger Logger, CancellationToken CancellationToken)
		{
			return Task.CompletedTask;
		}

		DirectoryReference GetAutomationToolDir(DirectoryReference SharedStorageDir)
		{
			return DirectoryReference.Combine(SharedStorageDir, "UAT");
		}

		protected void CopyAutomationTool(DirectoryReference SharedStorageDir, DirectoryReference WorkspaceDir, ILogger Logger)
		{
			DirectoryReference BuildDir = GetAutomationToolDir(SharedStorageDir);

			FileReference AutomationTool = FileReference.Combine(BuildDir, "Engine", "Binaries", "DotNET", "AutomationTool.exe");
			if(FileReference.Exists(AutomationTool))
			{
				Logger.LogInformation("Copying AutomationTool binaries from '{BuildDir}' to '{WorkspaceDir}", BuildDir, WorkspaceDir);
				foreach (FileReference SourceFile in DirectoryReference.EnumerateFiles(BuildDir, "*", SearchOption.AllDirectories))
				{
					FileReference TargetFile = FileReference.Combine(WorkspaceDir, SourceFile.MakeRelativeTo(BuildDir));
					if (FileReference.Exists(TargetFile))
					{
						FileUtils.ForceDeleteFile(TargetFile);
					}
					DirectoryReference.CreateDirectory(TargetFile.Directory);
					FileReference.Copy(SourceFile, TargetFile);
				}
				CompileAutomationTool = false;
			}
		}

		private async Task StorePreprocessedFile(FileReference? LocalFile, string StepId, DirectoryReference? SharedStorageDir, ILogger Logger, CancellationToken CancellationToken)
		{
			if (LocalFile != null)
			{
				string FileName = LocalFile.GetFileName();
				await ArtifactUploader.UploadAsync(RpcConnection, JobId, BatchId, StepId, FileName, LocalFile, Logger, CancellationToken);

				if (SharedStorageDir != null)
				{
					FileReference RemoteFile = FileReference.Combine(SharedStorageDir, FileName);
					DirectoryReference.CreateDirectory(RemoteFile.Directory);
					FileReference.Copy(LocalFile, RemoteFile);
				}
			}
		}

		private void FetchPreprocessedFile(FileReference LocalFile, DirectoryReference? SharedStorageDir, ILogger Logger)
		{
			if (!FileReference.Exists(LocalFile))
			{
				if (SharedStorageDir == null)
				{
					throw new FileNotFoundException($"Missing preprocessed script {LocalFile}");
				}

				FileReference RemoteFile = FileReference.Combine(SharedStorageDir, LocalFile.GetFileName());
				Logger.LogInformation("Copying {RemoteFile} to {LocalFile}", RemoteFile, LocalFile);
				DirectoryReference.CreateDirectory(LocalFile.Directory);
				FileReference.Copy(RemoteFile, LocalFile, false);
			}
		}

		protected abstract Task<bool> SetupAsync(BeginStepResponse Step, ILogger Logger, CancellationToken CancellationToken);

		protected abstract Task<bool> ExecuteAsync(BeginStepResponse Step, ILogger Logger, CancellationToken CancellationToken);

		protected virtual async Task<bool> SetupAsync(BeginStepResponse Step, DirectoryReference WorkspaceDir, DirectoryReference? SharedStorageDir, IReadOnlyDictionary<string, string> EnvVars, ILogger Logger, CancellationToken CancellationToken)
		{
			FileReference DefinitionFile = FileReference.Combine(WorkspaceDir, "Engine", "Saved", "Horde", "Exported.json");

			StringBuilder Arguments = new StringBuilder($"BuildGraph");
			if (ScriptFileName != null)
			{
				Arguments.AppendArgument(ScriptArgumentPrefix, ScriptFileName);
			}
			Arguments.AppendArgument("-HordeExport=", DefinitionFile.FullName);
			Arguments.AppendArgument("-ListOnly");
			//Arguments.AppendArgument("-TokenSignature=", JobId.ToString());
			foreach (string AdditionalArgument in AdditionalArguments)
			{
				Arguments.AppendArgument(AdditionalArgument);
			}

			FileReference? PreprocessedScriptFile = null;
			FileReference? PreprocessedSchemaFile = null;
			if (bPreprocessScript)
			{
				PreprocessedScriptFile = FileReference.Combine(WorkspaceDir, PreprocessedScript);
				Arguments.AppendArgument("-Preprocess=", PreprocessedScriptFile.FullName);

				PreprocessedSchemaFile = FileReference.Combine(WorkspaceDir, PreprocessedSchema);
				Arguments.AppendArgument("-Schema=", PreprocessedSchemaFile.FullName);
			}
			if (SharedStorageDir != null && !bPreprocessScript) // Do not precompile when preprocessing the script; other agents may have a different view of UAT
			{
				DirectoryReference BuildDir = GetAutomationToolDir(SharedStorageDir);
				Arguments.Append($" CopyUAT -WithLauncher -TargetDir=\"{BuildDir}\"");
			}

			int Result = await ExecuteAutomationToolAsync(Step, WorkspaceDir, Arguments.ToString(), EnvVars, Step.Credentials, Logger, CancellationToken);
			if (Result != 0)
			{
				return false;
			}
			
			await ArtifactUploader.UploadAsync(RpcConnection, JobId, BatchId, Step.StepId, DefinitionFile.GetFileName(), DefinitionFile, Logger, CancellationToken);
			await StorePreprocessedFile(PreprocessedScriptFile, Step.StepId, SharedStorageDir, Logger, CancellationToken);
			await StorePreprocessedFile(PreprocessedSchemaFile, Step.StepId, SharedStorageDir, Logger, CancellationToken);

			JsonSerializerOptions Options = new JsonSerializerOptions();
			Options.PropertyNameCaseInsensitive = true;
			Options.Converters.Add(new JsonStringEnumConverter());

			ExportedGraph Graph = JsonSerializer.Deserialize<ExportedGraph>(await FileReference.ReadAllBytesAsync(DefinitionFile), Options);

			List<string> MissingAgentTypes = new List<string>();

			UpdateGraphRequest UpdateGraph = new UpdateGraphRequest();
			UpdateGraph.JobId = JobId;
			foreach (ExportedGroup ExportedGroup in Graph.Groups)
			{
				string? AgentTypeName = null;
				foreach (string ValidAgentTypeName in ExportedGroup.Types)
				{
					string? ThisAgentTypeName;
					if (!RemapAgentTypes.TryGetValue(ValidAgentTypeName, out ThisAgentTypeName))
					{
						ThisAgentTypeName = ValidAgentTypeName;
					}

					if (Stream!.AgentTypes.ContainsKey(ThisAgentTypeName))
					{
						AgentTypeName = ThisAgentTypeName;
						break;
					}
				}

				if (AgentTypeName == null)
				{
					AgentTypeName = ExportedGroup.Types.FirstOrDefault() ?? "Unspecified";
					foreach (ExportedNode Node in ExportedGroup.Nodes)
					{
						MissingAgentTypes.Add($"  {Node.Name} ({String.Join(", ", ExportedGroup.Types)})");
					}
				}

				CreateGroupRequest CreateGroup = new CreateGroupRequest();
				CreateGroup.AgentType = AgentTypeName;

				foreach (ExportedNode ExportedNode in ExportedGroup.Nodes)
				{
					CreateNodeRequest CreateNode = new CreateNodeRequest();
					CreateNode.Name = ExportedNode.Name;
					if (ExportedNode.InputDependencies != null)
					{
						CreateNode.InputDependencies.Add(ExportedNode.InputDependencies);
					}
					if (ExportedNode.OrderDependencies != null)
					{
						CreateNode.OrderDependencies.Add(ExportedNode.OrderDependencies);
					}
					CreateNode.RunEarly = ExportedNode.RunEarly;
					CreateNode.Warnings = ExportedNode.Warnings;
					CreateNode.Priority = Priority.Normal;
					CreateGroup.Nodes.Add(CreateNode);
				}
				UpdateGraph.Groups.Add(CreateGroup);
			}

			if (MissingAgentTypes.Count > 0)
			{
				Logger.LogInformation("The following nodes cannot be executed in this stream due to missing agent types:");
				foreach (string MissingAgentType in MissingAgentTypes)
				{
					Logger.LogInformation(MissingAgentType);
				}
			}

			foreach (ExportedAggregate ExportedAggregate in Graph.Aggregates)
			{
				CreateAggregateRequest CreateAggregate = new CreateAggregateRequest();
				CreateAggregate.Name = ExportedAggregate.Name;
				CreateAggregate.Nodes.AddRange(ExportedAggregate.Nodes);
				UpdateGraph.Aggregates.Add(CreateAggregate);
			}

			foreach (ExportedLabel ExportedLabel in Graph.Labels)
			{
				CreateLabelRequest CreateLabel = new CreateLabelRequest();
				if (ExportedLabel.Name != null)
				{
					CreateLabel.DashboardName = ExportedLabel.Name;
				}
				if (ExportedLabel.Category != null)
				{
					CreateLabel.DashboardCategory = ExportedLabel.Category;
				}
				if (ExportedLabel.UgsBadge != null)
				{
					CreateLabel.UgsName = ExportedLabel.UgsBadge;
				}
				if (ExportedLabel.UgsProject != null)
				{
					CreateLabel.UgsProject = ExportedLabel.UgsProject;
				}
				CreateLabel.Change = ExportedLabel.Change;
				CreateLabel.RequiredNodes.AddRange(ExportedLabel.RequiredNodes);
				CreateLabel.IncludedNodes.AddRange(ExportedLabel.IncludedNodes);
				UpdateGraph.Labels.Add(CreateLabel);
			}

			Dictionary<string, ExportedNode> NameToNode = Graph.Groups.SelectMany(x => x.Nodes).ToDictionary(x => x.Name, x => x);
			foreach (ExportedBadge ExportedBadge in Graph.Badges)
			{
				CreateLabelRequest CreateLabel = new CreateLabelRequest();
				CreateLabel.UgsName = ExportedBadge.Name;

				string? Project = ExportedBadge.Project;
				if (Project != null && Project.StartsWith("//", StringComparison.Ordinal))
				{
					int NextIdx = Project.IndexOf('/', 2);
					if (NextIdx != -1)
					{
						NextIdx = Project.IndexOf('/', NextIdx + 1);
						if (NextIdx != -1 && !Project.Substring(NextIdx).Equals("/...", StringComparison.Ordinal))
						{
							CreateLabel.UgsProject = Project.Substring(NextIdx + 1);
						}
					}
				}

				if (ExportedBadge.Change == Job.Change || ExportedBadge.Change == 0)
				{
					CreateLabel.Change = LabelChange.Current;
				}
				else if (ExportedBadge.Change == Job.CodeChange)
				{
					CreateLabel.Change = LabelChange.Code;
				}
				else
				{
					Logger.LogWarning("Badge is set to display for changelist {Change}. This is neither the current changelist ({CurrentChange}) or the current code changelist ({CurrentCodeChange}).", ExportedBadge.Change, Job.Change, Job.CodeChange);
				}

				if (ExportedBadge.Dependencies != null)
				{
					CreateLabel.RequiredNodes.AddRange(ExportedBadge.Dependencies.Split(new char[] { ';' }, StringSplitOptions.RemoveEmptyEntries));

					HashSet<string> Dependencies = new HashSet<string>();
					foreach(string RequiredNode in CreateLabel.RequiredNodes)
					{
						GetRecursiveDependencies(RequiredNode, NameToNode, Dependencies);
					}
					CreateLabel.IncludedNodes.AddRange(Dependencies);
				}
				UpdateGraph.Labels.Add(CreateLabel);
			}
			

			await RpcConnection.InvokeAsync(x => x.UpdateGraphAsync(UpdateGraph, null, null, CancellationToken), new RpcContext(), CancellationToken);

			HashSet<string> ValidTargets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			ValidTargets.Add("Setup Build");
			ValidTargets.UnionWith(UpdateGraph.Groups.SelectMany(x => x.Nodes).Select(x => x.Name));
			ValidTargets.UnionWith(UpdateGraph.Aggregates.Select(x => x.Name));
			foreach (string Target in Targets)
			{
				if (!ValidTargets.Contains(Target))
				{
					Logger.LogWarning("Target '{Target}' does not exist in the graph", Target);
				}
			}

			return true;
		}

		private static void GetRecursiveDependencies(string Name, Dictionary<string, ExportedNode> NameToNode, HashSet<string> Dependencies)
		{
			ExportedNode? Node;
			if (NameToNode.TryGetValue(Name, out Node) && Dependencies.Add(Node.Name))
			{
				foreach (string InputDependency in Node.InputDependencies)
				{
					GetRecursiveDependencies(InputDependency, NameToNode, Dependencies);
				}
			}
		}

		protected async Task<bool> ExecuteAsync(BeginStepResponse Step, DirectoryReference WorkspaceDir, DirectoryReference? SharedStorageDir, IReadOnlyDictionary<string, string> EnvVars, ILogger Logger, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("BuildGraph");
			if (bPreprocessScript)
			{
				FileReference LocalPreprocessedScript = FileReference.Combine(WorkspaceDir, PreprocessedScript);
				FetchPreprocessedFile(LocalPreprocessedScript, SharedStorageDir, Logger);
				Arguments.AppendArgument(ScriptArgumentPrefix, LocalPreprocessedScript.FullName);

				FileReference LocalPreprocessedSchema = FileReference.Combine(WorkspaceDir, PreprocessedSchema);
				FetchPreprocessedFile(LocalPreprocessedSchema, SharedStorageDir, Logger);
				Arguments.AppendArgument("-ImportSchema=", LocalPreprocessedSchema.FullName);
			}
			else if(ScriptFileName != null)
			{
				Arguments.AppendArgument(ScriptArgumentPrefix, ScriptFileName);
			}
			Arguments.AppendArgument("-SingleNode=", Step.Name);
			if (SharedStorageDir != null)
			{
				Arguments.AppendArgument("-SharedStorageDir=", SharedStorageDir.FullName);
			}
//			Arguments.AppendArgument("-TokenSignature=", JobId.ToString());
			foreach (string AdditionalArgument in AdditionalArguments)
			{
				if (!bPreprocessScript || !AdditionalArgument.StartsWith("-set:", StringComparison.OrdinalIgnoreCase))
				{
					Arguments.AppendArgument(AdditionalArgument);
				}
			}

			return await ExecuteAutomationToolAsync(Step, WorkspaceDir, Arguments.ToString(), EnvVars, Step.Credentials, Logger, CancellationToken) == 0;
		}

		protected async Task<int> ExecuteAutomationToolAsync(BeginStepResponse Step, DirectoryReference WorkspaceDir, string Arguments, IReadOnlyDictionary<string, string> EnvVars, IReadOnlyDictionary<string, string> Credentials, ILogger Logger, CancellationToken CancellationToken)
		{
			int Result;
			using (IScope Scope = GlobalTracer.Instance.BuildSpan("BuildGraph").StartActive())
			{
				if (!CompileAutomationTool)
				{
					Arguments += " -NoCompile";
				}

				if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
				{
					Result = await ExecuteCommandAsync(Step, WorkspaceDir, Environment.GetEnvironmentVariable("COMSPEC") ?? "cmd.exe", $"/C \"\"{WorkspaceDir}\\Engine\\Build\\BatchFiles\\RunUAT.bat\" {Arguments}\"", EnvVars, Credentials, Logger, CancellationToken);
				}
				else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
				{
					Result = await ExecuteCommandAsync(Step, WorkspaceDir, "/bin/bash", $"\"{WorkspaceDir}/Engine/Build/BatchFiles/RunUAT.sh\" {Arguments}", EnvVars, Credentials, Logger, CancellationToken);
				}
				else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
				{
					Result = await ExecuteCommandAsync(Step, WorkspaceDir, "/bin/sh", $"\"{WorkspaceDir}/Engine/Build/BatchFiles/RunUAT.sh\" {Arguments}", EnvVars, Credentials, Logger, CancellationToken);
				}
				else
				{
					throw new Exception("Unsupported platform");
				}

				CompileAutomationTool = false;
			}
			return Result;
		}

		static void AddRestrictedDirs(List<DirectoryReference> Directories, string SubFolder)
		{
			int NumDirs = Directories.Count;
			for (int Idx = 0; Idx < NumDirs; Idx++)
			{
				DirectoryReference SubDir = DirectoryReference.Combine(Directories[Idx], SubFolder);
				if(DirectoryReference.Exists(SubDir))
				{
					Directories.AddRange(DirectoryReference.EnumerateDirectories(SubDir));
				}
			}
		}

		static async Task<List<string>> ReadIgnorePatternsAsync(DirectoryReference WorkspaceDir, ILogger Logger)
		{
			List<DirectoryReference> BaseDirs = new List<DirectoryReference>();
			BaseDirs.Add(DirectoryReference.Combine(WorkspaceDir, "Engine"));
			AddRestrictedDirs(BaseDirs, "Restricted");
			AddRestrictedDirs(BaseDirs, "Platforms");

			List<string> IgnorePatternLines = new List<string>(Properties.Resources.IgnorePatterns.Split('\n', StringSplitOptions.RemoveEmptyEntries));
			foreach (DirectoryReference BaseDir in BaseDirs)
			{
				FileReference IgnorePatternFile = FileReference.Combine(BaseDir, "Build", "Horde", "IgnorePatterns.txt");
				if (FileReference.Exists(IgnorePatternFile))
				{
					Logger.LogInformation("Reading ignore patterns from {0}...", IgnorePatternFile);
					IgnorePatternLines.AddRange(await FileReference.ReadAllLinesAsync(IgnorePatternFile));
				}
			}

			HashSet<string> IgnorePatterns = new HashSet<string>(StringComparer.Ordinal);
			foreach (string Line in IgnorePatternLines)
			{
				string TrimLine = Line.Trim();
				if (TrimLine.Length > 0 && TrimLine[0] != '#')
				{
					IgnorePatterns.Add(TrimLine);
				}
			}

			return IgnorePatterns.ToList();
		}

		async Task<int> ExecuteCommandAsync(BeginStepResponse Step, DirectoryReference WorkspaceDir, string FileName, string Arguments, IReadOnlyDictionary<string, string> EnvVars, IReadOnlyDictionary<string, string> Credentials, ILogger Logger, CancellationToken CancellationToken)
		{
			Dictionary<string, string> NewEnvironment = new Dictionary<string, string>(EnvVars);
			foreach (object? Object in Environment.GetEnvironmentVariables())
			{
				System.Collections.DictionaryEntry Entry = (System.Collections.DictionaryEntry)Object!;
				string Key = Entry.Key.ToString()!;
				if (!NewEnvironment.ContainsKey(Key))
				{
					NewEnvironment[Key] = Entry.Value!.ToString()!;
				}
			}
			foreach (KeyValuePair<string, string> EnvVar in AgentType.Environment)
			{
				Logger.LogInformation("Setting env var: {Key}={Value}", EnvVar.Key, EnvVar.Value);
				NewEnvironment[EnvVar.Key] = EnvVar.Value;
			}
			foreach (KeyValuePair<string, string> EnvVar in Credentials)
			{
				Logger.LogInformation("Setting env var: {Key}=[redacted]", EnvVar.Key);
				NewEnvironment[EnvVar.Key] = EnvVar.Value;
			}

			NewEnvironment["IsBuildMachine"] = "1";

			DirectoryReference LogDir = DirectoryReference.Combine(WorkspaceDir, "Engine", "Programs", "AutomationTool", "Saved", "Logs");
			NewEnvironment["uebp_LogFolder"] = LogDir.FullName;

			DirectoryReference TelemetryDir = DirectoryReference.Combine(WorkspaceDir, "Engine", "Programs", "AutomationTool", "Saved", "Telemetry");
			FileUtils.ForceDeleteDirectoryContents(TelemetryDir);
			NewEnvironment["UE_TELEMETRY_DIR"] = TelemetryDir.FullName;

			DirectoryReference TestDataDir = DirectoryReference.Combine(WorkspaceDir, "Engine", "Programs", "AutomationTool", "Saved", "TestData");
			FileUtils.ForceDeleteDirectoryContents(TestDataDir);
			NewEnvironment["UE_TESTDATA_DIR"] = TestDataDir.FullName;

			NewEnvironment["UE_HORDE_JOBID"] = JobId;
			NewEnvironment["UE_HORDE_BATCHID"] = BatchId;
			NewEnvironment["UE_HORDE_STEPID"] = Step.StepId;

			// Enable structured logging output
			NewEnvironment["UE_LOG_JSON"] = "1";

			// Disable the S3DDC. This is technically a Fortnite-specific setting, but affects a large number of branches and is hard to retrofit. 
			// Setting here for now, since it's likely to be temporary.
			if(RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				NewEnvironment["UE-S3DataCachePath"] = "None";
			}

			// Clear out the telemetry directory
			if (DirectoryReference.Exists(TelemetryDir))
			{
				FileUtils.ForceDeleteDirectoryContents(TelemetryDir);
			}
			
			using (ManagedProcessGroup ProcessGroup = new ManagedProcessGroup())
			{
				using (ManagedProcess Process = new ManagedProcess(ProcessGroup, FileName, Arguments, null, NewEnvironment, null, ProcessPriorityClass.Normal))
				{
					LogParserContext Context = new LogParserContext();
					Context.WorkspaceDir = WorkspaceDir;
					Context.PerforceStream = Stream.Name;
					Context.PerforceChange = Job.Change;

					List<string> IgnorePatterns = await ReadIgnorePatternsAsync(WorkspaceDir, Logger);
					using (LogParser Filter = new LogParser(Logger, Context, IgnorePatterns))
					{
						await Process.CopyToAsync((Buffer, Offset, Length) => Filter.WriteData(Buffer.AsMemory(Offset, Length)), 4096, CancellationToken);
						Filter.Flush();
					}
					
					Process.WaitForExit();
					if (DirectoryReference.Exists(TelemetryDir))
					{
						List<TraceEventList> TelemetryList = new List<TraceEventList>();
						foreach (FileReference TelemetryFile in DirectoryReference.EnumerateFiles(TelemetryDir, "*.json"))
						{
							Logger.LogInformation("Reading telemetry from {File}", TelemetryFile);
							byte[] Data = await FileReference.ReadAllBytesAsync(TelemetryFile);

							TraceEventList Telemetry = JsonSerializer.Deserialize<TraceEventList>(Data.AsSpan());
							if (Telemetry.Spans.Count > 0)
							{
								string DefaultServiceName = TelemetryFile.GetFileNameWithoutAnyExtensions();
								foreach (TraceEvent Span in Telemetry.Spans)
								{
									Span.Service = Span.Service ?? DefaultServiceName;
								}
								TelemetryList.Add(Telemetry);
							}

							await ArtifactUploader.UploadAsync(RpcConnection, JobId, BatchId, Step.StepId, $"Telemetry/{TelemetryFile.GetFileName()}", TelemetryFile, Logger, CancellationToken.None);
							FileUtils.ForceDeleteFile(TelemetryFile);
						}

						List<TraceEvent> TelemetrySpans = new List<TraceEvent>();
						foreach (TraceEventList Telemetry in TelemetryList.OrderBy(x => x.Spans.First().StartTime).ThenBy(x => x.Spans.Last().FinishTime))
						{
							foreach (TraceEvent Span in Telemetry.Spans)
							{
								if (Span.FinishTime - Span.StartTime > TimeSpan.FromMilliseconds(1.0))
								{
									Span.Index = TelemetrySpans.Count;
									TelemetrySpans.Add(Span);
								}
							}
						}

						if (TelemetrySpans.Count > 0)
						{
							TraceSpan RootSpan = new TraceSpan();
							RootSpan.Name = Step.Name;

							Stack<TraceSpan> Stack = new Stack<TraceSpan>();
							Stack.Push(RootSpan);

							foreach (TraceEvent Event in TelemetrySpans.OrderBy(x => x.StartTime).ThenByDescending(x => x.FinishTime).ThenBy(x => x.Index))
							{
								TraceSpan NewSpan = new TraceSpan();
								NewSpan.Name = Event.Name;
								NewSpan.Service = Event.Service;
								NewSpan.Resource = Event.Resource;
								NewSpan.Start = Event.StartTime.UtcTicks;
								NewSpan.Finish = Event.FinishTime.UtcTicks;
								if (Event.Metadata != null && Event.Metadata.Count > 0)
								{
									NewSpan.Properties = Event.Metadata;
								}

								TraceSpan StackTop = Stack.Peek();
								while (Stack.Count > 1 && NewSpan.Start >= StackTop.Finish)
								{
									Stack.Pop();
									StackTop = Stack.Peek();
								}

								if (Stack.Count > 1 && NewSpan.Finish > StackTop.Finish)
								{
									Logger.LogInformation("Trace event name='{Name}', service'{Service}', resource='{Resource}' has invalid finish time ({SpanFinish} < {StackFinish})", NewSpan.Name, NewSpan.Service, NewSpan.Resource, NewSpan.Finish, StackTop.Finish);
									NewSpan.Finish = StackTop.Finish;
								}

								if (StackTop.Children == null)
								{
									StackTop.Children = new List<TraceSpan>();
								}

								StackTop.Children.Add(NewSpan);
								Stack.Push(NewSpan);
							}

							RootSpan.Start = RootSpan.Children.First().Start;
							RootSpan.Finish = RootSpan.Children.Last().Finish;

							FileReference TraceFile = FileReference.Combine(TelemetryDir, "Trace.json");
							using (FileStream Stream = FileReference.Open(TraceFile, FileMode.Create))
							{
								JsonSerializerOptions Options = new JsonSerializerOptions { IgnoreNullValues = true };
								await JsonSerializer.SerializeAsync(Stream, RootSpan, Options);
							}
							await ArtifactUploader.UploadAsync(RpcConnection, JobId, BatchId, Step.StepId, "Trace.json", TraceFile, Logger, CancellationToken.None);

							CreateTracingData(GlobalTracer.Instance.ActiveSpan, RootSpan);
						}
					}
					
					if (DirectoryReference.Exists(TestDataDir))
					{
						Dictionary<string, object> CombinedTestData = new Dictionary<string, object>();
						foreach (FileReference TestDataFile in DirectoryReference.EnumerateFiles(TestDataDir, "*.json", SearchOption.AllDirectories))
						{
							Logger.LogInformation("Reading test data {TestDataFile}", TestDataFile);
							await ArtifactUploader.UploadAsync(RpcConnection, JobId, BatchId, Step.StepId, $"TestData/{TestDataFile.MakeRelativeTo(TestDataDir)}", TestDataFile, Logger, CancellationToken.None);

							TestData TestData;
							using (FileStream Stream = FileReference.Open(TestDataFile, FileMode.Open))
							{
								JsonSerializerOptions Options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
								TestData = await JsonSerializer.DeserializeAsync<TestData>(Stream, Options);
							}

							foreach (TestDataItem Item in TestData.Items)
							{
								if (CombinedTestData.ContainsKey(Item.Key))
								{
									Logger.LogWarning("Key '{Key}' already exists - ignoring", Item.Key);
								}
								else
								{
									Logger.LogDebug("Adding data with key '{Key}'", Item.Key);
									CombinedTestData.Add(Item.Key, Item.Data);
								}
							}
						}

						Logger.LogInformation("Found {NumResults} test results", CombinedTestData.Count);
						await UploadTestDataAsync(Step.StepId, CombinedTestData);
					}
					
					if (DirectoryReference.Exists(LogDir))
					{
						Dictionary<FileReference, string> ArtifactFileToId = new Dictionary<FileReference, string>();
						foreach (FileReference ArtifactFile in DirectoryReference.EnumerateFiles(LogDir, "*", SearchOption.AllDirectories))
						{
							string ArtifactName = ArtifactFile.MakeRelativeTo(LogDir);

							string? ArtifactId = await ArtifactUploader.UploadAsync(RpcConnection, JobId, BatchId, Step.StepId, ArtifactName, ArtifactFile, Logger, CancellationToken);
							if (ArtifactId != null)
							{
								ArtifactFileToId[ArtifactFile] = ArtifactId;
							}
						}

						foreach (FileReference ReportFile in ArtifactFileToId.Keys.Where(x => x.HasExtension(".report.json")))
						{
							try
							{
								await CreateReportAsync(Step.StepId, ReportFile, ArtifactFileToId, Logger);
							}
							catch(Exception Ex)
							{
								Logger.LogWarning("Unable to upload report: {Message}", Ex.Message);
							}
						}
					}

					return Process.ExitCode;
				}
			}
		}

		private async Task CreateReportAsync(string StepId, FileReference ReportFile, Dictionary<FileReference, string> ArtifactFileToId, ILogger Logger)
		{
			byte[] Data = await FileReference.ReadAllBytesAsync(ReportFile);

			JsonSerializerOptions Options = new JsonSerializerOptions();
			Options.PropertyNameCaseInsensitive = true;
			Options.Converters.Add(new JsonStringEnumConverter());
			ReportData Report = JsonSerializer.Deserialize<ReportData>(Data, Options);

			if (String.IsNullOrEmpty(Report.Name))
			{
				Logger.LogWarning("Missing 'Name' field in report data");
				return;
			}
			if (String.IsNullOrEmpty(Report.FileName))
			{
				Logger.LogWarning("Missing 'FileName' field in report data");
				return;
			}

			FileReference ArtifactFile = FileReference.Combine(ReportFile.Directory, Report.FileName);
			if (!ArtifactFileToId.TryGetValue(ArtifactFile, out string? ArtifactId))
			{
				Logger.LogWarning("Unable to find artifact id for {File}", ArtifactFile);
				return;
			}

			Logger.LogInformation("Creating report for {File} using artifact {ArtifactId}", ReportFile, ArtifactId);

			CreateReportRequest Request = new CreateReportRequest();
			Request.JobId = JobId;
			Request.BatchId = BatchId;
			Request.StepId = StepId;
			Request.Scope = Report.Scope;
			Request.Placement = Report.Placement;
			Request.Name = Report.Name;
			Request.ArtifactId = ArtifactId;
			await RpcConnection.InvokeAsync(x => x.CreateReportAsync(Request), new RpcContext(), CancellationToken.None);
		}

		private ISpan CreateTracingData(ISpan Parent, TraceSpan Span)
		{
			ISpan NewSpan = GlobalTracer.Instance.BuildSpan(Span.Name)
				.AsChildOf(Parent)
				.WithServiceName(Span.Service)
				.WithResourceName(Span.Resource)
				.WithStartTimestamp(new DateTime(Span.Start, DateTimeKind.Utc))
				.Start();

			if (Span.Properties != null)
			{
				foreach (KeyValuePair<string, string> Pair in Span.Properties)
				{
					NewSpan.SetTag(Pair.Key, Pair.Value);
				}
			}
			if (Span.Children != null)
			{
				foreach (TraceSpan Child in Span.Children)
				{
					CreateTracingData(NewSpan, Child);
				}
			}

			NewSpan.Finish(new DateTime(Span.Finish, DateTimeKind.Utc));
			return NewSpan;
		}

		protected async Task UploadTestDataAsync(string JobStepId, IEnumerable<KeyValuePair<string, object>> TestData)
		{
			if (TestData.Any())
			{
				await RpcConnection.InvokeAsync(x => UploadTestDataAsync(x, JobStepId, TestData), new RpcContext(), CancellationToken.None);
			}
		}

		async Task<bool> UploadTestDataAsync(HordeRpc.HordeRpcClient RpcClient, string JobStepId, IEnumerable<KeyValuePair<string, object>> Pairs)
		{
			using (AsyncClientStreamingCall<UploadTestDataRequest, UploadTestDataResponse> Call = RpcClient.UploadTestData())
			{
				foreach (KeyValuePair<string, object> Pair in Pairs)
				{
					JsonSerializerOptions Options = new JsonSerializerOptions();
					Options.PropertyNameCaseInsensitive = true;
					Options.Converters.Add(new JsonStringEnumConverter());
					byte[] Data = JsonSerializer.SerializeToUtf8Bytes(Pair.Value, Options);

					UploadTestDataRequest Request = new UploadTestDataRequest();
					Request.JobId = JobId;
					Request.JobStepId = JobStepId;
					Request.Key = Pair.Key;
					Request.Value = Google.Protobuf.ByteString.CopyFrom(Data);
					await Call.RequestStream.WriteAsync(Request);
				}
				await Call.RequestStream.CompleteAsync();
				await Call.ResponseAsync;
			}
			return true;
		}
	}
}
