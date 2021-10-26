// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Interceptors;
using HordeServer.Utilities;
using StackExchange.Redis;
using System.Diagnostics;

namespace HordeServer.Services
{
	/// <summary>
	/// Service containing an async task that allows long polling operations to complete early if the server is shutting down
	/// </summary>
	public sealed class LifetimeService : IDisposable
	{
		/// <summary>
		/// Writer for log output
		/// </summary>
		ILogger<LifetimeService> Logger;

		/// <summary>
		/// Task source for the server stopping
		/// </summary>
		TaskCompletionSource<bool> StoppingTaskCompletionSource;

		/// <summary>
		/// Task source for the server stopping
		/// </summary>
		TaskCompletionSource<bool> PreStoppingTaskCompletionSource;

		/// <summary>
		/// Registration token for the stopping event
		/// </summary>
		CancellationTokenRegistration Registration;
		
		/// <summary>
		/// Singleton instance of the database service
		/// </summary>
		DatabaseService DatabaseService;
		
		/// <summary>
		/// The Redis database
		/// </summary>
		IDatabase RedisDb;
		
		/// <summary>
		/// Singleton instance of the request tracker service
		/// </summary>
		RequestTrackerService RequestTrackerService;
		
		/*
		/// <summary>
		/// Max time to wait for any outstanding requests to finish
		/// </summary>
		readonly TimeSpan RequestGracefulTimeout = TimeSpan.FromMinutes(5);
		
		/// <summary>
		/// Initial delay before attempting the shutdown. This to ensure any load balancers/ingress will detect
		/// the server is unavailable to serve new requests in the event of no outstanding requests to wait for.
		/// </summary>
		readonly TimeSpan InitialStoppingDelay = TimeSpan.FromSeconds(35);
		*/

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Lifetime">Application lifetime interface</param>
		/// <param name="DatabaseService">Database singleton service</param>
		/// <param name="RedisDb">Redis singleton service</param>
		/// <param name="RequestTrackerService">Request tracker singleton service</param>
		/// <param name="Logger">Logging interface</param>
		public LifetimeService(IHostApplicationLifetime Lifetime, DatabaseService DatabaseService, IDatabase RedisDb, RequestTrackerService RequestTrackerService, ILogger<LifetimeService> Logger)
		{
			this.DatabaseService = DatabaseService;
			this.RedisDb = RedisDb;
			this.RequestTrackerService = RequestTrackerService;
			this.Logger = Logger;
			this.StoppingTaskCompletionSource = new TaskCompletionSource<bool>();
			this.PreStoppingTaskCompletionSource = new TaskCompletionSource<bool>();
			this.Registration = Lifetime.ApplicationStopping.Register(ApplicationStopping);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Registration.Dispose();
		}

		/// <summary>
		/// Callback for the application stopping
		/// </summary>
		void ApplicationStopping()
		{
			Logger.LogInformation("SIGTERM signal received");
			IsPreStopping = true;
			IsStopping = true;

			PreStoppingTaskCompletionSource.TrySetResult(true);
			StoppingTaskCompletionSource.TrySetResult(true);

			int ShutdownDelayMs = 30 * 1000;
			Logger.LogInformation($"Delaying shutdown by sleeping {ShutdownDelayMs} ms...");
			Thread.Sleep(ShutdownDelayMs);
			Logger.LogInformation($"Server process now shutting down...");
			
			/*
			if (PreStoppingTaskCompletionSource.TrySetResult(true))
			{
				
				WaitAndTriggerStoppingTask = Task.Run(() => ExecStoppingTask());
				
				Logger.LogInformation("App is stopping. Waiting an initial {InitialDelay} secs before waiting on any requests...", (int)InitialStoppingDelay.TotalSeconds);
				Thread.Sleep(InitialStoppingDelay);
				Logger.LogInformation("Blocking shutdown for up to {MaxGraceTimeout} secs until all request have finished...", (int)RequestGracefulTimeout.TotalSeconds);
				
				DateTime StartTime = DateTime.UtcNow;
				do
				{
					RequestTrackerService.LogRequestsInProgress();
					Thread.Sleep(5000);
				}
				while (DateTime.UtcNow < StartTime + RequestGracefulTimeout && RequestTrackerService.GetRequestsInProgress().Count > 0);

				if (RequestTrackerService.GetRequestsInProgress().Count == 0)
				{
					Logger.LogInformation("All open requests finished gracefully after {TimeTaken} secs", (DateTime.UtcNow - StartTime).TotalSeconds);	
				}
				else
				{
					Logger.LogInformation("One or more requests did not finish within the grace period of {TimeTaken} secs. Shutdown will now resume with risk of interrupting those requests!", (DateTime.UtcNow - StartTime).TotalSeconds);
					RequestTrackerService.LogRequestsInProgress();
				}
			}
			*/
		}

		async Task ExecStoppingTask()
		{
			await Task.Delay(TimeSpan.FromSeconds(15.0));

			Logger.LogInformation("Setting stopping event");
			IsStopping = true;
			StoppingTaskCompletionSource.TrySetResult(true);
		}

		/// <summary>
		/// Returns true if the server is stopping
		/// </summary>
		public bool IsStopping { get; private set; }

		/// <summary>
		/// Returns true if the server is stopping, but may not be removed from the load balancer yet
		/// </summary>
		public bool IsPreStopping { get; private set; }

		/// <summary>
		/// Gets an awaitable task for the server stopping
		/// </summary>
		public Task StoppingTask
		{
			get { return StoppingTaskCompletionSource.Task; }
		}
		
		/// <summary>
		/// Check if MongoDB can be reached
		/// </summary>
		/// <returns>True if communication works</returns>
		[SuppressMessage("Design", "CA1031:Do not catch general exception types")]
		public async Task<bool> IsMongoDbConnectionHealthy()
		{
			using CancellationTokenSource CancelSource = new CancellationTokenSource(10000);
			bool IsHealthy = false;
			try
			{
				await DatabaseService.Database.ListCollectionNamesAsync(null, CancelSource.Token);
				IsHealthy = true;
			}
			catch (Exception e)
			{
				Logger.LogError("MongoDB call failed during health check", e);
			}
			
			return IsHealthy;
		}
		
		/// <summary>
		/// Check if Redis can be reached
		/// </summary>
		/// <returns>True if communication works</returns>
		[SuppressMessage("Design", "CA1031:Do not catch general exception types")]
		public async Task<bool> IsRedisConnectionHealthy()
		{
			using CancellationTokenSource CancelSource = new CancellationTokenSource(10000);
			bool IsHealthy = false;
			try
			{
				string Key = "HordeLifetimeService-Health-Check";
				await RedisDb.StringSetAsync(Key, "ok");
				await RedisDb.StringGetAsync(Key);
				IsHealthy = true;
			}
			catch (Exception e)
			{
				Logger.LogError("Redis call failed during health check", e);
			}
			
			return IsHealthy;
		}
	}

	/// <summary>
	/// Intercept incoming gRPC calls and raise an abort exception if the server is shutting down
	///
	/// This is to avoid clients keeping connections to a server that is about to shut down.
	/// The client must handle the RPC exception raised and simply retry the call on a different gRPC channel/connection.
	/// By retrying the call on a new channel, it should hit a new valid server as the load balancer will not direct traffic
	/// to a server process shutting down.
	/// </summary>
	public class LifetimeGrpcInterceptor : Interceptor
	{
		private readonly LifetimeService LifetimeService;
		private readonly ILogger<LifetimeGrpcInterceptor> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="LifetimeService"></param>
		/// <param name="Logger"></param>
		public LifetimeGrpcInterceptor(LifetimeService LifetimeService, ILogger<LifetimeGrpcInterceptor> Logger)
		{
			this.LifetimeService = LifetimeService;
			this.Logger = Logger;
		}

		[SuppressMessage("Performance", "CA1822:Mark members as static")]
		[SuppressMessage("Usage", "CA1801:Review unused parameters")]
		private void CheckIfServerIsBeingStopped(ServerCallContext Context)
		{
			/*
			if (LifetimeService.IsStopping)
			{
				Logger.LogDebug("Refusing to serve {Method} as server process is being shut down.", Context.Method);
				throw new RpcException(new Status(StatusCode.Aborted, "Server is shutting down."));
			}
			*/
		}

		/// <inheritdoc />
		public override Task<TResponse> UnaryServerHandler<TRequest, TResponse>(TRequest Request, ServerCallContext Context, UnaryServerMethod<TRequest, TResponse> Continuation)
		{
			CheckIfServerIsBeingStopped(Context);
			return base.UnaryServerHandler(Request, Context, Continuation);
		}

		/// <inheritdoc />
		public override Task<TResponse> ClientStreamingServerHandler<TRequest, TResponse>(IAsyncStreamReader<TRequest> RequestStream, ServerCallContext Context, ClientStreamingServerMethod<TRequest, TResponse> Continuation)
		{
			CheckIfServerIsBeingStopped(Context);
			return base.ClientStreamingServerHandler(RequestStream, Context, Continuation);
		}

		/// <inheritdoc />
		public override Task ServerStreamingServerHandler<TRequest, TResponse>(TRequest Request, IServerStreamWriter<TResponse> ResponseStream, ServerCallContext Context, ServerStreamingServerMethod<TRequest, TResponse> Continuation)
		{
			CheckIfServerIsBeingStopped(Context);
			return base.ServerStreamingServerHandler(Request, ResponseStream, Context, Continuation);
		}

		/// <inheritdoc />
		public override Task DuplexStreamingServerHandler<TRequest, TResponse>(IAsyncStreamReader<TRequest> RequestStream, IServerStreamWriter<TResponse> ResponseStream, ServerCallContext Context, DuplexStreamingServerMethod<TRequest, TResponse> Continuation)
		{
			CheckIfServerIsBeingStopped(Context);
			return base.DuplexStreamingServerHandler(RequestStream, ResponseStream, Context, Continuation);
		}
	}
}