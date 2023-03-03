// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Horde.Build.Server;
using Horde.Build.Storage;
using Horde.Build.Tools;
using HordeCommon;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Build.Tests
{
	[TestClass]
    public class ToolTests : TestSetup
    {
		ToolId _toolId = new ToolId("ugs");

		public ToolTests()
		{
			GlobalConfig globalConfig = new GlobalConfig();
			globalConfig.Storage.Backends.Clear();
			globalConfig.Storage.Backends.Add(new BackendConfig { Id = new BackendId("tools-backend"), Type = StorageBackendType.Memory });
			globalConfig.Storage.Namespaces.Clear();
			globalConfig.Storage.Namespaces.Add(new NamespaceConfig { Id = Namespace.Tools, Backend = new BackendId("tools-backend") });
			globalConfig.Tools.Add(new ToolConfig(_toolId) { Name = "UnrealGameSync", Description = "Tool for syncing content from source control" });
			SetConfig(globalConfig);
		}

        [TestMethod]
        public async Task AddTool()
        {
			ToolCollection collection = ServiceProvider.GetRequiredService<ToolCollection>();

			ITool tool = Deref(await collection.GetAsync(_toolId, GlobalConfig.CurrentValue));
			Assert.AreEqual(tool.Id, new ToolId("ugs"));
			Assert.AreEqual(tool.Config.Name, "UnrealGameSync");
			Assert.AreEqual(tool.Config.Description, "Tool for syncing content from source control");

			ITool tool2 = Deref(await collection.GetAsync(tool.Id, GlobalConfig.CurrentValue));
			Assert.AreEqual(tool.Id, tool2.Id);
			Assert.AreEqual(tool.Config.Name, tool2.Config.Name);
			Assert.AreEqual(tool.Config.Description, tool2.Config.Description);
        }

		[TestMethod]
		public async Task AddDeployment()
		{
			ToolCollection collection = ServiceProvider.GetRequiredService<ToolCollection>();

			ITool tool = Deref(await collection.GetAsync(_toolId, GlobalConfig.CurrentValue));
			Assert.AreEqual(new ToolId("ugs"), tool.Id);
			Assert.AreEqual("UnrealGameSync", tool.Config.Name);
			Assert.AreEqual("Tool for syncing content from source control", tool.Config.Description);
			Assert.AreEqual(0, tool.Deployments.Count);

			byte[] deploymentData = Encoding.UTF8.GetBytes("hello world");

			ToolDeploymentId deploymentId;
			using (MemoryStream stream = new MemoryStream(deploymentData))
			{
				tool = Deref(await collection.CreateDeploymentAsync(tool, new ToolDeploymentConfig { Version = "1.0", Duration = TimeSpan.FromMinutes(5.0), CreatePaused = true }, stream, GlobalConfig.CurrentValue, CancellationToken.None));
				Assert.AreEqual(1, tool.Deployments.Count);
				Assert.IsNull(tool.Deployments[0].StartedAt);
				deploymentId = tool.Deployments[^1].Id;
			}

			// Check that the deployment doesn't do anything until started
			FakeClock clock = ServiceProvider.GetRequiredService<FakeClock>();
			await clock.AdvanceAsync(TimeSpan.FromHours(1.0));

			tool = Deref(await collection.GetAsync(tool.Id, GlobalConfig.CurrentValue));
			Assert.AreEqual(1, tool.Deployments.Count);
			Assert.IsNull(tool.Deployments[0].StartedAt);

			// Start the deployment
			tool = Deref(await collection.UpdateDeploymentAsync(tool, deploymentId, ToolDeploymentState.Active));
			Assert.AreEqual(1, tool.Deployments.Count);
			Assert.IsNotNull(tool.Deployments[0].StartedAt);
			Assert.IsTrue(Math.Abs((tool.Deployments[0].StartedAt!.Value - clock.UtcNow).TotalSeconds) < 1.0);

			// Check it updates
			await clock.AdvanceAsync(TimeSpan.FromMinutes(2.5));
			tool = Deref(await collection.UpdateDeploymentAsync(tool, deploymentId, ToolDeploymentState.Paused));
			Assert.AreEqual(1, tool.Deployments.Count);
			Assert.IsNull(tool.Deployments[0].StartedAt);
			Assert.IsTrue((tool.Deployments[0].Progress - 0.5) < 0.1);

			// Check it stays paused
			await clock.AdvanceAsync(TimeSpan.FromHours(1.0));
			tool = Deref(await collection.GetAsync(tool.Id, GlobalConfig.CurrentValue));
			Assert.AreEqual(1, tool.Deployments.Count);
			Assert.IsNull(tool.Deployments[0].StartedAt);
			Assert.IsTrue((tool.Deployments[0].Progress - 0.5) < 0.1);

			// Get the deployment data
			byte[] outputDeploymentData;
			using (MemoryStream stream = new MemoryStream())
			{
				Stream dataStream = await collection.GetDeploymentPayloadAsync(tool, tool.Deployments[0], CancellationToken.None);
				await dataStream.CopyToAsync(stream);
				outputDeploymentData = stream.ToArray();
			}
			Assert.IsTrue(deploymentData.AsSpan().SequenceEqual(outputDeploymentData));
		}
	}
}