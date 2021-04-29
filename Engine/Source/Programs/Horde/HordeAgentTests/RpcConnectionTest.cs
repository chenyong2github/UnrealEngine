// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Net.Client;
using HordeAgent.Utility;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Console;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

namespace HordeAgentTests
{
#if false
	[TestClass]
	public class RpcConnectionV2Test
	{
		private readonly CancellationTokenSource Cts = new CancellationTokenSource();
		private readonly Mock<HordeRpc.HordeRpcClient> Mock;
		private readonly Mock<Func<GrpcChannel>> CreateGrpcMock = new Mock<Func<GrpcChannel>>();
		private readonly RpcConnectionV2 RpcConnection;

		public RpcConnectionV2Test()
		{
			LoggerFactory LoggerFactory = new LoggerFactory();
			ConsoleLoggerOptions LoggerOptions = new ConsoleLoggerOptions();
			TestOptionsMonitor<ConsoleLoggerOptions> LoggerOptionsMon = new TestOptionsMonitor<ConsoleLoggerOptions>(LoggerOptions);
			LoggerFactory.AddProvider(new ConsoleLoggerProvider(LoggerOptionsMon));
			ILogger<RpcConnectionV2> RpcConnectionLogger = LoggerFactory.CreateLogger<RpcConnectionV2>();
			
			Mock = new Mock<HordeRpc.HordeRpcClient>();
			CreateGrpcMock.Setup(c => c()).Returns(GrpcChannel.ForAddress("http://localhost"));
			RpcConnection = new RpcConnectionV2(CreateGrpcMock.Object, RpcConnectionLogger, (c) => Mock.Object);
			RpcConnectionV2.RetryTimes = new []
			{
				TimeSpan.FromMilliseconds(1),
				TimeSpan.FromMilliseconds(1),
				TimeSpan.FromMilliseconds(1),
			};
		}
		
		[TestMethod]
		public async Task InvokeOnceTest()
		{
			Mock.Setup(c => c.GetJobAsync(It.IsAny<GetJobRequest>(), null!, null, It.IsAny<CancellationToken>()))
				.Returns(GetResponse(new GetJobResponse {StreamId = "mock-stream-id"}));
			
			GetJobResponse Res = await RpcConnection.InvokeOnceAsync(c => c.GetJobAsync(new GetJobRequest()), new RpcContext(), Cts.Token);
			Assert.AreEqual("mock-stream-id", Res.StreamId);

			AssertNumGrpcChannelsCreated(1);
		}
		
		[TestMethod]
		public async Task InvokeOnceFailTest()
		{
			Mock.SetupSequence(c => c.GetJobAsync(It.IsAny<GetJobRequest>(), null!, null, It.IsAny<CancellationToken>()))
				.Throws(new RpcException(new Status(StatusCode.Aborted, "Server is shutting down")));
			
			await Assert.ThrowsExceptionAsync<RpcException>(() => RpcConnection.InvokeOnceAsync(c => c.GetJobAsync(new GetJobRequest()), new RpcContext(), Cts.Token));
			AssertNumGrpcChannelsCreated(1);
		}

		[TestMethod]
		public async Task InvokeTest()
		{
			Mock.Setup(c => c.GetJobAsync(It.IsAny<GetJobRequest>(), null!, null, It.IsAny<CancellationToken>()))
				.Returns(GetResponse(new GetJobResponse {StreamId = "mock-stream-id"}));
			
			GetJobResponse Res = await RpcConnection.InvokeAsync(c => c.GetJobAsync(new GetJobRequest()), new RpcContext(), Cts.Token);

			Assert.AreEqual("mock-stream-id", Res.StreamId);
			AssertNumGrpcChannelsCreated(1);
		}
		
		[TestMethod]
		public async Task InvokeCancellationTest()
		{
			Cts.Cancel();
			Mock.SetupSequence(c => c.GetJobAsync(It.IsAny<GetJobRequest>(), null!, null, It.IsAny<CancellationToken>()))
				.Throws(new RpcException(new Status(StatusCode.Aborted, "Server is shutting down")))
				.Returns(GetResponse(new GetJobResponse {StreamId = "mock-stream-id"}));

			await Assert.ThrowsExceptionAsync<RpcException>(() => RpcConnection.InvokeAsync(c => c.GetJobAsync(new GetJobRequest()), new RpcContext(), Cts.Token));
			AssertNumGrpcChannelsCreated(1);			
		}
		
		[TestMethod]
		public async Task InvokeRetryAndSucceedTest()
		{
			Mock.SetupSequence(c => c.GetJobAsync(It.IsAny<GetJobRequest>(), null!, null, It.IsAny<CancellationToken>()))
				.Throws(new RpcException(new Status(StatusCode.PermissionDenied, "Permission denied")))
				.Throws(new RpcException(new Status(StatusCode.Aborted, "Server is shutting down")))
				.Returns(GetResponse(new GetJobResponse {StreamId = "mock-stream-id"}));
			
			GetJobResponse Res = await RpcConnection.InvokeAsync(c => c.GetJobAsync(new GetJobRequest()), new RpcContext(), Cts.Token);
			Assert.AreEqual("mock-stream-id", Res.StreamId);
			AssertNumGrpcChannelsCreated(2);
		}
		
		[TestMethod]
		public async Task InvokeRetryAndFailTest()
		{
			Mock.SetupSequence(c => c.GetJobAsync(It.IsAny<GetJobRequest>(), null!, null, It.IsAny<CancellationToken>()))
				.Throws(new RpcException(new Status(StatusCode.Aborted, "Server is shutting down")))
				.Throws(new RpcException(new Status(StatusCode.Cancelled, "Request is cancelled")))
				.Throws(new RpcException(new Status(StatusCode.Cancelled, "Request is cancelled")))
				.Throws(new RpcException(new Status(StatusCode.Cancelled, "Request is cancelled")))
				.Returns(GetResponse(new GetJobResponse {StreamId = "mock-stream-id"}));
			
			await Assert.ThrowsExceptionAsync<RpcException>(() => RpcConnection.InvokeAsync(c => c.GetJobAsync(new GetJobRequest()), new RpcContext(), Cts.Token));
			AssertNumGrpcChannelsCreated(4);
		}
		
		private AsyncUnaryCall<T> GetResponse<T>(T Response)
		{
			return new AsyncUnaryCall<T>(Task.FromResult(Response), null!, null!, null!, null!, null!);
		}

		private void AssertNumGrpcChannelsCreated(int Expected)
		{
			CreateGrpcMock.Verify(c => c(), Times.Exactly(Expected));
		}
	}
#endif
}