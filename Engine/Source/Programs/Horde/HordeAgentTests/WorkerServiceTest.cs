// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
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

namespace HordeAgentTests
{
	[TestClass]
	public class WorkerServiceTest
	{
		private readonly ILogger<WorkerService> WorkerLogger;

		public WorkerServiceTest()
		{
			LoggerFactory LoggerFactory = new LoggerFactory();
			ConsoleLoggerOptions LoggerOptions = new ConsoleLoggerOptions();
			TestOptionsMonitor<ConsoleLoggerOptions> LoggerOptionsMon =
				new TestOptionsMonitor<ConsoleLoggerOptions>(LoggerOptions);
			LoggerFactory.AddProvider(new ConsoleLoggerProvider(LoggerOptionsMon));
			WorkerLogger = LoggerFactory.CreateLogger<WorkerService>();
		}

		private WorkerService GetWorkerService(
			Func<IRpcConnection, ExecuteJobTask, BeginBatchResponse, IExecutor>? CreateExecutor = null)
		{
			AgentSettings Settings = new AgentSettings();
			ServerProfile Profile = new ServerProfile();
			Profile.Name = "test";
			Profile.Environment = "test-env";
			Profile.Token = "bogus-token";
			Profile.Url = "http://localhost";
			Settings.ServerProfiles.Add(Profile);
			Settings.Server = "test";
			Settings.WorkingDir = Path.GetTempPath();
			Settings.Executor = ExecutorType.Test; // Not really used since the executor is overridden in the tests
			IOptionsMonitor<AgentSettings> SettingsMonitor = new TestOptionsMonitor<AgentSettings>(Settings);

			return new WorkerService(WorkerLogger, SettingsMonitor, null!, CreateExecutor);
		}

		[TestMethod]
		public async Task AbortExecuteStepTest()
		{
			WorkerService Ws = GetWorkerService();

			CancellationTokenSource CancelSource = new CancellationTokenSource();
			CancellationTokenSource StepCancelSource = new CancellationTokenSource();
			JobStepOutcome StepOutcome;
			JobStepState StepState;

			IExecutor Executor = new SimpleTestExecutor(async (StepResponse, Logger, CancelToken) =>
			{
				CancelSource.CancelAfter(10);
				await Task.Delay(5000, CancelToken);
				return JobStepOutcome.Success;
			});
			await Assert.ThrowsExceptionAsync<TaskCanceledException>(() => Ws.ExecuteStepAsync(Executor,
				new BeginStepResponse(), WorkerLogger, CancelSource.Token, StepCancelSource.Token));

			CancelSource = new CancellationTokenSource();
			StepCancelSource = new CancellationTokenSource();

			Executor = new SimpleTestExecutor(async (StepResponse, Logger, CancelToken) =>
			{
				StepCancelSource.CancelAfter(10);
				await Task.Delay(5000, CancelToken);
				return JobStepOutcome.Success;
			});
			(StepOutcome, StepState) = await Ws.ExecuteStepAsync(Executor, new BeginStepResponse(), WorkerLogger,
				CancelSource.Token, StepCancelSource.Token);
			Assert.AreEqual(JobStepOutcome.Failure, StepOutcome);
			Assert.AreEqual(JobStepState.Aborted, StepState);
		}

		[TestMethod]
		public async Task AbortExecuteJobTest()
		{
			CancellationTokenSource Source = new CancellationTokenSource();
			CancellationToken Token = Source.Token;

			ExecuteJobTask ExecuteJobTask = new ExecuteJobTask();
			ExecuteJobTask.JobId = "jobId1";
			ExecuteJobTask.BatchId = "batchId1";
			ExecuteJobTask.LogId = "logId1";
			ExecuteJobTask.JobName = "jobName1";
			ExecuteJobTask.AutoSdkWorkspace = new AgentWorkspace();
			ExecuteJobTask.Workspace = new AgentWorkspace();

			HordeRpcClientStub Client = new HordeRpcClientStub(WorkerLogger);
			RpcConnectionStub RpcConnection = new RpcConnectionStub(null!, Client);

			Client.BeginStepResponses.Enqueue(new BeginStepResponse {Name = "stepName1", StepId = "stepId1"});
			Client.BeginStepResponses.Enqueue(new BeginStepResponse {Name = "stepName2", StepId = "stepId2"});
			Client.BeginStepResponses.Enqueue(new BeginStepResponse {Name = "stepName3", StepId = "stepId3"});

			GetStepRequest Step2Req = new GetStepRequest(ExecuteJobTask.JobId, ExecuteJobTask.BatchId, "stepId2");
			GetStepResponse Step2Res = new GetStepResponse(JobStepOutcome.Unspecified, JobStepState.Unspecified, true);
			Client.GetStepResponses[Step2Req] = Step2Res;

			IExecutor Executor = new SimpleTestExecutor(async (Step, Logger, CancelToken) =>
			{
				await Task.Delay(50, CancelToken);
				return JobStepOutcome.Success;
			});

			WorkerService Ws = GetWorkerService((a, b, c) => Executor);
			Ws.StepAbortPollInterval = TimeSpan.FromMilliseconds(1);
			LeaseOutcome Outcome = (await Ws.ExecuteJobAsync(RpcConnection, "agentId1", "leaseId1", ExecuteJobTask,
				WorkerLogger, Token)).Outcome;

			Assert.AreEqual(LeaseOutcome.Success, Outcome);
			Assert.AreEqual(4, Client.UpdateStepRequests.Count);
			// An extra UpdateStep request is sent by JsonRpcLogger on failures which is why four requests
			// are returned instead of three.
			Assert.AreEqual(JobStepOutcome.Success, Client.UpdateStepRequests[0].Outcome);
			Assert.AreEqual(JobStepState.Completed, Client.UpdateStepRequests[0].State);
			Assert.AreEqual(JobStepOutcome.Failure, Client.UpdateStepRequests[1].Outcome);
			Assert.AreEqual(JobStepState.Unspecified, Client.UpdateStepRequests[1].State);
			Assert.AreEqual(JobStepOutcome.Failure, Client.UpdateStepRequests[2].Outcome);
			Assert.AreEqual(JobStepState.Aborted, Client.UpdateStepRequests[2].State);
			Assert.AreEqual(JobStepOutcome.Success, Client.UpdateStepRequests[3].Outcome);
			Assert.AreEqual(JobStepState.Completed, Client.UpdateStepRequests[3].State);
		}
		
		[TestMethod]
		public async Task PollForStepAbortFailureTest()
		{
			IExecutor Executor = new SimpleTestExecutor(async (Step, Logger, CancelToken) =>
			{
				await Task.Delay(50, CancelToken);
				return JobStepOutcome.Success;
			});

			WorkerService Ws = GetWorkerService((a, b, c) => Executor);
			Ws.StepAbortPollInterval = TimeSpan.FromMilliseconds(5);
			
			HordeRpcClientStub Client = new HordeRpcClientStub(WorkerLogger);
			RpcConnectionStub RpcConnection = new RpcConnectionStub(null!, Client);

			int c = 0;
			Client.GetStepFunc = (Request) =>
			{
				return ++c switch
				{
					1 => new GetStepResponse {AbortRequested = false},
					2 => throw new RpcException(new Status(StatusCode.Cancelled, "Fake cancel from test")),
					3 => new GetStepResponse {AbortRequested = true},
					_ => throw new Exception("Should never reach here")
				};
			};

			using CancellationTokenSource StepPollCancelSource = new CancellationTokenSource();
			using CancellationTokenSource StepCancelSource = new CancellationTokenSource();
			TaskCompletionSource<bool> StepFinishedSource = new TaskCompletionSource<bool>();

			await Ws.PollForStepAbort(RpcConnection, "jobId1", "batchId1", "logId1", StepPollCancelSource.Token, StepCancelSource, StepFinishedSource.Task);
			Assert.IsTrue(StepCancelSource.IsCancellationRequested);
		}
	}
}