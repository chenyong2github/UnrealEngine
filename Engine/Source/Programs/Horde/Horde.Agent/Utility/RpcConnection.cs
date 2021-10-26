// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Grpc.Core;
using Grpc.Core.Interceptors;
using Grpc.Net.Client;
using HordeCommon.Rpc;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using OpenTracing;
using OpenTracing.Util;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace HordeAgent.Utility
{
	/// <summary>
	/// Ref-counted reference to a HordeRpcClient. Must be disposed after use.
	/// </summary>
	interface IRpcClientRef : IDisposable
	{
		/// <summary>
		/// The Grpc channel instance
		/// </summary>
		GrpcChannel Channel { get; }

		/// <summary>
		/// The client instance
		/// </summary>
		HordeRpc.HordeRpcClient Client { get; }

		/// <summary>
		/// Task which completes when the client needs to be disposed of
		/// </summary>
		Task DisposingTask { get; }
	}

	/// <summary>
	/// Context for an RPC call. Used for debugging reference leaks.
	/// </summary>
	class RpcContext
	{
		/// <summary>
		/// Text to display
		/// </summary>
		public string Text { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Text">Text to display</param>
		public RpcContext(string Text)
		{
			this.Text = Text;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Line">Line number of the file</param>
		/// <param name="File">Calling file</param>
		public RpcContext([CallerLineNumber] int Line = 0, [CallerFilePath] string File = "")
		{
			this.Text = $"{File}({Line})";
		}
	}

	internal interface IRpcConnection : IAsyncDisposable
	{
		/// <summary>
		/// Attempts to get a client reference, returning immediately if there's not one available
		/// </summary>
		/// <returns></returns>
		IRpcClientRef? TryGetClientRef(RpcContext Context);

		/// <summary>
		/// Obtains a new client reference object
		/// </summary>
		/// <param name="Context">Context for the call, for debugging</param>
		/// <param name="CancellationToken">Cancellation token for this request</param>
		/// <returns>New client reference</returns>
		Task<IRpcClientRef> GetClientRef(RpcContext Context, CancellationToken CancellationToken);

		/// <summary>
		/// Invokes an asynchronous command with a HordeRpcClient instance
		/// </summary>
		/// <param name="Func">The function to execute</param>
		/// <param name="Context">Context for the call, for debugging</param>
		/// <param name="CancellationToken">Cancellation token for the call</param>
		/// <returns>Response from the call</returns>
		Task<T> InvokeOnceAsync<T>(Func<HordeRpc.HordeRpcClient, Task<T>> Func, RpcContext Context, CancellationToken CancellationToken);

		/// <summary>
		/// Obtains a client reference and executes a unary RPC
		/// </summary>
		/// <typeparam name="T">Return type from the call</typeparam>
		/// <param name="Func">Method to execute with the RPC instance</param>
		/// <param name="Context">Context for the call, for debugging</param>
		/// <param name="CancellationToken">Cancellation token for the call</param>
		/// <returns>Response from the call</returns>
		Task<T> InvokeOnceAsync<T>(Func<HordeRpc.HordeRpcClient, AsyncUnaryCall<T>> Func, RpcContext Context, CancellationToken CancellationToken);

		/// <summary>
		/// Invokes an asynchronous command with a HordeRpcClient instance
		/// </summary>
		/// <param name="Func">The function to execute</param>
		/// <param name="Context">Context for the call, for debugging</param>
		/// <param name="CancellationToken">Cancellation token for the call</param>
		/// <returns>Response from the call</returns>
		Task<T> InvokeAsync<T>(Func<HordeRpc.HordeRpcClient, Task<T>> Func, RpcContext Context, CancellationToken CancellationToken);

		/// <summary>
		/// Obtains a client reference and executes a unary RPC
		/// </summary>
		/// <typeparam name="T">Return type from the call</typeparam>
		/// <param name="Func">Method to execute with the RPC instance</param>
		/// <param name="Context">Context for the call, for debugging</param>
		/// <param name="CancellationToken">Cancellation token for the call</param>
		/// <returns>Response from the call</returns>
		Task<T> InvokeAsync<T>(Func<HordeRpc.HordeRpcClient, AsyncUnaryCall<T>> Func, RpcContext Context, CancellationToken CancellationToken);
	}

	/// <summary>
	/// Represents a connection to a GRPC server, which responds to server shutting down events and attempts to reconnect to another instance.
	/// </summary>
	class RpcConnection : IRpcConnection
	{
		/// <summary>
		/// An instance of a connection to a particular server
		/// </summary>
		class RpcSubConnection : IAsyncDisposable
		{
			/// <summary>
			/// The connection id
			/// </summary>
			public int ConnectionId { get; }

			/// <summary>
			/// Name of the server
			/// </summary>
			public string Name
			{
				get;
			}

			/// <summary>
			/// The Grpc channel
			/// </summary>
			public GrpcChannel Channel { get; }

			/// <summary>
			/// The RPC interface
			/// </summary>
			public HordeRpc.HordeRpcClient Client
			{
				get;
			}

			/// <summary>
			/// Task which is set to indicate the connection is being disposed
			/// </summary>
			public Task DisposingTask => DisposingTaskSource.Task;

			/// <summary>
			/// Logger for messages about refcount changes
			/// </summary>
			ILogger Logger;

			/// <summary>
			/// The current refcount
			/// </summary>
			int RefCount;

			/// <summary>
			/// Whether the connection is terminating
			/// </summary>
			bool bDisposing;

			/// <summary>
			/// Cancellation token set 
			/// </summary>
			TaskCompletionSource<bool> DisposingTaskSource = new TaskCompletionSource<bool>();

			/// <summary>
			/// Wraps a task allowing the disposer to wait for clients to finish using this connection
			/// </summary>
			TaskCompletionSource<bool> DisposedTaskSource = new TaskCompletionSource<bool>();

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="ConnectionId">The connection id</param>
			/// <param name="Name">Name of the server</param>
			/// <param name="Channel">The channel instance</param>
			/// <param name="Client">The client instance</param>
			/// <param name="Logger">Logger for debug messages</param>
			public RpcSubConnection(int ConnectionId, string Name, GrpcChannel Channel, HordeRpc.HordeRpcClient Client, ILogger Logger)
			{
				this.ConnectionId = ConnectionId;
				this.Name = Name;
				this.Channel = Channel;
				this.Client = Client;
				this.Logger = Logger;
				this.RefCount = 1;
			}

			/// <summary>
			/// Attempts to increment the reference count for this subconnection. Fails if it's already zero.
			/// </summary>
			/// <returns>True if a reference was added</returns>
			public bool TryAddRef()
			{
				for (; ; )
				{
					int InitialRefCount = RefCount;
					if (InitialRefCount == 0)
					{
						return false;
					}
					if (Interlocked.CompareExchange(ref RefCount, InitialRefCount + 1, InitialRefCount) == InitialRefCount)
					{
						return true;
					}
				}
			}

			/// <summary>
			/// Release a reference to this connection
			/// </summary>
			public void Release()
			{
				int NewRefCount = Interlocked.Decrement(ref RefCount);
				if (bDisposing)
				{
					Logger.LogInformation("Connection {ConnectionId}: Decrementing refcount to rpc server {ServerName} ({RefCount} rpc(s) in flight)", ConnectionId, Name, NewRefCount);
				}
				if (NewRefCount == 0)
				{
					DisposedTaskSource.SetResult(true);
				}
			}

			/// <summary>
			/// Dispose of this connection, waiting for all references to be released first
			/// </summary>
			/// <returns>Async task</returns>
			public ValueTask DisposeAsync()
			{
				Logger.LogInformation("Connection {ConnectionId}: Disposing sub-connection", ConnectionId);
				DisposingTaskSource.TrySetResult(true);
				bDisposing = true;
				Release();
				return new ValueTask(DisposedTaskSource.Task);
			}
		}

		/// <summary>
		/// Reference to a connection instance
		/// </summary>
		class RpcClientRef : IRpcClientRef
		{
			/// <summary>
			/// The subconnection containing the client
			/// </summary>
			public RpcSubConnection SubConnection
			{
				get;
			}

			/// <summary>
			/// Context for the reference
			/// </summary>
			public RpcContext Context
			{
				get;
			}

			/// <summary>
			/// The channel instance
			/// </summary>
			public GrpcChannel Channel
			{
				get { return SubConnection.Channel; }
			}

			/// <summary>
			/// The client instance
			/// </summary>
			public HordeRpc.HordeRpcClient Client
			{
				get { return SubConnection.Client; }
			}

			/// <summary>
			/// Task which completes when the connection should be disposed
			/// </summary>
			public Task DisposingTask
			{
				get { return SubConnection.DisposingTask; }
			}

			/// <summary>
			/// Tracks all the active refs
			/// </summary>
			static List<RpcClientRef> CurrentRefs = new List<RpcClientRef>();

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="SharedClient">The subconnection containing the client</param>
			/// <param name="Context">Description of the reference, for debugging</param>
			public RpcClientRef(RpcSubConnection SharedClient, RpcContext Context)
			{
				this.SubConnection = SharedClient;
				this.Context = Context;

				lock (CurrentRefs)
				{
					CurrentRefs.Add(this);
				}
			}

			/// <summary>
			/// Dispose of this reference
			/// </summary>
			public void Dispose()
			{
				SubConnection.Release();

				lock(CurrentRefs)
				{
					CurrentRefs.Remove(this);
				}
			}

			/// <summary>
			/// Gets a list of all the active refs
			/// </summary>
			/// <returns>List of ref descriptions</returns>
			public static List<string> GetActiveRefs()
			{
				lock(CurrentRefs)
				{
					return CurrentRefs.Select(x => x.Context.Text).ToList();
				}
			}
		}

		/// <summary>
		/// Delays before retrying RPC calls
		/// </summary>
		static TimeSpan[] RetryTimes =
		{
			TimeSpan.FromSeconds(1.0),
			TimeSpan.FromSeconds(10.0),
			TimeSpan.FromSeconds(30.0),
		};

		/// <summary>
		/// Delegate to create a new GrpcChannel
		/// </summary>
		Func<GrpcChannel> CreateGrpcChannel;

		/// <summary>
		/// Source for tokens asking the background thread to terminate
		/// </summary>
		TaskCompletionSource<bool> StoppingTaskSource = new TaskCompletionSource<bool>();

		/// <summary>
		/// Task source for waiting for a new subconnection to be made
		/// </summary>
		TaskCompletionSource<RpcSubConnection> SubConnectionTaskSource = new TaskCompletionSource<RpcSubConnection>();

		/// <summary>
		/// Background task to maintain the connection
		/// </summary>
		Task? BackgroundTask;

		/// <summary>
		/// Logger instance
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="CreateGrpcChannel">Factory method for creating new GRPC channels</param>
		/// <param name="Logger">Logger instance</param>
		public RpcConnection(Func<GrpcChannel> CreateGrpcChannel, ILogger Logger)
		{
			this.CreateGrpcChannel = CreateGrpcChannel;
			this.Logger = Logger;

			BackgroundTask = Task.Run(() => ExecuteAsync());
		}

		/// <summary>
		/// Attempts to get a client reference, returning immediately if there's not one available
		/// </summary>
		/// <returns></returns>
		public IRpcClientRef? TryGetClientRef(RpcContext Context)
		{
			Task<RpcSubConnection> SubConnectionTask = SubConnectionTaskSource.Task;
			if (SubConnectionTask.IsCompleted)
			{
				RpcSubConnection DefaultClient = SubConnectionTask.Result;
				if(DefaultClient.TryAddRef())
				{
					return new RpcClientRef(DefaultClient, Context);
				}
			}
			return null;
		}

		/// <summary>
		/// Obtains a new client reference object
		/// </summary>
		/// <param name="Context">Context for the call, for debugging</param>
		/// <param name="CancellationToken">Cancellation token for this request</param>
		/// <returns>New client reference</returns>
		public async Task<IRpcClientRef> GetClientRef(RpcContext Context, CancellationToken CancellationToken)
		{
			for (; ; )
			{
				// Print a message while we're waiting for a client to be available
				if (!SubConnectionTaskSource.Task.IsCompleted)
				{
					Stopwatch Timer = Stopwatch.StartNew();
					while (!SubConnectionTaskSource.Task.IsCompleted)
					{
						Task Delay = Task.Delay(TimeSpan.FromSeconds(30.0), CancellationToken);
						if (await Task.WhenAny(Delay, SubConnectionTaskSource.Task, StoppingTaskSource.Task) == Delay)
						{
							await Delay; // Allow the cancellation token to throw
							Logger.LogInformation("Thread stalled for {Time:0.0}s while waiting for IRpcClientRef. Source:\n{Context}\nActive references:\n{References}\nStack trace:\n{StackTrace}", Timer.Elapsed.TotalSeconds, Context.Text, String.Join("\n", RpcClientRef.GetActiveRefs()), Environment.StackTrace);
						}
						if (StoppingTaskSource.Task.IsCompleted)
						{
							throw new OperationCanceledException("RpcConnection is being terminated");
						}
					}
				}

				// Get the new task and increase the refcount
				RpcSubConnection DefaultClient = await SubConnectionTaskSource.Task;
				if (DefaultClient.TryAddRef())
				{
					return new RpcClientRef(DefaultClient, Context);
				}
			}
		}

		/// <summary>
		/// Invokes an asynchronous command with a HordeRpcClient instance
		/// </summary>
		/// <param name="Func">The function to execute</param>
		/// <param name="Context">Context for the call, for debugging</param>
		/// <param name="CancellationToken">Cancellation token for the call</param>
		/// <returns>Response from the call</returns>
		public async Task<T> InvokeOnceAsync<T>(Func<HordeRpc.HordeRpcClient, Task<T>> Func, RpcContext Context, CancellationToken CancellationToken)
		{
			using (IRpcClientRef ClientRef = await GetClientRef(Context, CancellationToken))
			{
				return await Func(ClientRef.Client);
			}
		}

		/// <summary>
		/// Obtains a client reference and executes a unary RPC
		/// </summary>
		/// <typeparam name="T">Return type from the call</typeparam>
		/// <param name="Func">Method to execute with the RPC instance</param>
		/// <param name="Context">Context for the call, for debugging</param>
		/// <param name="CancellationToken">Cancellation token for the call</param>
		/// <returns>Response from the call</returns>
		public Task<T> InvokeOnceAsync<T>(Func<HordeRpc.HordeRpcClient, AsyncUnaryCall<T>> Func, RpcContext Context, CancellationToken CancellationToken)
		{
			return InvokeOnceAsync(async (x) => await Func(x), Context, CancellationToken);
		}

		/// <summary>
		/// Invokes an asynchronous command with a HordeRpcClient instance
		/// </summary>
		/// <param name="Func">The function to execute</param>
		/// <param name="Context">Context for the call, for debugging</param>
		/// <param name="CancellationToken">Cancellation token for the call</param>
		/// <returns>Response from the call</returns>
		public async Task<T> InvokeAsync<T>(Func<HordeRpc.HordeRpcClient, Task<T>> Func, RpcContext Context, CancellationToken CancellationToken)
		{
			for (int Attempt = 0; ;Attempt++)
			{
				// Attempt the RPC call
				using (IRpcClientRef ClientRef = await GetClientRef(Context, CancellationToken))
				{
					try
					{
						return await Func(ClientRef.Client);
					}
					catch (Exception Ex)
					{
						// Otherwise check if we can try again
						if (Attempt >= RetryTimes.Length)
						{
							Logger.LogError("Max number of retry attempts reached");
							throw;
						}

						// Check if the user requested cancellation
						if (CancellationToken.IsCancellationRequested)
						{
							Logger.LogInformation("Cancellation of gRPC call requested");
							throw;
						}

						// Check we can retry it
						if (!CanRetry(Ex))
						{
							Logger.LogError(Ex, "Exception during RPC: {Message}", Ex.Message);
							throw;
						}

						RpcException? RpcEx = Ex as RpcException;
						if (RpcEx != null && RpcEx.StatusCode == StatusCode.Unavailable)
						{
							Logger.LogInformation(Ex, "Failure #{Attempt} during gRPC call (service unavailable). Retrying...", Attempt + 1);
						}
						else
						{
							Logger.LogError(Ex, "Failure #{Attempt} during gRPC call.", Attempt + 1);
						}
					}
				}

				// Wait before retrying
				await Task.Delay(RetryTimes[Attempt]);
			}
		}

		/// <summary>
		/// Determines if the given exception should be retried
		/// </summary>
		/// <param name="Ex"></param>
		/// <returns></returns>
		public static bool CanRetry(Exception Ex)
		{
			RpcException? RpcEx = Ex as RpcException;
			if(RpcEx != null)
			{
				return RpcEx.StatusCode == StatusCode.Cancelled || RpcEx.StatusCode == StatusCode.Unavailable || RpcEx.StatusCode == StatusCode.DataLoss /* Interrupted streaming call */;
			}
			else
			{
				return false;
			}
		}

		/// <summary>
		/// Obtains a client reference and executes a unary RPC
		/// </summary>
		/// <typeparam name="T">Return type from the call</typeparam>
		/// <param name="Func">Method to execute with the RPC instance</param>
		/// <param name="Context">Context for the call, for debugging</param>
		/// <param name="CancellationToken">Cancellation token for the call</param>
		/// <returns>Response from the call</returns>
		public Task<T> InvokeAsync<T>(Func<HordeRpc.HordeRpcClient, AsyncUnaryCall<T>> Func, RpcContext Context, CancellationToken CancellationToken)
		{
			return InvokeAsync(async (x) => await Func(x), Context, CancellationToken);
		}

		/// <summary>
		/// Background task to monitor the server state
		/// </summary>
		/// <returns>Async task</returns>
		async Task ExecuteAsync()
		{
			// Counter for the connection number. Used to track log messages through async threads.
			int ConnectionId = 0;

			// This task source is marked as completed once a RPC connection receives a notification from the server that it's shutting down.
			// Another connection will immediately be allocated for other connection requests to use, and the load balancer should route us to
			// the new server.
			TaskCompletionSource<bool>? ReconnectTaskSource = null;

			// List of connection handling async tasks we're tracking
			List<Task> Tasks = new List<Task>();
			while (Tasks.Count > 0 || !StoppingTaskSource.Task.IsCompleted)
			{
				// Get the task indicating we want a new connection (or create a new one)
				Task? ReconnectTask = null;
				if (!StoppingTaskSource.Task.IsCompleted)
				{
					if (ReconnectTaskSource == null || ReconnectTaskSource.Task.IsCompleted)
					{
						// Need to avoid lambda capture of the reconnect task source
						int NewConnectionId = ++ConnectionId;
						TaskCompletionSource<bool> NewReconnectTaskSource = new TaskCompletionSource<bool>();
						Tasks.Add(Task.Run(() => HandleConnectionAsync(NewConnectionId, NewReconnectTaskSource)));
						ReconnectTaskSource = NewReconnectTaskSource;
					}
					ReconnectTask = ReconnectTaskSource.Task;
				}

				// Build a list of tasks to wait for
				List<Task> WaitTasks = new List<Task>(Tasks);
				if (ReconnectTask != null)
				{
					WaitTasks.Add(ReconnectTask);
				}
				await Task.WhenAny(WaitTasks);

				// Remove any complete tasks, logging any exceptions
				for (int Idx = 0; Idx < Tasks.Count; Idx++)
				{
					Task Task = Tasks[Idx];
					if(Task.IsCompleted)
					{
						await Task;
						Tasks.RemoveAt(Idx--);
					}
				}
			}
		}

		/// <summary>
		/// Background task to monitor the server state
		/// </summary>
		/// <param name="ConnectionId">The connection id</param>
		/// <param name="ReconnectTaskSource">Task source for reconnecting</param>
		/// <returns>Async task</returns>
		async Task HandleConnectionAsync(int ConnectionId, TaskCompletionSource<bool> ReconnectTaskSource)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("RpcConnection {ConnectionId}").StartActive();

			Logger.LogInformation("Connection {ConnectionId}: Creating connection", ConnectionId);
			try
			{
				await HandleConnectionInternalAsync(ConnectionId, ReconnectTaskSource);
			}
			catch (RpcException Ex) when (Ex.StatusCode == StatusCode.Unavailable)
			{
				Logger.LogInformation("Connection {ConnectionId}: Rpc service is unavailable. Reconnecting.", ConnectionId);
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Connection {ConnectionId}: Exception handling connection: {Message}", ConnectionId, Ex.Message);
			}
			finally
			{
				TriggerReconnect(ReconnectTaskSource);
			}
		}

		/// <summary>
		/// Background task to monitor the server state
		/// </summary>
		/// <returns>Async task</returns>
		async Task HandleConnectionInternalAsync(int ConnectionId, TaskCompletionSource<bool> ReconnectTaskSource)
		{
			using (GrpcChannel Channel = CreateGrpcChannel())
			{
				HordeRpc.HordeRpcClient Client = new HordeRpc.HordeRpcClient(Channel);//.Intercept(new ClientTracingInterceptor(GlobalTracer.Instance)));

				RpcSubConnection? SubConnection = null;
				try
				{
					while (!ReconnectTaskSource.Task.IsCompleted && !StoppingTaskSource.Task.IsCompleted)
					{
						// Client HAS TO BE the one to end the call, by closing the stream. The server must keep receiving requests from us until it does.
						using (AsyncDuplexStreamingCall<QueryServerStateRequest, QueryServerStateResponse> Call = Client.QueryServerStateV2())
						{
							// Send the name of this agent
							QueryServerStateRequest Request = new QueryServerStateRequest();
							Request.Name = Dns.GetHostName();
							await Call.RequestStream.WriteAsync(Request);

							// Read the server info
							Task<bool> MoveNextTask = Call.ResponseStream.MoveNext();
							if (!await MoveNextTask)
							{
								Logger.LogError("Connection {ConnectionId}: No response from server", ConnectionId);
								break;
							}

							// Read the server response
							QueryServerStateResponse Response = Call.ResponseStream.Current;
							if (!Response.Stopping)
							{
								// The first time we connect, log the server name and create the subconnection
								if (SubConnection == null)
								{
									Logger.LogInformation("Connection {ConnectionId}: Connected to rpc server {ServerName}", ConnectionId, Response.Name);
									SubConnection = new RpcSubConnection(ConnectionId, Response.Name, Channel, Client, Logger);
									SubConnectionTaskSource.SetResult(SubConnection);
								}

								// Wait for the StoppingTask token to be set, or the server to inform that it's shutting down
								MoveNextTask = Call.ResponseStream.MoveNext();
								await Task.WhenAny(StoppingTaskSource.Task, Task.Delay(TimeSpan.FromSeconds(45.0)), MoveNextTask);

								// Update the response
								if (MoveNextTask.IsCompleted && MoveNextTask.Result)
								{
									Response = Call.ResponseStream.Current;
								}
							}

							// If the server is stopping, start reconnecting
							if (Response.Stopping)
							{
								Logger.LogInformation("Connection {ConnectionId}: Server is stopping. Triggering reconnect.", ConnectionId);
								TriggerReconnect(ReconnectTaskSource);
							}

							// Close the request stream
							await Call.RequestStream.CompleteAsync();

							// Wait for the server to finish posting responses
							while (await MoveNextTask)
							{
								Response = Call.ResponseStream.Current;
								MoveNextTask = Call.ResponseStream.MoveNext();
							}
						}
					}
					Logger.LogInformation("Connection {ConnectionId}: Closing connection to rpc server", ConnectionId);
				}
				finally
				{
					if (SubConnection != null)
					{
						await SubConnection.DisposeAsync();
					}
				}

				// Wait a few seconds before finishing
				await Task.Delay(TimeSpan.FromSeconds(5.0));
			}
		}

		/// <summary>
		/// Clears the current subconnection and triggers a reconnect
		/// </summary>
		/// <param name="ReconnectTaskSource"></param>
		void TriggerReconnect(TaskCompletionSource<bool> ReconnectTaskSource)
		{
			if (!ReconnectTaskSource.Task.IsCompleted)
			{
				// First reset the task source, so that nothing new starts to use the current connection
				if (SubConnectionTaskSource.Task.IsCompleted)
				{
					SubConnectionTaskSource = new TaskCompletionSource<RpcSubConnection>();
				}

				// Now trigger another connection
				ReconnectTaskSource.SetResult(true);
			}
		}

		/// <summary>
		/// Dispose of this connection
		/// </summary>
		/// <returns>Async task</returns>
		public async ValueTask DisposeAsync()
		{
			if (BackgroundTask != null)
			{
				StoppingTaskSource.TrySetResult(true);

				await BackgroundTask;
				BackgroundTask = null!;
			}
		}

		public static IRpcConnection Create(Func<GrpcChannel> CreateGrpcChannel, ILogger Logger)
		{
			return new RpcConnection(CreateGrpcChannel, Logger);
		}
	}
}
