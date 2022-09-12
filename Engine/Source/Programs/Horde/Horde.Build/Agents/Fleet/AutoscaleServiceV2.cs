// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Horde.Build.Agents.Leases;
using Horde.Build.Agents.Pools;
using Horde.Build.Jobs;
using Horde.Build.Jobs.Graphs;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using OpenTracing;
using OpenTracing.Util;
using StatsdClient;

namespace Horde.Build.Agents.Fleet
{
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Service for managing the autoscaling of agent pools
	/// </summary>
	public sealed class AutoscaleServiceV2 : IHostedService, IDisposable
	{
		private readonly IAgentCollection _agentCollection;
		private readonly IGraphCollection _graphCollection;
		private readonly IJobCollection _jobCollection;
		private readonly ILeaseCollection _leaseCollection;
		private readonly IPoolCollection _poolCollection;
		private readonly StreamService _streamService;
		private readonly IFleetManager _fleetManager;
		private readonly IDogStatsd _dogStatsd;
		private readonly IClock _clock;
		private readonly ITicker _ticker;
		private readonly ITicker _tickerHighFrequency;
		private readonly TimeSpan _defaultScaleOutCooldown;
		private readonly TimeSpan _defaultScaleInCooldown;
		private readonly ILogger<AutoscaleServiceV2> _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public AutoscaleServiceV2(
			IAgentCollection agentCollection,
			IGraphCollection graphCollection,
			IJobCollection jobCollection,
			ILeaseCollection leaseCollection,
			IPoolCollection poolCollection,
			StreamService streamService,
			IFleetManager fleetManager,
			IDogStatsd dogStatsd,
			IClock clock,
			IOptions<ServerSettings> settings,
			ILogger<AutoscaleServiceV2> logger)
		{
			_agentCollection = agentCollection;
			_graphCollection = graphCollection;
			_jobCollection = jobCollection;
			_leaseCollection = leaseCollection;
			_poolCollection = poolCollection;
			_streamService = streamService;
			_fleetManager = fleetManager;
			_dogStatsd = dogStatsd;
			_clock = clock;
			_ticker = clock.AddSharedTicker<AutoscaleServiceV2>(TimeSpan.FromMinutes(5.0), TickLeaderAsync, logger);
			_tickerHighFrequency = clock.AddSharedTicker("AutoscaleServiceV2.TickHighFrequency", TimeSpan.FromSeconds(30), TickHighFrequencyAsync, logger);
			_logger = logger;
			_defaultScaleOutCooldown = TimeSpan.FromSeconds(settings.Value.AgentPoolScaleOutCooldownSeconds);
			_defaultScaleInCooldown = TimeSpan.FromSeconds(settings.Value.AgentPoolScaleInCooldownSeconds);
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken cancellationToken)
		{
			return Task.WhenAll(_ticker.StartAsync(), _tickerHighFrequency.StartAsync());
		}

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken cancellationToken)
		{
			return Task.WhenAll(_ticker.StopAsync(), _tickerHighFrequency.StopAsync());
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_ticker.Dispose();
			_tickerHighFrequency.Dispose();
		}

		internal async ValueTask TickLeaderAsync(CancellationToken stoppingToken)
		{
			_logger.LogInformation("Autoscaling pools...");
			Stopwatch stopwatch = Stopwatch.StartNew();
			using IScope _ = GlobalTracer.Instance.BuildSpan("AutoscaleService.TickAsync").StartActive();
			
			List<PoolSizeData> input = await GetPoolSizeDataAsync();
			List<PoolSizeData> output = await CalculateDesiredPoolSizesAsync(input);
			await ResizePoolsAsync(output);

			stopwatch.Stop();
			_logger.LogInformation("Autoscaling pools took {ElapsedTime} ms", stopwatch.ElapsedMilliseconds);
		}

		internal async ValueTask TickHighFrequencyAsync(CancellationToken stoppingToken)
		{
			_logger.LogInformation("Autoscaling pools (high frequency)...");
			Stopwatch stopwatch = Stopwatch.StartNew();
			using IScope _ = GlobalTracer.Instance.BuildSpan("AutoscaleService.TickHighFrequency").StartActive();
			
			// TODO: Re-enable high frequency scaling (only used for experimental scaling of remote execution agents at the moment)
			await Task.Delay(0, stoppingToken);

			stopwatch.Stop();
			_logger.LogInformation("Autoscaling pools (high frequency) took {ElapsedTime} ms", stopwatch.ElapsedMilliseconds);
		}

		internal async Task<List<PoolSizeData>> CalculateDesiredPoolSizesAsync(List<PoolSizeData> poolSizeDatas)
		{
			List<PoolSizeData> result = new();
			foreach (PoolSizeData psd in poolSizeDatas)
			{
				result.Add((await CreatePoolSizeStrategy(psd.Pool).CalcDesiredPoolSizesAsync(new List<PoolSizeData> { psd }))[0]);
			}

			return result;
		}

		internal async Task<List<PoolSizeData>> GetPoolSizeDataAsync()
		{
			List<IAgent> agents = await _agentCollection.FindAsync(status: AgentStatus.Ok, enabled: true);
			List<IAgent> GetAgentsInPool(PoolId poolId) => agents.FindAll(a => a.GetPools().Any(p => p == poolId));
			List<IPool> pools = await _poolCollection.GetAsync();

			return pools.Select(pool => new PoolSizeData(pool, GetAgentsInPool(pool.Id), null)).ToList();
		}

		internal async Task ResizePoolsAsync(List<PoolSizeData> poolSizeDatas)
		{
			foreach (PoolSizeData poolSizeData in poolSizeDatas.OrderByDescending(x => x.Agents.Count))
			{
				IPool pool = poolSizeData.Pool;

				if (!pool.EnableAutoscaling || poolSizeData.DesiredAgentCount == null)
				{
					continue;
				}

				int currentAgentCount = poolSizeData.Agents.Count;
				int desiredAgentCount = poolSizeData.DesiredAgentCount.Value;
				int deltaAgentCount = desiredAgentCount - currentAgentCount;

				_logger.LogInformation("{PoolName,-48} Current={Current,4} Target={Target,4} Delta={Delta,4} Status={Status}", pool.Name, currentAgentCount, desiredAgentCount, deltaAgentCount, poolSizeData.StatusMessage);
				
				try
				{
					using IScope scope = GlobalTracer.Instance.BuildSpan("ScalingPool").StartActive();
					scope.Span.SetTag("poolName", pool.Name);
					scope.Span.SetTag("current", currentAgentCount);
					scope.Span.SetTag("desired", desiredAgentCount);
					scope.Span.SetTag("delta", deltaAgentCount);

					if (deltaAgentCount > 0)
					{
						TimeSpan scaleOutCooldown = pool.ScaleOutCooldown ?? _defaultScaleOutCooldown;
						bool isCoolingDown = pool.LastScaleUpTime != null && pool.LastScaleUpTime + scaleOutCooldown > _clock.UtcNow;
						scope.Span.SetTag("isCoolingDown", isCoolingDown);
						if (!isCoolingDown)
						{
							await _fleetManager.ExpandPoolAsync(pool, poolSizeData.Agents, deltaAgentCount);
							await _poolCollection.TryUpdateAsync(pool, lastScaleUpTime: _clock.UtcNow);
						}
						else
						{
							TimeSpan? cooldownTimeLeft = pool.LastScaleUpTime + _defaultScaleOutCooldown - _clock.UtcNow;
							_logger.LogDebug("Cannot scale out {PoolName}, it's cooling down for another {TimeLeft} secs", pool.Name, cooldownTimeLeft?.TotalSeconds);
						}
					}

					if (deltaAgentCount < 0)
					{
						TimeSpan scaleInCooldown = pool.ScaleInCooldown ?? _defaultScaleInCooldown;
						bool isCoolingDown = pool.LastScaleDownTime != null && pool.LastScaleDownTime + scaleInCooldown > _clock.UtcNow;
						scope.Span.SetTag("isCoolingDown", isCoolingDown);
						if (!isCoolingDown)
						{
							await _fleetManager.ShrinkPoolAsync(pool, poolSizeData.Agents, -deltaAgentCount);
							await _poolCollection.TryUpdateAsync(pool, lastScaleDownTime: _clock.UtcNow);
						}
						else
						{
							TimeSpan? cooldownTimeLeft = pool.LastScaleDownTime + _defaultScaleInCooldown - _clock.UtcNow;
							_logger.LogDebug("Cannot scale in {PoolName}, it's cooling down for another {TimeLeft} secs", pool.Name, cooldownTimeLeft?.TotalSeconds);
						}
					}
				}
				catch (Exception ex)
				{
					_logger.LogError(ex, "Failed to scale {PoolName}:\n{Exception}", pool.Name, ex);
					continue;
				}

				_dogStatsd.Gauge("agentpools.autoscale.target", desiredAgentCount, tags: new []{"pool:" + pool.Name});
				_dogStatsd.Gauge("agentpools.autoscale.current", currentAgentCount, tags: new []{"pool:" + pool.Name});
			}
		}

		/// <summary>
		/// Instantiate a sizing strategy for the given pool
		/// Can resolve either from the list of strategies or the old legacy way
		/// </summary>
		/// <param name="pool">Pool to use</param>
		/// <returns>A pool sizing strategy with parameters set</returns>
		/// <exception cref="ArgumentException"></exception>
		public IPoolSizeStrategy CreatePoolSizeStrategy(IPool pool)
		{
	
			IEnumerable<string> GetPropValues(string name)
			{
				return name switch
				{
					"dayOfWeek" => new List<string> { _clock.UtcNow.DayOfWeek.ToString().ToLower() },
					_ => Array.Empty<string>()
				};
			}

			if (pool.SizeStrategies.Count > 0)
			{
				foreach (PoolSizeStrategyInfo info in pool.SizeStrategies)
				{
					if (info.Condition == null || info.Condition.Evaluate(GetPropValues))
					{
						switch (info.Type)
						{
							case PoolSizeStrategy.JobQueue:
								JobQueueSettings? settings = JsonSerializer.Deserialize<JobQueueSettings>(info.Config);
								if (settings == null) throw new ArgumentException("Unable to deserialize pool sizing config");
								return new JobQueueStrategy(_jobCollection, _graphCollection, _streamService, _clock, settings);
							
							case PoolSizeStrategy.LeaseUtilization:
								// No settings for lease utilization strategy
								return new LeaseUtilizationStrategy(_agentCollection, _poolCollection, _leaseCollection, _clock);
							
							case PoolSizeStrategy.NoOp:
								return new NoOpPoolSizeStrategy();
							
							default:
								throw new ArgumentException("Invalid pool size strategy type " + info.Type);
						}
					}
				}
			}

			// These is the legacy way of creating and configuring strategies (list-based approach above is preferred)
			switch (pool.SizeStrategy)
			{
				case PoolSizeStrategy.JobQueue:
					return new JobQueueStrategy(_jobCollection, _graphCollection, _streamService, _clock, pool.JobQueueSettings);
				case PoolSizeStrategy.LeaseUtilization:
					return new LeaseUtilizationStrategy(_agentCollection, _poolCollection, _leaseCollection, _clock);
				case PoolSizeStrategy.NoOp:
					return new NoOpPoolSizeStrategy();
				default:
					throw new ArgumentException("Unknown pool size strategy " + pool.SizeStrategy);
			}
		}
	}
}
