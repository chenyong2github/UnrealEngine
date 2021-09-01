// Copyright Epic Games, Inc. All Rights Reserved.

using Grpc.Core;
using Microsoft.Extensions.Logging;
using System.Threading.Tasks;
using Grpc.Health.V1;
using static Grpc.Health.V1.HealthCheckResponse.Types;

namespace HordeServer.Services
{
	/// <summary>
	/// Implements the gRPC health checking protocol
	/// See https://github.com/grpc/grpc/blob/master/doc/health-checking.md for details
	/// </summary>
	public class HealthService : Health.HealthBase
	{
		/// <summary>
		/// The application lifetime interface
		/// </summary>
		LifetimeService LifetimeService;

		/// <summary>
		/// Writer for log output
		/// </summary>
		ILogger<HealthService> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="LifetimeService">The application lifetime</param>
		/// <param name="Logger">Log writer</param>
		public HealthService(LifetimeService LifetimeService, ILogger<HealthService> Logger)
		{
			this.LifetimeService = LifetimeService;
			this.Logger = Logger;
		}

		/// <summary>
		/// Return server status, primarily intended for load balancers to decide if traffic should be routed to this process
		/// The supplied service name is currently ignored.
		///
		/// For example, the gRPC health check for AWS ALB will pick up and react to the gRPC status code returned.
		/// </summary>
		/// <param name="Request">Empty placeholder request (for now)</param>
		/// <param name="Context">Context for the call</param>
		/// <returns>Return status code 'unavailable' if stopping</returns>
		public override Task<HealthCheckResponse> Check(HealthCheckRequest Request, ServerCallContext Context)
		{
			ServingStatus Status = ServingStatus.Serving;
			
			bool IsStopping = LifetimeService.IsPreStopping || LifetimeService.IsStopping;
			if (IsStopping)
			{
				Context.Status = new Status(StatusCode.Unavailable, "Server is stopping");
				Status = ServingStatus.NotServing;
			}
			 
			return Task.FromResult(new HealthCheckResponse {Status = Status});
		}

		
		/// <summary>
		/// Stream the server health status (not implemented)
		/// </summary>
		/// <param name="Request"></param>
		/// <param name="ResponseStream"></param>
		/// <param name="Context"></param>
		/// <returns></returns>
		public override Task Watch(HealthCheckRequest Request, IServerStreamWriter<HealthCheckResponse> ResponseStream, ServerCallContext Context)
		{
			return Task.FromException(new RpcException(new Status(StatusCode.Unimplemented, "Watch() not implemented")));
		}
	}
}
