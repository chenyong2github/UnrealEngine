// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Claims;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using Google.Protobuf;
using Grpc.Core;
using HordeServer;
using HordeServer.Collections;
using HordeCommon.Rpc;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using HordeServerTests.Stubs.Collections;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.Features;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Bson;
using AgentCapabilities = HordeCommon.Rpc.Messages.AgentCapabilities;
using GlobalPermissions = HordeServer.Models.GlobalPermissions;
using ISession = Microsoft.AspNetCore.Http.ISession;
using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;


namespace HordeServerTests
{
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;

	public class AppLifetimeStub : IHostApplicationLifetime
	{
		public CancellationToken ApplicationStarted { get; }
		public CancellationToken ApplicationStopping { get; }
		public CancellationToken ApplicationStopped { get; }

		public AppLifetimeStub()
		{
			ApplicationStarted = new CancellationToken();
			ApplicationStopping = new CancellationToken();
			ApplicationStopped = new CancellationToken();
		}

		public void StopApplication()
		{
			throw new NotImplementedException();
		}
	}

	[TestClass]
	public class RpcServiceTest : TestSetup
	{
		private readonly ServerCallContext AdminContext = new ServerCallContextStub("app-horde-admins");

		sealed class HttpContextStub : HttpContext
		{
			public override ConnectionInfo Connection { get; } = null!;
			public override IFeatureCollection Features { get; } = null!;
			public override IDictionary<object, object?> Items { get; set; } = null!;
			public override HttpRequest Request { get; } = null!;
			public override CancellationToken RequestAborted { get; set; }
			public override IServiceProvider RequestServices { get; set; } = null!;
			public override HttpResponse Response { get; } = null!;
			public override ISession Session { get; set; } = null!;
			public override string TraceIdentifier { get; set; } = null!;
			public override ClaimsPrincipal User { get; set; }
			public override WebSocketManager WebSockets { get; } = null!;

			public HttpContextStub(string RoleClaimType)
			{
				User = new ClaimsPrincipal(new ClaimsIdentity(new List<Claim>
				{
					new Claim(HordeClaimTypes.Role, RoleClaimType),
				}, "TestAuthType"));
			}
			
			public HttpContextStub(ClaimsPrincipal User)
			{
				this.User = User;
			}

			public override void Abort()
			{
				throw new NotImplementedException();
			}
		}

		public class ServerCallContextStub : ServerCallContext
		{
			// Copied from ServerCallContextExtensions.cs in Grpc.Core
			const string HttpContextKey = "__HttpContext";

			public static ServerCallContext ForAdminWithAgentSessionId(string AgentSessionId)
			{
				return new ServerCallContextStub(new ClaimsPrincipal(new ClaimsIdentity(new List<Claim>
				{
					new Claim(HordeClaimTypes.Role, "app-horde-admins"),
					new Claim(HordeClaimTypes.AgentSessionId, AgentSessionId),
				}, "TestAuthType")));
			}
			
			public static ServerCallContext ForAdmin()
			{
				return new ServerCallContextStub(new ClaimsPrincipal(new ClaimsIdentity(new List<Claim>
				{
					new Claim(HordeClaimTypes.Role, "app-horde-admins"),
				}, "TestAuthType")));
			}

			public ServerCallContextStub(string RoleClaimType)
			{
				// The GetHttpContext extension falls back to getting the HttpContext from UserState
				// We can piggyback on that behavior during tests
				UserState[HttpContextKey] = new HttpContextStub(RoleClaimType);
			}
			
			public ServerCallContextStub(ClaimsPrincipal User)
			{
				// The GetHttpContext extension falls back to getting the HttpContext from UserState
				// We can piggyback on that behavior during tests
				UserState[HttpContextKey] = new HttpContextStub(User);
			}

			protected override Task WriteResponseHeadersAsyncCore(Metadata ResponseHeaders)
			{
				throw new NotImplementedException();
			}

			protected override ContextPropagationToken CreatePropagationTokenCore(ContextPropagationOptions Options)
			{
				throw new NotImplementedException();
			}

			protected override string MethodCore { get; } = null!;
			protected override string HostCore { get; } = null!;
			protected override string PeerCore { get; } = null!;
			protected override DateTime DeadlineCore { get; } = DateTime.Now.AddHours(24);
			protected override Metadata RequestHeadersCore { get; } = null!;
			protected override CancellationToken CancellationTokenCore { get; } = new CancellationToken();
			protected override Metadata ResponseTrailersCore { get; } = null!;
			protected override Status StatusCore { get; set; }
			protected override WriteOptions WriteOptionsCore { get; set; } = null!;
			protected override AuthContext AuthContextCore { get; } = null!;
		}
		
		public class RpcServiceInvoker : CallInvoker
		{
			private readonly RpcService RpcService;
			private readonly ServerCallContext ServerCallContext;

			public RpcServiceInvoker(RpcService RpcService, ServerCallContext ServerCallContext)
			{
				this.RpcService = RpcService;
				this.ServerCallContext = ServerCallContext;
			}

			public override TResponse BlockingUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
			{
				throw new NotImplementedException("Blocking calls are not supported! Method " + method.FullName);
			}

			public override AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> Method, string Host, CallOptions Options, TRequest Request)
			{
				MethodInfo MethodInfo = GetMethod(Method.Name);
				Task<TResponse> Res = (MethodInfo.Invoke(RpcService, new object[] {Request, ServerCallContext}) as Task<TResponse>)!;
				return new AsyncUnaryCall<TResponse>(Res, null!, null!, null!, null!, null!);
			}

			public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> Method, string Host, CallOptions Options,
				TRequest Request)
			{
				Console.WriteLine($"RpcServiceInvoker.AsyncServerStreamingCall(method={Method.FullName} request={Request})");
				throw new NotImplementedException();
			}

			public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> Method, string Host, CallOptions Options)
			{
				Console.WriteLine($"RpcServiceInvoker.AsyncClientStreamingCall(method={Method.FullName})");
				throw new NotImplementedException();
			}

			public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> Method, string Host, CallOptions Options)
			{
				Console.WriteLine($"RpcServiceInvoker.AsyncDuplexStreamingCall(method={Method.FullName})");

				GrpcDuplexStreamHandler<TRequest> RequestStream = new GrpcDuplexStreamHandler<TRequest>(ServerCallContext);
				GrpcDuplexStreamHandler<TResponse> ResponseStream = new GrpcDuplexStreamHandler<TResponse>(ServerCallContext);

				MethodInfo MethodInfo = GetMethod(Method.Name);
				Task MethodTask = (MethodInfo.Invoke(RpcService, new object[] {RequestStream, ResponseStream, ServerCallContext}) as Task)!;
				MethodTask.ContinueWith(t => { Console.Error.WriteLine($"Uncaught exception in {Method.Name}: {t}"); }, TaskContinuationOptions.OnlyOnFaulted);
				
				return new AsyncDuplexStreamingCall<TRequest, TResponse>(RequestStream, ResponseStream, null!, null!, null!, null!);
			}
			
			private MethodInfo GetMethod(string MethodName)
			{
				MethodInfo? Method = RpcService.GetType().GetMethod(MethodName);
				if (Method == null)
				{
					throw new ArgumentException($"Method {MethodName} not found in RpcService");
				}

				return Method;
			}
		}
		
		/// <summary>
		/// Combines and cross-writes streams for a duplex streaming call in gRPC
		/// </summary>
		/// <typeparam name="T"></typeparam>
		public class GrpcDuplexStreamHandler<T> : IServerStreamWriter<T>, IClientStreamWriter<T>, IAsyncStreamReader<T> where T : class
		{
			private readonly ServerCallContext ServerCallContext;
			private readonly Channel<T> Channel;

			public WriteOptions? WriteOptions { get; set; }

			public GrpcDuplexStreamHandler(ServerCallContext ServerCallContext)
			{
				Channel = System.Threading.Channels.Channel.CreateUnbounded<T>();

				this.ServerCallContext = ServerCallContext;
			}

			public void Complete()
			{
				Channel.Writer.Complete();
			}

			public IAsyncEnumerable<T> ReadAllAsync()
			{
				return Channel.Reader.ReadAllAsync();
			}

			public async Task<T?> ReadNextAsync()
			{
				if (await Channel.Reader.WaitToReadAsync())
				{
					Channel.Reader.TryRead(out var message);
					return message;
				}
				else
				{
					return null;
				}
			}

			public Task WriteAsync(T Message)
			{
				if (ServerCallContext.CancellationToken.IsCancellationRequested)
				{
					return Task.FromCanceled(ServerCallContext.CancellationToken);
				}

				if (!Channel.Writer.TryWrite(Message))
				{
					throw new InvalidOperationException("Unable to write message.");
				}

				return Task.CompletedTask;
			}

			public Task CompleteAsync()
			{
				Complete();
				return Task.CompletedTask;
			}

			public async Task<bool> MoveNext(CancellationToken CancellationToken)
			{
				ServerCallContext.CancellationToken.ThrowIfCancellationRequested();
				
				if (await Channel.Reader.WaitToReadAsync(CancellationToken))
				{
					if (Channel.Reader.TryRead(out var Message))
					{
						Current = Message;
						return true;
					}
				}

				Current = null!;
				return false;
			}

			public T Current { get; private set; } = null!;
		}

		[TestMethod]
		public async Task CreateSessionTest()
		{
			CreateSessionRequest Req = new CreateSessionRequest();
			await Assert.ThrowsExceptionAsync<StructuredRpcException>(() => RpcService.CreateSession(Req, AdminContext));

			Req.Name = "MyName";
			await Assert.ThrowsExceptionAsync<StructuredRpcException>(() => RpcService.CreateSession(Req, AdminContext));

			Req.Capabilities = new AgentCapabilities();
			CreateSessionResponse Res = await RpcService.CreateSession(Req, AdminContext);

			Assert.AreEqual("MYNAME", Res.AgentId);
			// TODO: Check Token, ExpiryTime, SessionId 
		}

		[TestMethod]
		public async Task UpdateSessionTest()
		{
			CreateSessionRequest CreateReq = new CreateSessionRequest
			{
				Name = "UpdateSessionTest1", Capabilities = new AgentCapabilities()
			};
			CreateSessionResponse CreateRes = await RpcService.CreateSession(CreateReq, AdminContext);
			string AgentId = CreateRes.AgentId;
			string SessionId = CreateRes.SessionId;

			TestAsyncStreamReader<UpdateSessionRequest> RequestStream =
				new TestAsyncStreamReader<UpdateSessionRequest>(AdminContext);
			TestServerStreamWriter<UpdateSessionResponse> ResponseStream =
				new TestServerStreamWriter<UpdateSessionResponse>(AdminContext);
			Task Call = RpcService.UpdateSession(RequestStream, ResponseStream, AdminContext);

			RequestStream.AddMessage(new UpdateSessionRequest {AgentId = "does-not-exist", SessionId = SessionId});
			StructuredRpcException Re = await Assert.ThrowsExceptionAsync<StructuredRpcException>(() => Call);
			Assert.AreEqual(StatusCode.NotFound, Re.StatusCode);
			Assert.IsTrue(Re.Message.Contains("Invalid agent name"));
		}
		
		[TestMethod]
		public async Task QueryServerSessionTest()
		{
			RpcService.LongPollTimeout = TimeSpan.FromMilliseconds(200);

			TestAsyncStreamReader<QueryServerStateRequest> RequestStream =
				new TestAsyncStreamReader<QueryServerStateRequest>(AdminContext);
			TestServerStreamWriter<QueryServerStateResponse> ResponseStream =
				new TestServerStreamWriter<QueryServerStateResponse>(AdminContext);
			Task Call = RpcService.QueryServerState(RequestStream, ResponseStream, AdminContext);

			RequestStream.AddMessage(new QueryServerStateRequest {Name = "bogusAgentName"});
			QueryServerStateResponse? Res = await ResponseStream.ReadNextAsync();
			Assert.IsNotNull(Res);
			
			Res = await ResponseStream.ReadNextAsync();
			Assert.IsNotNull(Res);

			// Should timeout after LongPollTimeout specified above
			await Call;
		}
		
		[TestMethod]
		public async Task FinishBatchTest()
		{
			CreateSessionRequest CreateReq = new CreateSessionRequest
			{
				Name = "UpdateSessionTest1", Capabilities = new AgentCapabilities()
			};
			CreateSessionResponse CreateRes = await RpcService.CreateSession(CreateReq, AdminContext);
			string AgentId = CreateRes.AgentId;
			string SessionId = CreateRes.SessionId;

			TestAsyncStreamReader<UpdateSessionRequest> RequestStream =
				new TestAsyncStreamReader<UpdateSessionRequest>(AdminContext);
			TestServerStreamWriter<UpdateSessionResponse> ResponseStream =
				new TestServerStreamWriter<UpdateSessionResponse>(AdminContext);
			Task Call = RpcService.UpdateSession(RequestStream, ResponseStream, AdminContext);

			RequestStream.AddMessage(new UpdateSessionRequest {AgentId = "does-not-exist", SessionId = SessionId});
			StructuredRpcException Re = await Assert.ThrowsExceptionAsync<StructuredRpcException>(() => Call);
			Assert.AreEqual(StatusCode.NotFound, Re.StatusCode);
			Assert.IsTrue(Re.Message.Contains("Invalid agent name"));
		}
		
		[TestMethod]
		public async Task UploadArtifactTest()
		{
			Fixture Fixture = await CreateFixtureAsync();

			ObjectId SessionId = ObjectId.GenerateNewId();
			ServerCallContext Context = new ServerCallContextStub(new ClaimsPrincipal(new ClaimsIdentity(new List<Claim>
			{
				new Claim(HordeClaimTypes.Role, "app-horde-admins"),
				new Claim(HordeClaimTypes.AgentSessionId, SessionId.ToString()),
			}, "TestAuthType")));


			string[] Data = {"foo", "bar", "baz", "qux"};
			string DataStr = string.Join("", Data);
			
			UploadArtifactMetadata Metadata = new UploadArtifactMetadata
			{
				JobId = Fixture.Job1.Id.ToString(),
				BatchId = Fixture.Job1.Batches[0].Id.ToString(),
				StepId = Fixture.Job1.Batches[0].Steps[0].Id.ToString(),
				Name = "testfile.txt",
				MimeType = "text/plain",
				Length = DataStr.Length
			};

			// Set the session ID on the job batch to pass auth later
			Deref(await JobCollection.TryAssignLeaseAsync(Fixture.Job1, 0, new PoolId("foo"),
				new AgentId("test"), SessionId,
				LeaseId.GenerateNewId(), LogId.GenerateNewId()));
/*
			TestAsyncStreamReader<UploadArtifactRequest> RequestStream = new TestAsyncStreamReader<UploadArtifactRequest>(Context);
			Task<UploadArtifactResponse> Call = TestSetup.RpcService.UploadArtifact(RequestStream,  Context);
			RequestStream.AddMessage(new UploadArtifactRequest { Metadata = Metadata });
			RequestStream.AddMessage(new UploadArtifactRequest { Data = ByteString.CopyFromUtf8(Data[0]) });
			RequestStream.AddMessage(new UploadArtifactRequest { Data = ByteString.CopyFromUtf8(Data[1]) });
			RequestStream.AddMessage(new UploadArtifactRequest { Data = ByteString.CopyFromUtf8(Data[2]) });
			// Only send three messages and not the last one.
			// Aborting the upload here and retry in next code section below.
			RequestStream.Complete();
			await Task.Delay(500);
*/			
			TestAsyncStreamReader<UploadArtifactRequest> RequestStream = new TestAsyncStreamReader<UploadArtifactRequest>(Context);
			Task<UploadArtifactResponse> Call = RpcService.UploadArtifact(RequestStream,  Context);
			RequestStream.AddMessage(new UploadArtifactRequest { Metadata = Metadata });
			RequestStream.AddMessage(new UploadArtifactRequest { Data = ByteString.CopyFromUtf8(Data[0]) });
			RequestStream.AddMessage(new UploadArtifactRequest { Data = ByteString.CopyFromUtf8(Data[1]) });
			RequestStream.AddMessage(new UploadArtifactRequest { Data = ByteString.CopyFromUtf8(Data[2]) });
			RequestStream.AddMessage(new UploadArtifactRequest { Data = ByteString.CopyFromUtf8(Data[3]) });
			UploadArtifactResponse Res = await Call;


			IArtifact? Artifact = await ArtifactCollection.GetArtifactAsync(ObjectId.Parse(Res.Id));
			Assert.IsNotNull(Artifact);
			Stream Stream = await ArtifactCollection.OpenArtifactReadStreamAsync(Artifact!);
			StreamReader Reader = new StreamReader(Stream);
			string text = Reader.ReadToEnd();
			Assert.AreEqual(DataStr, text);
		}
		/*
		[TestMethod]
		public async Task UploadSoftwareAsync()
		{
			TestSetup TestSetup = await GetTestSetup();
			
			MemoryStream OutputStream = new MemoryStream();
			using (ZipArchive ZipFile = new ZipArchive(OutputStream, ZipArchiveMode.Create, false))
			{
				string TempFilename = Path.GetTempFileName();
				File.WriteAllText(TempFilename, "{\"Horde\": {\"Version\": \"myVersion\"}}");
				ZipFile.CreateEntryFromFile(TempFilename, "appsettings.json");
			}

			ByteString Data = ByteString.CopyFrom(OutputStream.ToArray());
			UploadSoftwareRequest Req = new UploadSoftwareRequest { Channel = "boguschannel", Data = Data };

			UploadSoftwareResponse Res1 = await TestSetup.RpcService.UploadSoftware(Req, AdminContext);
			Assert.AreEqual("r1", Res1.Version);
			
			UploadSoftwareResponse Res2 = await TestSetup.RpcService.UploadSoftware(Req, AdminContext);
			Assert.AreEqual("r2", Res2.Version);
		}
		*/
	}
}