// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Horde.Agent.Execution.Interfaces;
using Horde.Agent.Services;
using Horde.Agent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using HordeCommon.Rpc.Messages;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Agent.Tests
{
	[TestClass]
	public class WorkerServiceTest
	{
		private readonly ILogger<WorkerService> _workerLogger;

		public WorkerServiceTest()
		{
			_workerLogger = NullLogger<WorkerService>.Instance;
		}

		private WorkerService GetWorkerService(
			Func<IRpcConnection, ExecuteJobTask, BeginBatchResponse, IExecutor>? createExecutor = null)
		{
			AgentSettings settings = new AgentSettings();
			ServerProfile profile = new ServerProfile();
			profile.Name = "test";
			profile.Environment = "test-env";
			profile.Token = "bogus-token";
			profile.Url = new Uri("http://localhost");
			settings.ServerProfiles.Add(profile);
			settings.Server = "test";
			settings.WorkingDir = Path.GetTempPath();
			settings.Executor = ExecutorType.Test; // Not really used since the executor is overridden in the tests
			IOptions<AgentSettings> settingsMonitor = new TestOptionsMonitor<AgentSettings>(settings);

			return new WorkerService(_workerLogger, settingsMonitor, null!, null!, createExecutor);
		}

		[TestMethod]
		public async Task AbortExecuteStepTest()
		{
			using WorkerService ws = GetWorkerService();

			{
				using CancellationTokenSource cancelSource = new CancellationTokenSource();
				using CancellationTokenSource stepCancelSource = new CancellationTokenSource();

				IExecutor executor = new SimpleTestExecutor(async (stepResponse, logger, cancelToken) =>
				{
					cancelSource.CancelAfter(10);
					await Task.Delay(5000, cancelToken);
					return JobStepOutcome.Success;
				});
				await Assert.ThrowsExceptionAsync<TaskCanceledException>(() => WorkerService.ExecuteStepAsync(executor,
					new BeginStepResponse(), _workerLogger, cancelSource.Token, stepCancelSource.Token));
			}

			{
				using CancellationTokenSource cancelSource = new CancellationTokenSource();
				using CancellationTokenSource stepCancelSource = new CancellationTokenSource();

				IExecutor executor = new SimpleTestExecutor(async (stepResponse, logger, cancelToken) =>
				{
					stepCancelSource.CancelAfter(10);
					await Task.Delay(5000, cancelToken);
					return JobStepOutcome.Success;
				});
				(JobStepOutcome stepOutcome, JobStepState stepState) = await WorkerService.ExecuteStepAsync(executor, new BeginStepResponse(), _workerLogger,
					cancelSource.Token, stepCancelSource.Token);
				Assert.AreEqual(JobStepOutcome.Failure, stepOutcome);
				Assert.AreEqual(JobStepState.Aborted, stepState);
			}
		}

		[TestMethod]
		public async Task AbortExecuteJobTest()
		{
			using CancellationTokenSource source = new CancellationTokenSource();
			CancellationToken token = source.Token;

			ExecuteJobTask executeJobTask = new ExecuteJobTask();
			executeJobTask.JobId = "jobId1";
			executeJobTask.BatchId = "batchId1";
			executeJobTask.LogId = "logId1";
			executeJobTask.JobName = "jobName1";
			executeJobTask.AutoSdkWorkspace = new AgentWorkspace();
			executeJobTask.Workspace = new AgentWorkspace();

			HordeRpcClientStub client = new HordeRpcClientStub(_workerLogger);
			await using RpcConnectionStub rpcConnection = new RpcConnectionStub(null!, client);

			client.BeginStepResponses.Enqueue(new BeginStepResponse {Name = "stepName1", StepId = "stepId1"});
			client.BeginStepResponses.Enqueue(new BeginStepResponse {Name = "stepName2", StepId = "stepId2"});
			client.BeginStepResponses.Enqueue(new BeginStepResponse {Name = "stepName3", StepId = "stepId3"});

			GetStepRequest step2Req = new GetStepRequest(executeJobTask.JobId, executeJobTask.BatchId, "stepId2");
			GetStepResponse step2Res = new GetStepResponse(JobStepOutcome.Unspecified, JobStepState.Unspecified, true);
			client.GetStepResponses[step2Req] = step2Res;

			IExecutor executor = new SimpleTestExecutor(async (step, logger, cancelToken) =>
			{
				await Task.Delay(50, cancelToken);
				return JobStepOutcome.Success;
			});

			using WorkerService ws = GetWorkerService((a, b, c) => executor);
			ws._stepAbortPollInterval = TimeSpan.FromMilliseconds(1);
			LeaseOutcome outcome = (await ws.ExecuteJobAsync(rpcConnection, "leaseId1", executeJobTask,
				_workerLogger, token)).Outcome;

			Assert.AreEqual(LeaseOutcome.Success, outcome);
			Assert.AreEqual(4, client.UpdateStepRequests.Count);
			// An extra UpdateStep request is sent by JsonRpcLogger on failures which is why four requests
			// are returned instead of three.
			Assert.AreEqual(JobStepOutcome.Success, client.UpdateStepRequests[0].Outcome);
			Assert.AreEqual(JobStepState.Completed, client.UpdateStepRequests[0].State);
			Assert.AreEqual(JobStepOutcome.Failure, client.UpdateStepRequests[1].Outcome);
			Assert.AreEqual(JobStepState.Unspecified, client.UpdateStepRequests[1].State);
			Assert.AreEqual(JobStepOutcome.Failure, client.UpdateStepRequests[2].Outcome);
			Assert.AreEqual(JobStepState.Aborted, client.UpdateStepRequests[2].State);
			Assert.AreEqual(JobStepOutcome.Success, client.UpdateStepRequests[3].Outcome);
			Assert.AreEqual(JobStepState.Completed, client.UpdateStepRequests[3].State);
		}
		
		[TestMethod]
		public async Task PollForStepAbortFailureTest()
		{
			IExecutor executor = new SimpleTestExecutor(async (step, logger, cancelToken) =>
			{
				await Task.Delay(50, cancelToken);
				return JobStepOutcome.Success;
			});

			using WorkerService ws = GetWorkerService((a, b, c) => executor);
			ws._stepAbortPollInterval = TimeSpan.FromMilliseconds(5);
			
			HordeRpcClientStub client = new HordeRpcClientStub(_workerLogger);
			await using RpcConnectionStub rpcConnection = new RpcConnectionStub(null!, client);

			int c = 0;
			client._getStepFunc = (request) =>
			{
				return ++c switch
				{
					1 => new GetStepResponse {AbortRequested = false},
					2 => throw new RpcException(new Status(StatusCode.Cancelled, "Fake cancel from test")),
					3 => new GetStepResponse {AbortRequested = true},
					_ => throw new Exception("Should never reach here")
				};
			};

			using CancellationTokenSource stepPollCancelSource = new CancellationTokenSource();
			using CancellationTokenSource stepCancelSource = new CancellationTokenSource();
			TaskCompletionSource<bool> stepFinishedSource = new TaskCompletionSource<bool>();

			await ws.PollForStepAbort(rpcConnection, "jobId1", "batchId1", "logId1", stepCancelSource, stepFinishedSource.Task, stepPollCancelSource.Token);
			Assert.IsTrue(stepCancelSource.IsCancellationRequested);
		}
	}
}