// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Grpc.Net.Client;
using HordeAgent;
using HordeAgent.Execution.Interfaces;
using HordeAgent.Services;
using HordeAgent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using HordeCommon.Rpc.Messages;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Console;
using Microsoft.Extensions.Options;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Directory = System.IO.Directory;

namespace HordeAgentTests
{
	[TestClass]
	public class TaskTest
	{
		private readonly ILogger<WorkerService> WorkerLogger;
		private readonly ILogger<GrpcService> GrpcServiceLogger;
		private readonly string TempDir;
		private readonly AgentSettings Settings = new AgentSettings();
		private readonly GrpcService GrpcService;
		private readonly FakeCasClient Cas;
		private readonly FakeActionRpcClient ActionRpc;
		private readonly RpcConnectionStub RpcConnection;
		private readonly WorkerService Ws;
		private readonly string InstanceName = "testing-instance";

		public TaskTest()
		{
			LoggerFactory LoggerFactory = new LoggerFactory();
			ConsoleLoggerOptions LoggerOptions = new ConsoleLoggerOptions();
			TestOptionsMonitor<ConsoleLoggerOptions> LoggerOptionsMon = new TestOptionsMonitor<ConsoleLoggerOptions>(LoggerOptions);
			LoggerFactory.AddProvider(new ConsoleLoggerProvider(LoggerOptionsMon));
			WorkerLogger = LoggerFactory.CreateLogger<WorkerService>();
			GrpcServiceLogger = LoggerFactory.CreateLogger<GrpcService>();
			
			TempDir = ActionExecutorTest.GetTemporaryDirectory();
			
			ServerProfile Profile = new ServerProfile();
			Profile.Name = "test";
			Profile.Environment = "test-env";
			Profile.Token = "bogus-token";
			Profile.Url = "http://localhost";
			Settings.ServerProfiles.Add(Profile);
			Settings.Server = "test";
			Settings.WorkingDir = Path.GetTempPath();
			Settings.Executor = ExecutorType.Test; // Not really used since the executor is overridden in the tests
			
			GrpcService = new GrpcService(new OptionsWrapper<AgentSettings>(Settings), GrpcServiceLogger);
			Cas = new FakeCasClient();
			ActionRpc = new FakeActionRpcClient();
			GrpcService.CasClientFactory = Channel => Cas;
			GrpcService.ActionRpcClientFactory = Channel => ActionRpc;
			
			HordeRpcClientStub HordeRpcClient = new HordeRpcClientStub(WorkerLogger);
			RpcConnection = new RpcConnectionStub(GrpcChannel.ForAddress("http://bogus-test-url"), HordeRpcClient);
			Ws = GetWorkerService();
		}
		
		[TestCleanup]
		public void Cleanup()
		{
			Directory.Delete(TempDir, true);
		}

		private WorkerService GetWorkerService(
			Func<IRpcConnection, ExecuteJobTask, BeginBatchResponse, IExecutor>? CreateExecutor = null)
		{
			IOptionsMonitor<AgentSettings> SettingsMonitor = new TestOptionsMonitor<AgentSettings>(Settings);
			return new WorkerService(WorkerLogger, SettingsMonitor, GrpcService, CreateExecutor);
		}
	
		[TestMethod]
		public async Task ActionTask()
		{
			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) return;
			
			ActionTask ActionTask = CreateActionTask(InstanceName, Cas);
			Assert.IsNull(ActionRpc.ActionResultRequest);
			LeaseOutcome Outcome = (await Ws.HandleLeasePayloadAsync(RpcConnection, "my-agent-id", CreateLeaseInfo(ActionTask))).Outcome;
			Assert.AreEqual(LeaseOutcome.Success, Outcome);
			Assert.IsNotNull(ActionRpc.ActionResultRequest);
			Assert.IsNull(ActionRpc.ActionResultRequest!.Error);
		}
		
		[TestMethod]
		[Ignore]
		public async Task ActionTaskMissingAction()
		{
			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) return;
			
			ActionTask ActionTask = CreateActionTask(InstanceName, Cas);
			LeaseOutcome Outcome = (await Ws.HandleLeasePayloadAsync(RpcConnection, "my-agent-id", CreateLeaseInfo(ActionTask))).Outcome;
			Assert.AreEqual(LeaseOutcome.Failed, Outcome);
			Assert.IsNotNull(ActionRpc.ActionResultRequest);
			Assert.IsNull(ActionRpc.ActionResultRequest!.Error);
		}
		
		private static string GetHash(byte[] data)
		{
			using System.Security.Cryptography.MD5 md5 = System.Security.Cryptography.MD5.Create();
			byte[] hashBytes = md5.ComputeHash(data);
			StringBuilder sb = new StringBuilder();
			foreach (byte b in hashBytes)
			{
				sb.Append(b.ToString("X2"));
			}
			return sb.ToString();
		}

		private static Build.Bazel.Remote.Execution.V2.Digest GetDigest(byte[] data)
		{
			return new Build.Bazel.Remote.Execution.V2.Digest {Hash = GetHash(data), SizeBytes = data.Length};
		}

		/// <summary>
		/// Create an action task with the remote exec test binary
		/// Will populate the CAS as well. Flags for uploading allow tests to verify different failure scenarios.
		/// </summary>
		/// <param name="InstanceName">Instance name</param>
		/// <param name="Cas">The content-addressable store being used</param>
		/// <param name="Timeout">Timeout before killing the command</param>
		/// <param name="UploadBin">True to upload the remote exec test binary to CAS</param>
		/// <param name="UploadDir">True to upload directory to CAS</param>
		/// <param name="UploadCommand">True to upload the command to CAS</param>
		/// <param name="UploadAction">True to upload action to CAS</param>
		/// <returns>An ActionTask populated in CAS</returns>
		public static ActionTask CreateActionTask(string InstanceName, FakeCasClient Cas, TimeSpan? CommandSleep = null, int CommandExitCode = 0, TimeSpan? Timeout = null,
			bool UploadBin = true, bool UploadDir = true, bool UploadCommand = true, bool UploadAction = true)
		{
			string BinName = "remote-exec-test-bin.exe";
			string AssemblyPath = Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location)!;
			string BinPath = Path.Combine(AssemblyPath, "remote-exec-test-bin", BinName);

			byte[] ExeFileData = File.ReadAllBytes(BinPath);
			if (UploadBin)
			{
				Cas.SetBlob(InstanceName, GetHash(ExeFileData), ByteString.CopyFrom(ExeFileData));
			}

			Build.Bazel.Remote.Execution.V2.FileNode ExeFile = new Build.Bazel.Remote.Execution.V2.FileNode();
			ExeFile.Digest = GetDigest(ExeFileData);
			ExeFile.Name = BinName;
			ExeFile.IsExecutable = true;
			
			Build.Bazel.Remote.Execution.V2.Directory Dir = new Build.Bazel.Remote.Execution.V2.Directory();
			Dir.Files.Add(ExeFile);
			byte[] DirData = Dir.ToByteArray();
			if (UploadDir)
			{
				Cas.SetBlob(InstanceName, GetHash(DirData), ByteString.CopyFrom(DirData));
			}

			Build.Bazel.Remote.Execution.V2.Command Command = new Build.Bazel.Remote.Execution.V2.Command();
			Command.Arguments.Add(ExeFile.Name);

			int CommandSleepMs = 0;
			if (CommandSleep != null)
			{
				CommandSleepMs = (int) CommandSleep.Value.TotalMilliseconds;
			}
			
			Command.Arguments.Add(CommandSleepMs.ToString());
			Command.Arguments.Add(CommandExitCode.ToString());
			
			byte[] CommandData = Command.ToByteArray();
			if (UploadCommand)
			{
				Cas.SetBlob(InstanceName, GetHash(CommandData), ByteString.CopyFrom(CommandData));
			}

			Build.Bazel.Remote.Execution.V2.Action Action = new Build.Bazel.Remote.Execution.V2.Action();
			Action.CommandDigest = GetDigest(CommandData);
			Action.InputRootDigest = GetDigest(DirData);
			Action.DoNotCache = true;

			if (Timeout != null)
			{
				Action.Timeout = Duration.FromTimeSpan(Timeout.Value);	
			}
			
			byte[] ActionData = Action.ToByteArray();
			if (UploadAction)
			{
				Cas.SetBlob(InstanceName, GetHash(ActionData), ByteString.CopyFrom(ActionData));
			}

			ActionTask ActionTask = new ActionTask();
			ActionTask.Digest = GetDigest(ActionData);
			ActionTask.LogId = "bogus-log-id";
			ActionTask.CasUrl = "http://bogus-cas-url";
			ActionTask.ActionCacheUrl = "http://bogus-actioncache-url";
			ActionTask.InstanceName = InstanceName;
			return ActionTask;
		}
		
		private WorkerService.LeaseInfo CreateLeaseInfo(IMessage Payload)
		{
			Lease Lease = new Lease();
			Lease.Id = "my-lease-id";
			Lease.Payload = Any.Pack(Payload);
			Lease.State = LeaseState.Active;
			Lease.Outcome = LeaseOutcome.Unspecified;
			return new WorkerService.LeaseInfo(Lease);
		}
	}
}