// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using HordeAgent.Execution.Interfaces;
using HordeAgent.Parser;
using HordeAgent.Parser.Interfaces;
using HordeAgent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;

namespace HordeAgent.Execution
{
	class TestExecutor : BuildGraphExecutor
	{
		public TestExecutor(IRpcConnection RpcClient, string JobId, string BatchId, string AgentTypeName)
			: base(RpcClient, JobId, BatchId, AgentTypeName)
		{
		}

		public override Task InitializeAsync(ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogInformation("Initializing");
			return Task.CompletedTask;
		}

		protected override async Task<bool> SetupAsync(BeginStepResponse Step, ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogInformation("**** BEGIN JOB SETUP ****");

			await Task.Delay(5000, CancellationToken);

			UpdateGraphRequest UpdateGraph = new UpdateGraphRequest();
			UpdateGraph.JobId = JobId;

			CreateGroupRequest WinEditorGroup = CreateGroup("Win64");
			WinEditorGroup.Nodes.Add(CreateNode("Update Version Files", new string[] { }, JobStepOutcome.Success));
			WinEditorGroup.Nodes.Add(CreateNode("Compile UnrealHeaderTool Win64", new string[] { "Update Version Files" }, JobStepOutcome.Success));
			WinEditorGroup.Nodes.Add(CreateNode("Compile UE4Editor Win64", new string[] { "Compile UnrealHeaderTool Win64" }, JobStepOutcome.Success));
			WinEditorGroup.Nodes.Add(CreateNode("Compile FortniteEditor Win64", new string[] { "Compile UnrealHeaderTool Win64", "Compile UE4Editor Win64" }, JobStepOutcome.Success));
			UpdateGraph.Groups.Add(WinEditorGroup);

			CreateGroupRequest WinToolsGroup = CreateGroup("Win64"); 
			WinToolsGroup.Nodes.Add(CreateNode("Compile Tools Win64", new string[] { "Compile UnrealHeaderTool Win64" }, JobStepOutcome.Warnings));
			UpdateGraph.Groups.Add(WinToolsGroup);

			CreateGroupRequest WinClientsGroup = CreateGroup("Win64");
			WinClientsGroup.Nodes.Add(CreateNode("Compile FortniteClient Win64", new string[] { "Compile UnrealHeaderTool Win64" }, JobStepOutcome.Success));
			UpdateGraph.Groups.Add(WinClientsGroup);

			CreateGroupRequest WinCooksGroup = CreateGroup("Win64");
			WinCooksGroup.Nodes.Add(CreateNode("Cook FortniteClient Win64", new string[] { "Compile FortniteEditor Win64", "Compile Tools Win64" }, JobStepOutcome.Warnings));
			WinCooksGroup.Nodes.Add(CreateNode("Stage FortniteClient Win64", new string[] { "Cook FortniteClient Win64", "Compile Tools Win64" }, JobStepOutcome.Success));
			WinCooksGroup.Nodes.Add(CreateNode("Publish FortniteClient Win64", new string[] { "Stage FortniteClient Win64" }, JobStepOutcome.Success));
			UpdateGraph.Groups.Add(WinCooksGroup);

			Dictionary<string, string[]> DependencyMap = CreateDependencyMap(UpdateGraph.Groups);
			UpdateGraph.Labels.Add(CreateLabel("Editors", "UE4", new string[] { "Compile UE4Editor Win64" }, Array.Empty<string>(), DependencyMap));
			UpdateGraph.Labels.Add(CreateLabel("Editors", "Fortnite", new string[] { "Compile FortniteEditor Win64" }, Array.Empty<string>(), DependencyMap));
			UpdateGraph.Labels.Add(CreateLabel("Clients", "Fortnite", new string[] { "Cook FortniteClient Win64" }, new string[] { "Publish FortniteClient Win64" }, DependencyMap));

			await RpcConnection.InvokeAsync(x => x.UpdateGraphAsync(UpdateGraph, null, null, CancellationToken), new RpcContext(), CancellationToken);

			Logger.LogInformation("**** FINISH JOB SETUP ****");
			return true;
		}

		static CreateGroupRequest CreateGroup(string AgentType)
		{
			CreateGroupRequest Request = new CreateGroupRequest();
			Request.AgentType = AgentType;
			return Request;
		}

		static CreateNodeRequest CreateNode(string Name, string[] InputDependencies, JobStepOutcome Outcome)
		{
			CreateNodeRequest Request = new CreateNodeRequest();
			Request.Name = Name;
			Request.InputDependencies.AddRange(InputDependencies);
			Request.Properties.Add("Action", "Build");
			Request.Properties.Add("Outcome", Outcome.ToString());
			return Request;
		}

		static CreateLabelRequest CreateLabel(string Category, string Name, string[] RequiredNodes, string[] IncludedNodes, Dictionary<string, string[]> DependencyMap)
		{
			CreateLabelRequest Request = new CreateLabelRequest();
			Request.DashboardName = Name;
			Request.DashboardCategory = Category;
			Request.RequiredNodes.AddRange(RequiredNodes);
			Request.IncludedNodes.AddRange(Enumerable.Union(RequiredNodes, IncludedNodes).SelectMany(x => DependencyMap[x]).Distinct());
			return Request;
		}

		static Dictionary<string, string[]> CreateDependencyMap(IEnumerable<CreateGroupRequest> Groups)
		{
			Dictionary<string, string[]> NameToDependencyNames = new Dictionary<string, string[]>();
			foreach (CreateGroupRequest Group in Groups)
			{
				foreach (CreateNodeRequest Node in Group.Nodes)
				{
					HashSet<string> DependencyNames = new HashSet<string> { Node.Name };

					foreach (string InputDependency in Node.InputDependencies)
					{
						DependencyNames.UnionWith(NameToDependencyNames[InputDependency]);
					}
					foreach (string OrderDependency in Node.OrderDependencies)
					{
						DependencyNames.UnionWith(NameToDependencyNames[OrderDependency]);
					}

					NameToDependencyNames[Node.Name] = DependencyNames.ToArray();
				}
			}
			return NameToDependencyNames;
		}

		protected override async Task<bool> ExecuteAsync(BeginStepResponse Step, ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogInformation("**** BEGIN NODE {StepName} ****", Step.Name);

			await Task.Delay(TimeSpan.FromSeconds(5.0), CancellationToken);
			CancellationToken.ThrowIfCancellationRequested();

			JobStepOutcome Outcome = Enum.Parse<JobStepOutcome>(Step.Properties["Outcome"]);

			Dictionary<string, object> Items = new Dictionary<string, object>();
			Items["hello"] = new { prop = 12345, prop2 = "world" };
			Items["world"] = new { prop = 123 };
			await UploadTestDataAsync(Step.StepId, Items);

			if (Step.Name == "Stage FortniteClient Win64")
			{
				Outcome = JobStepOutcome.Failure;
			}

			foreach (KeyValuePair<string, string> Credential in Step.Credentials)
			{
				Logger.LogInformation("Credential: {CredentialName}={CredentialValue}", Credential.Key, Credential.Value);
			}

			LogParserContext Context = new LogParserContext();
			Context.WorkspaceDir = new DirectoryReference("D:\\Test");
			Context.PerforceStream = "//UE4/Main";
			Context.PerforceChange = 12345;

			using (LogParser Filter = new LogParser(Logger, Context, new List<string>()))
			{
				if(Outcome == JobStepOutcome.Warnings)
				{
					Filter.WriteLine("D:\\Test\\Path\\To\\Source\\File.cpp(234): warning: This is a compilation warning");
					Logger.LogWarning("This is a warning!");
					Filter.WriteLine("warning: this is a test");
				}
				if(Outcome == JobStepOutcome.Failure)
				{
					Filter.WriteLine("D:\\Test\\Path\\To\\Source\\File.cpp(234): error: This is a compilation error");
					Logger.LogError("This is an error!");
					Filter.WriteLine("error: this is a test");
				}
			}

			FileReference CurrentFile = new FileReference(Assembly.GetExecutingAssembly().Location);
			await ArtifactUploader.UploadAsync(RpcConnection, JobId, BatchId, Step.StepId, CurrentFile.GetFileName(), CurrentFile, Logger, CancellationToken);

			Logger.LogInformation("**** FINISH NODE {StepName} ****", Step.Name);

			return true;
		}

		public override Task FinalizeAsync(ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogInformation("Finalizing");
			return Task.CompletedTask;
		}
	}
}
