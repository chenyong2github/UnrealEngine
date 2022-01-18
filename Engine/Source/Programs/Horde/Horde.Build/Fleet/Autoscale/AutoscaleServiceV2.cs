// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using HordeCommon;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using OpenTracing;
using OpenTracing.Util;
using StatsdClient;

namespace Horde.Build.Fleet.Autoscale
{
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Service for managing the autoscaling of agent pools
	/// </summary>
	public sealed class AutoscaleServiceV2 : IHostedService, IDisposable
	{
		static readonly TimeSpan ShrinkPoolCoolDown = TimeSpan.FromMinutes(20.0);

		IPoolSizeStrategy PoolSizeStrategy;
		IAgentCollection AgentCollection;
		IPoolCollection PoolCollection;
		IFleetManager FleetManager;
		IDogStatsd DogStatsd;
		IClock Clock;
		ITicker Ticker;
		ILogger<AutoscaleServiceV2> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public AutoscaleServiceV2(IPoolSizeStrategy PoolSizeStrategy, IAgentCollection AgentCollection, IPoolCollection PoolCollection, IFleetManager FleetManager, IDogStatsd DogStatsd, IClock Clock, ILogger<AutoscaleServiceV2> Logger)
		{
			this.PoolSizeStrategy = PoolSizeStrategy;
			this.AgentCollection = AgentCollection;
			this.PoolCollection = PoolCollection;
			this.FleetManager = FleetManager;
			this.DogStatsd = DogStatsd;
			this.Clock = Clock;
			this.Ticker = Clock.AddSharedTicker<AutoscaleServiceV2>(TimeSpan.FromMinutes(5.0), TickLeaderAsync, Logger);
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken CancellationToken) => Ticker.StartAsync();

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken CancellationToken) => Ticker.StopAsync();

		/// <inheritdoc/>
		public void Dispose() => Ticker.Dispose();

		/// <inheritdoc/>
		async ValueTask TickLeaderAsync(CancellationToken StoppingToken)
		{
			DateTime UtcNow = Clock.UtcNow;

			Logger.LogInformation("Autoscaling pools using strategy {PoolSizeStrategyName}...", PoolSizeStrategy.Name);
			Stopwatch Stopwatch = Stopwatch.StartNew();
			using IScope _ = GlobalTracer.Instance.BuildSpan("AutoscaleService").StartActive();
			
			List<IAgent> Agents = await AgentCollection.FindAsync(Status: AgentStatus.Ok);
			List<IAgent> GetAgentsInPool(PoolId PoolId)
			{
				return Agents.FindAll(a => a.GetPools().Any(p => p == PoolId));
			}
			
			List<IPool> Pools = await PoolCollection.GetAsync();
			Dictionary<PoolId, PoolSizeData> PoolSizeDatas = Pools.ToDictionary(x => x.Id, x => new PoolSizeData(x, GetAgentsInPool(x.Id), null));

			await PoolSizeStrategy.CalcDesiredPoolSizesAsync(PoolSizeDatas);

			foreach (PoolSizeData PoolSizeData in PoolSizeDatas.Values.OrderByDescending(x => x.Agents.Count))
			{
				IPool Pool = PoolSizeData.Pool;

				if (!Pool.EnableAutoscaling || PoolSizeData.DesiredAgentCount == null) continue;

				int CurrentAgentCount = PoolSizeData.Agents.Count;
				int DesiredAgentCount = PoolSizeData.DesiredAgentCount.Value;
				int DeltaAgentCount = DesiredAgentCount - CurrentAgentCount;

				Logger.LogInformation("{PoolName,-48} Current={Current,4} Target={Target,4} Delta={Delta,4} Status={Status}", Pool.Name, CurrentAgentCount, DesiredAgentCount, DeltaAgentCount, PoolSizeData.StatusMessage);
				
				try
				{
					using IScope Scope = GlobalTracer.Instance.BuildSpan("Scaling pool").StartActive();
					Scope.Span.SetTag("poolName", Pool.Name);
					Scope.Span.SetTag("current", CurrentAgentCount);
					Scope.Span.SetTag("desired", DesiredAgentCount);
					Scope.Span.SetTag("delta", DeltaAgentCount);

					if (DeltaAgentCount > 0)
					{
						await FleetManager.ExpandPoolAsync(Pool, PoolSizeData.Agents, DeltaAgentCount);
						await PoolCollection.TryUpdateAsync(Pool, LastScaleUpTime: DateTime.UtcNow);
					}

					if (DeltaAgentCount < 0)
					{
						bool bShrinkIsOnCoolDown = Pool.LastScaleDownTime != null && Pool.LastScaleDownTime + ShrinkPoolCoolDown > DateTime.UtcNow;
						if (!bShrinkIsOnCoolDown)
						{
							await FleetManager.ShrinkPoolAsync(Pool, PoolSizeData.Agents, -DeltaAgentCount);
							await PoolCollection.TryUpdateAsync(Pool, LastScaleDownTime: DateTime.UtcNow);
						}
						else
						{
							Logger.LogDebug("Cannot shrink {PoolName} right now, it's on cool-down until {CoolDownTimeEnds}", Pool.Name, Pool.LastScaleDownTime + ShrinkPoolCoolDown);
						}
					}
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Failed to scale {PoolName}:\n{Exception}", Pool.Name, Ex);
					continue;
				}

				DogStatsd.Gauge("agentpools.autoscale.target", DesiredAgentCount, tags: new []{"pool:" + Pool.Name});
				DogStatsd.Gauge("agentpools.autoscale.current", CurrentAgentCount, tags: new []{"pool:" + Pool.Name});
			}
			
			Stopwatch.Stop();
			Logger.LogInformation("Autoscaling pools took {ElapsedTime} ms", Stopwatch.ElapsedMilliseconds);
		}
	}
}
