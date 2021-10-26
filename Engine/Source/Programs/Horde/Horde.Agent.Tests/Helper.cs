// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using Grpc.Net.Client;
using HordeAgent.Execution.Interfaces;
using HordeAgent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace HordeAgentTests
{
	// Stub for fulfilling IOptionsMonitor interface during testing
	// Copied from HordeServerTests until a good way to share code between these is decided.
	public class TestOptionsMonitor<T> : IOptionsMonitor<T>, IDisposable
		where T : class, new()
	{
		public TestOptionsMonitor(T CurrentValue)
		{
			this.CurrentValue = CurrentValue;
		}

		public T Get(string Name)
		{
			return CurrentValue;
		}

		public IDisposable OnChange(Action<T, string> listener)
		{
			return this;
		}

		public T CurrentValue { get; }

		public void Dispose()
		{
			// Dummy stub to satisfy return value of OnChange 
		}
	}

	class RpcClientRefStub : IRpcClientRef
	{
		public GrpcChannel Channel { get; }
		public HordeRpc.HordeRpcClient Client { get; }
		public Task DisposingTask { get; }

		public RpcClientRefStub(GrpcChannel Channel, HordeRpc.HordeRpcClient Client)
		{
			this.Channel = Channel;
			this.Client = Client;
			DisposingTask = new TaskCompletionSource<bool>().Task;
		}

		public void Dispose()
		{
		}
	}

	class RpcConnectionStub : IRpcConnection
	{
		private readonly GrpcChannel GrpcChannel;
		private readonly HordeRpc.HordeRpcClient HordeRpcClient;

		public RpcConnectionStub(GrpcChannel GrpcChannel, HordeRpc.HordeRpcClient HordeRpcClient)
		{
			this.GrpcChannel = GrpcChannel;
			this.HordeRpcClient = HordeRpcClient;
		}

		public IRpcClientRef? TryGetClientRef(RpcContext Context)
		{
			return new RpcClientRefStub(GrpcChannel, HordeRpcClient);
		}

		public Task<IRpcClientRef> GetClientRef(RpcContext Context, CancellationToken CancellationToken)
		{
			IRpcClientRef RpcClientRefStub = new RpcClientRefStub(GrpcChannel, HordeRpcClient);
			return Task.FromResult(RpcClientRefStub);
		}

		public Task<T> InvokeOnceAsync<T>(Func<HordeRpc.HordeRpcClient, Task<T>> Func, RpcContext Context,
			CancellationToken CancellationToken)
		{
			return Func(HordeRpcClient);
		}

		public Task<T> InvokeOnceAsync<T>(Func<HordeRpc.HordeRpcClient, AsyncUnaryCall<T>> Func, RpcContext Context,
			CancellationToken CancellationToken)
		{
			return Func(HordeRpcClient).ResponseAsync;
		}

		public Task<T> InvokeAsync<T>(Func<HordeRpc.HordeRpcClient, Task<T>> Func, RpcContext Context,
			CancellationToken CancellationToken)
		{
			return Func(HordeRpcClient);
		}

		public Task<T> InvokeAsync<T>(Func<HordeRpc.HordeRpcClient, AsyncUnaryCall<T>> Func, RpcContext Context,
			CancellationToken CancellationToken)
		{
			return Func(HordeRpcClient).ResponseAsync;
		}

		public ValueTask DisposeAsync()
		{
			return new ValueTask();
		}
	}

	class HordeRpcClientStub : HordeRpc.HordeRpcClient
	{
		public readonly Queue<BeginStepResponse> BeginStepResponses = new Queue<BeginStepResponse>();
		public readonly List<UpdateStepRequest> UpdateStepRequests = new List<UpdateStepRequest>();
		public readonly Dictionary<GetStepRequest, GetStepResponse> GetStepResponses = new Dictionary<GetStepRequest, GetStepResponse>();
		public Func<GetStepRequest, GetStepResponse>? GetStepFunc = null;
		private readonly ILogger Logger;

		public HordeRpcClientStub(ILogger Logger)
		{
			this.Logger = Logger;
		}

		public override AsyncUnaryCall<BeginBatchResponse> BeginBatchAsync(BeginBatchRequest Request,
			CallOptions Options)
		{
			Logger.LogDebug("HordeRpcClientStub.BeginBatchAsync()");
			BeginBatchResponse Res = new BeginBatchResponse();

			Res.AgentType = "agentType1";
			Res.LogId = "logId1";

			return Wrap(Res);
		}

		public override AsyncUnaryCall<Empty> FinishBatchAsync(FinishBatchRequest Request, CallOptions Options)
		{
			Empty Res = new Empty();
			return Wrap(Res);
		}

		public override AsyncUnaryCall<GetStreamResponse> GetStreamAsync(GetStreamRequest Request, CallOptions Options)
		{
			GetStreamResponse Res = new GetStreamResponse();
			return Wrap(Res);
		}

		public override AsyncUnaryCall<GetJobResponse> GetJobAsync(GetJobRequest Request, CallOptions Options)
		{
			GetJobResponse Res = new GetJobResponse();
			return Wrap(Res);
		}

		public override AsyncUnaryCall<BeginStepResponse> BeginStepAsync(BeginStepRequest Request, CallOptions Options)
		{
			if (BeginStepResponses.Count == 0)
			{
				BeginStepResponse CompleteRes = new BeginStepResponse();
				CompleteRes.State = BeginStepResponse.Types.Result.Complete;
				return Wrap(CompleteRes);
			}

			BeginStepResponse Res = BeginStepResponses.Dequeue();
			Res.State = BeginStepResponse.Types.Result.Ready;
			return Wrap(Res);
		}

		public override AsyncUnaryCall<Empty> WriteOutputAsync(WriteOutputRequest Request, CallOptions Options)
		{
			Logger.LogDebug("WriteOutputAsync: " + Request.Data);
			Empty Res = new Empty();
			return Wrap(Res);
		}

		public override AsyncUnaryCall<Empty> UpdateStepAsync(UpdateStepRequest Request, CallOptions Options)
		{
			Logger.LogDebug($"UpdateStepAsync(Request: {Request})");
			UpdateStepRequests.Add(Request);
			Empty Res = new Empty();
			return Wrap(Res);
		}

		public override AsyncUnaryCall<Empty> CreateEventsAsync(CreateEventsRequest Request, CallOptions Options)
		{
			Logger.LogDebug("CreateEventsAsync: " + Request);
			Empty Res = new Empty();
			return Wrap(Res);
		}

		public override AsyncUnaryCall<GetStepResponse> GetStepAsync(GetStepRequest Request, CallOptions Options)
		{
			if (GetStepFunc != null)
			{
				return Wrap(GetStepFunc(Request));
			}
		
			if (GetStepResponses.TryGetValue(Request, out GetStepResponse? Res))
			{
				return Wrap(Res);
			}
			
			return Wrap(new GetStepResponse());
		}

		private AsyncUnaryCall<T> Wrap<T>(T Res)
		{
			return new AsyncUnaryCall<T>(Task.FromResult(Res), Task.FromResult(Metadata.Empty),
				() => Status.DefaultSuccess, () => Metadata.Empty, null!);
		}
	}

	class SimpleTestExecutor : IExecutor
	{
		private Func<BeginStepResponse, ILogger, CancellationToken, Task<JobStepOutcome>> Func;

		public SimpleTestExecutor(Func<BeginStepResponse, ILogger, CancellationToken, Task<JobStepOutcome>> Func)
		{
			this.Func = Func;
		}

		public Task InitializeAsync(ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogDebug("SimpleTestExecutor.InitializeAsync()");
			return Task.CompletedTask;
		}

		public Task<JobStepOutcome> RunAsync(BeginStepResponse Step, ILogger Logger,
			CancellationToken CancellationToken)
		{
			Logger.LogDebug($"SimpleTestExecutor.RunAsync(Step: {Step})");
			return Func(Step, Logger, CancellationToken);
		}

		public Task FinalizeAsync(ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogDebug("SimpleTestExecutor.FinalizeAsync()");
			return Task.CompletedTask;
		}
	}
}