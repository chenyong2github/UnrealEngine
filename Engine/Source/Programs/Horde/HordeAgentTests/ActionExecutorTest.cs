// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Common.RemoteExecution;
using Grpc.Core;
using HordeAgent;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Console;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Directory = System.IO.Directory;

namespace HordeAgentTests
{
	[TestClass]
	public class ActionExecutorTest
	{
		private readonly string TempDir;
		private readonly ILogger Logger = CreateLogger();
		private readonly string AgentName = "myAgentName";
		private readonly string InstanceName = "myInstanceName";
		private readonly FakeCasClient CasClient = new FakeCasClient();
		private readonly FakeActionRpcClient ActionRpcClient = new FakeActionRpcClient();


		public ActionExecutorTest()
		{
			TempDir = GetTemporaryDirectory();
		}

		[TestMethod]
		public void TestPaths()
		{
			string SubDirPath = Path.Join(TempDir, "1");
			string SubSubDirPath = Path.Join(SubDirPath, "2");
			string SubSubSubDirPath = Path.Join(SubSubDirPath, "3");
			Directory.CreateDirectory(SubSubSubDirPath);
			File.WriteAllText(Path.Join(TempDir, "foo"), "a");
			File.WriteAllText(Path.Join(SubDirPath, "bar"), "b");
			File.WriteAllText(Path.Join(SubSubDirPath, "baz"), "b");
			File.WriteAllText(Path.Join(SubSubDirPath, "qux"), "c");
			File.WriteAllText(Path.Join(SubSubSubDirPath, "waldo"), "d");

			string[] OutputPaths =
			{
				"foo",
				"1/bar",
				"1/2", // directory
				"doesNotExist",
				//"foo", // duplicated (to be fixed)
			};

			DirectoryReference SandboxDir = new DirectoryReference(TempDir);
			var Resolved = ActionExecutor.ResolveOutputPaths(SandboxDir, OutputPaths);
			Assert.AreEqual(Path.Join(TempDir, "foo"), Resolved[0].FileRef.FullName);
			Assert.AreEqual("foo", Resolved[0].RelativePath);
			
			Assert.AreEqual(Path.Join(TempDir, "1", "bar"), Resolved[1].FileRef.FullName);
			Assert.AreEqual("1/bar", Resolved[1].RelativePath);
			Assert.AreEqual(Path.Join(TempDir, "1", "2", "baz"), Resolved[2].FileRef.FullName);
			Assert.AreEqual("1/2/baz", Resolved[2].RelativePath);
			Assert.AreEqual(Path.Join(TempDir, "1", "2", "qux"), Resolved[3].FileRef.FullName);
			Assert.AreEqual("1/2/qux", Resolved[3].RelativePath);
			Assert.AreEqual(Path.Join(TempDir, "1", "2", "3", "waldo"), Resolved[4].FileRef.FullName);
			Assert.AreEqual("1/2/3/waldo", Resolved[4].RelativePath);
		}

		[TestMethod]
		public async Task ExecuteAction()
		{
			ActionExecutor Executor = new ActionExecutor(AgentName, InstanceName, CasClient, null!, ActionRpcClient, Logger);
			ActionTask ActionTask = TaskTest.CreateActionTask(InstanceName, CasClient);
			await Executor.ExecuteActionAsync("myLeaseId1", ActionTask, new DirectoryReference(TempDir), DateTimeOffset.UtcNow, CancellationToken.None);
		}

		[TestMethod]
		public async Task ExecuteActionMissingBinary()
		{
			ActionExecutor Executor = new ActionExecutor(AgentName, InstanceName, CasClient, null!, ActionRpcClient, Logger);
			ActionTask ActionTask = TaskTest.CreateActionTask(InstanceName, CasClient, UploadBin: false);
			await Assert.ThrowsExceptionAsync<DigestNotFoundException>(() => Executor.ExecuteActionAsync("myLeaseId1", ActionTask,
				new DirectoryReference(TempDir), DateTimeOffset.UtcNow, CancellationToken.None));
		}
		
		[TestMethod]
		public async Task ExecuteActionMissingDirectory()
		{
			ActionExecutor Executor = new ActionExecutor(AgentName, InstanceName, CasClient, null!, ActionRpcClient, Logger);
			ActionTask ActionTask = TaskTest.CreateActionTask(InstanceName, CasClient, UploadDir: false);
			await Assert.ThrowsExceptionAsync<DigestNotFoundException>(() => Executor.ExecuteActionAsync("myLeaseId1", ActionTask,
				new DirectoryReference(TempDir), DateTimeOffset.UtcNow, CancellationToken.None));
		}
		
		[TestMethod]
		public async Task ExecuteActionMissingAction()
		{
			ActionExecutor Executor = new ActionExecutor(AgentName, InstanceName, CasClient, null!, ActionRpcClient, Logger);
			ActionTask ActionTask = TaskTest.CreateActionTask(InstanceName, CasClient, UploadAction: false);
			await Assert.ThrowsExceptionAsync<DigestNotFoundException>(() => Executor.ExecuteActionAsync("myLeaseId1", ActionTask,
				new DirectoryReference(TempDir), DateTimeOffset.UtcNow, CancellationToken.None));
		}
		
		[TestMethod]
		public async Task ExecuteActionMissingCommand()
		{
			ActionExecutor Executor = new ActionExecutor(AgentName, InstanceName, CasClient, null!, ActionRpcClient, Logger);
			ActionTask ActionTask = TaskTest.CreateActionTask(InstanceName, CasClient, UploadCommand: false);
			await Assert.ThrowsExceptionAsync<DigestNotFoundException>(() => Executor.ExecuteActionAsync("myLeaseId1", ActionTask,
				new DirectoryReference(TempDir), DateTimeOffset.UtcNow, CancellationToken.None));
		}
		
		
		[TestMethod]
		public async Task ExecuteActionWithinTimeout()
		{
			ActionExecutor Executor = new ActionExecutor(AgentName, InstanceName, CasClient, null!, ActionRpcClient, Logger);
			ActionTask ActionTask1 = TaskTest.CreateActionTask(InstanceName, CasClient, CommandSleep: TimeSpan.FromMilliseconds(200), Timeout: TimeSpan.FromMilliseconds(1500));
			await Executor.ExecuteActionAsync("myLeaseId1", ActionTask1, new DirectoryReference(TempDir),
				DateTimeOffset.UtcNow, CancellationToken.None);
		}
		
		[TestMethod]
		public async Task ExecuteActionCancel()
		{
			ActionExecutor Executor = new ActionExecutor(AgentName, InstanceName, CasClient, null!, ActionRpcClient, Logger);
			ActionTask ActionTask = TaskTest.CreateActionTask(InstanceName, CasClient, CommandSleep: TimeSpan.FromMilliseconds(15000));
			CancellationTokenSource Cts = new CancellationTokenSource();
			Cts.CancelAfter(2000);
			RpcException Ex = await Assert.ThrowsExceptionAsync<RpcException>(() => Executor.ExecuteActionAsync("myLeaseId1", ActionTask, new DirectoryReference(TempDir), DateTimeOffset.UtcNow, Cts.Token));
			Assert.AreEqual(StatusCode.Cancelled, Ex.Status.StatusCode);
		}
		
		[TestMethod]
		public async Task ExecuteActionExceedingTimeout()
		{
			ActionExecutor Executor = new ActionExecutor(AgentName, InstanceName, CasClient, null!, ActionRpcClient, Logger);
			ActionTask ActionTask = TaskTest.CreateActionTask(InstanceName, CasClient, CommandSleep: TimeSpan.FromMilliseconds(15000), Timeout: TimeSpan.FromMilliseconds(200));
			RpcException Ex = await Assert.ThrowsExceptionAsync<RpcException>(() => Executor.ExecuteActionAsync("myLeaseId1", ActionTask, new DirectoryReference(TempDir), DateTimeOffset.UtcNow, CancellationToken.None));
			Assert.AreEqual(StatusCode.DeadlineExceeded, Ex.Status.StatusCode);
		}
		
		private static ILogger CreateLogger()
		{
			LoggerFactory LoggerFactory = new LoggerFactory();
			ConsoleLoggerOptions LoggerOptions = new ConsoleLoggerOptions();
			TestOptionsMonitor<ConsoleLoggerOptions> LoggerOptionsMon = new TestOptionsMonitor<ConsoleLoggerOptions>(LoggerOptions);
			LoggerFactory.AddProvider(new ConsoleLoggerProvider(LoggerOptionsMon));
			return LoggerFactory.CreateLogger<ActionExecutor>();
		}

		[TestCleanup]
		public void Cleanup()
		{
			Directory.Delete(TempDir, true);
		}
		
		public static string GetTemporaryDirectory()
		{
			string TempDir = Path.Join(Path.GetTempPath(), "horde-" + Path.GetRandomFileName());
			if (Directory.Exists(TempDir))
			{
				Directory.Delete(TempDir, true);
			}

			Directory.CreateDirectory(TempDir);

			return TempDir;
		}
	}
}