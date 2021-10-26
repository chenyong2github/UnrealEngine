// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using System;
using System.Threading;
using System.Threading.Tasks;
using StatsdClient;

namespace HordeServer.Services
{
	/// <summary>
	/// Periodically send metrics for the CLR and other services that cannot be collected on a per-request basis
	/// </summary>
	public class MetricService : TickedBackgroundService
	{
		/// <summary>
		/// DogStatsd instance
		/// </summary>
		private readonly IDogStatsd DogStatsd;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DogStatsd"></param>
		/// <param name="Logger">Logger instance</param>
		public MetricService(IDogStatsd DogStatsd, ILogger<MetricService> Logger) : base(TimeSpan.FromSeconds(20.0), Logger)
		{
			this.DogStatsd = DogStatsd;
		}

		/// <summary>
		/// Execute the background task
		/// </summary>
		/// <param name="StoppingToken">Cancellation token for the async task</param>
		/// <returns>Async task</returns>
		protected override Task TickAsync(CancellationToken StoppingToken)
		{
			ReportGcMetrics();
			ReportThreadMetrics();
			return Task.CompletedTask;
		}

		private void ReportThreadMetrics()
		{
			ThreadPool.GetMaxThreads(out int MaxWorkerThreads, out int MaxIoThreads);
			ThreadPool.GetAvailableThreads(out int FreeWorkerThreads, out int FreeIoThreads);
			ThreadPool.GetMinThreads(out int MinWorkerThreads, out int MinIoThreads);

			int BusyIoThreads = MaxIoThreads - FreeIoThreads;
			int BusyWorkerThreads = MaxWorkerThreads - FreeWorkerThreads;
			
			DogStatsd.Gauge("horde.clr.threadpool.io.max", MaxIoThreads);
			DogStatsd.Gauge("horde.clr.threadpool.io.min", MinIoThreads);
			DogStatsd.Gauge("horde.clr.threadpool.io.free", FreeIoThreads);
			DogStatsd.Gauge("horde.clr.threadpool.io.busy", BusyIoThreads);
			
			DogStatsd.Gauge("horde.clr.threadpool.worker.max", MaxWorkerThreads);
			DogStatsd.Gauge("horde.clr.threadpool.worker.min", MinWorkerThreads);
			DogStatsd.Gauge("horde.clr.threadpool.worker.free", FreeWorkerThreads);
			DogStatsd.Gauge("horde.clr.threadpool.worker.busy", BusyWorkerThreads);
		}

		private void ReportGcMetrics()
		{
			GCMemoryInfo GcMemoryInfo = GC.GetGCMemoryInfo();

			DogStatsd.Gauge("horde.clr.gc.totalMemory", GC.GetTotalMemory(false));
			DogStatsd.Gauge("horde.clr.gc.totalAllocated", GC.GetTotalAllocatedBytes());
			DogStatsd.Gauge("horde.clr.gc.heapSize", GcMemoryInfo.HeapSizeBytes);
			DogStatsd.Gauge("horde.clr.gc.fragmented", GcMemoryInfo.FragmentedBytes);
			DogStatsd.Gauge("horde.clr.gc.memoryLoad", GcMemoryInfo.MemoryLoadBytes);
			DogStatsd.Gauge("horde.clr.gc.totalAvailableMemory", GcMemoryInfo.TotalAvailableMemoryBytes);
			DogStatsd.Gauge("horde.clr.gc.highMemoryLoadThreshold", GcMemoryInfo.HighMemoryLoadThresholdBytes);
		}
	}
}
