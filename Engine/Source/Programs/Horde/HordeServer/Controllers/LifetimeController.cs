// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Services;
using Microsoft.AspNetCore.Mvc;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for app lifetime related routes
	/// </summary>
	[ApiController]
	[Route("[controller]")]
	public class LifetimeController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the lifetime service
		/// </summary>
		LifetimeService LifetimeService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="LifetimeService">The lifetime service singleton</param>
		public LifetimeController(LifetimeService LifetimeService)
		{
			this.LifetimeService = LifetimeService;
		}

		/// <summary>
		/// Readiness check for Kubernetes
		/// If this return a non-successful HTTP response, Kubernetes will remove it from the load balancer,
		/// preventing it from serving traffic.
		/// </summary>
		/// <returns>Ok if app is not stopping and all databases can be reached</returns>
		[HttpGet]
		[Route("/health/ready")]
		public Task<ActionResult> K8sReadinessProbe()
		{
			// Disabled for now
			bool IsRunning = true;//!LifetimeService.IsStopping;
			bool IsMongoDBHealthy = true; //await LifetimeService.IsMongoDbConnectionHealthy();
			bool IsRedisHealthy = true; //await LifetimeService.IsRedisConnectionHealthy();
			int StatusCode = IsRunning && IsMongoDBHealthy && IsRedisHealthy ? 200 : 500;

			string Content = $"IsRunning={IsRunning}\n";
			Content += $"IsMongoDBHealthy={IsMongoDBHealthy}\n";
			Content += $"IsRedisHealthy={IsRedisHealthy}\n";

			return Task.FromResult<ActionResult>(new ContentResult { ContentType = "text/plain", StatusCode = StatusCode, Content = Content });
		}
		
		/// <summary>
		/// Liveness check for Kubernetes
		/// If this return a non-successful HTTP response, Kubernetes will kill the pod and restart it
		/// </summary>
		/// <returns>Ok if app is not stopping and all databases can be reached</returns>
		[HttpGet]
		[Route("/health/live")]
		public ActionResult K8sLivenessProbe()
		{
			return new ContentResult { ContentType = "text/plain", StatusCode = 200, Content = "ok" };
		}
	}
}
