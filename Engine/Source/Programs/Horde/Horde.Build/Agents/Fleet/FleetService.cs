// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
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
using Horde.Build.Server;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.Extensions.Caching.Memory;
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
	public sealed class FleetService : IHostedService, IDisposable
	{
		/// <summary>
		/// Max number of auto-scaling calculations to be done concurrently (sizing calculations and fleet manager calls)
		/// </summary>
		private const int MaxParallelTasks = 10;
		
		private readonly IAgentCollection _agentCollection;
		private readonly IGraphCollection _graphCollection;
		private readonly IJobCollection _jobCollection;
		private readonly ILeaseCollection _leaseCollection;
		private readonly IPoolCollection _poolCollection;
		private readonly IDowntimeService _downtimeService;
		private readonly StreamService _streamService;
		private readonly IDogStatsd _dogStatsd;
		private readonly IFleetManagerFactory _fleetManagerFactory;
		private readonly IClock _clock;
		private readonly IMemoryCache _cache;
		private readonly ITicker _ticker;
		private readonly ITicker _tickerHighFrequency;
		private readonly TimeSpan _defaultScaleOutCooldown;
		private readonly TimeSpan _defaultScaleInCooldown;
		private readonly IOptions<ServerSettings> _settings;
		private readonly ILogger<FleetService> _logger;
		
		/// <summary>
		/// Constructor
		/// </summary>
		public FleetService(
			IAgentCollection agentCollection,
			IGraphCollection graphCollection,
			IJobCollection jobCollection,
			ILeaseCollection leaseCollection,
			IPoolCollection poolCollection,
			IDowntimeService downtimeService,
			StreamService streamService,
			IDogStatsd dogStatsd,
			IFleetManagerFactory fleetManagerFactory,
			IClock clock,
			IMemoryCache cache,
			IOptions<ServerSettings> settings,
			ILogger<FleetService> logger)
		{
			_agentCollection = agentCollection;
			_graphCollection = graphCollection;
			_jobCollection = jobCollection;
			_leaseCollection = leaseCollection;
			_poolCollection = poolCollection;
			_downtimeService = downtimeService;
			_streamService = streamService;
			_dogStatsd = dogStatsd;
			_fleetManagerFactory = fleetManagerFactory;
			_clock = clock;
			_cache = cache;
			_logger = logger;
			_ticker = clock.AddSharedTicker<FleetService>(TimeSpan.FromSeconds(30), TickLeaderAsync, _logger);
			_tickerHighFrequency = clock.AddSharedTicker("FleetService.TickHighFrequency", TimeSpan.FromSeconds(30), TickHighFrequencyAsync, _logger);
			_settings = settings;
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
			ISpan span = GlobalTracer.Instance.BuildSpan("FleetService.Tick").Start();
			try
			{
				if (_downtimeService.IsDowntimeActive)
				{
					span.SetTag("IsDowntimeActive", _downtimeService.IsDowntimeActive);
					return;
				}
				
				List<PoolSizeResult> input = await GetPoolSizeDataAsync();
				List<PoolSizeResult> output = await CalculatePoolSizesAsync(input, stoppingToken);
				await ResizePoolsAsync(output, stoppingToken);
			}
			finally
			{
				span.Finish();
			}
		}

		internal async ValueTask TickHighFrequencyAsync(CancellationToken stoppingToken)
		{
			ISpan span = GlobalTracer.Instance.BuildSpan("FleetService.TickHighFrequency").Start();
			try
			{
				if (_downtimeService.IsDowntimeActive)
				{
					span.SetTag("IsDowntimeActive", _downtimeService.IsDowntimeActive);
					return;
				}
				
				// TODO: Re-enable high frequency scaling (only used for experimental scaling of remote execution agents at the moment)
				await Task.Delay(0, stoppingToken);
			}
			finally
			{
				span.Finish();
			}
		}

		internal async Task<List<PoolSizeResult>> CalculatePoolSizesAsync(List<PoolSizeResult> poolSizeDatas, CancellationToken cancellationToken)
		{
			ISpan span = GlobalTracer.Instance.BuildSpan("FleetService.CalculatePoolSizes").WithTag("MaxParallelTasks", MaxParallelTasks).Start();
			ConcurrentQueue<PoolSizeResult> results = new ();
			
			try
			{
				ParallelOptions options = new () { MaxDegreeOfParallelism = MaxParallelTasks, CancellationToken = cancellationToken };
				await Parallel.ForEachAsync(poolSizeDatas, options, async (input, innerCt) =>
				{
					try
					{
						IPoolSizeStrategy sizeStrategy = CreatePoolSizeStrategy(input.Pool);
						PoolSizeResult output = await sizeStrategy.CalculatePoolSizeAsync(input.Pool, input.Agents);
						results.Enqueue(output);
					}
					catch (Exception e)
					{
						_logger.LogError(e, "Failed calculating pool size for pool {PoolId}", input.Pool?.Id);
					}
				});
			}
			finally
			{
				span.Finish();
			}

			return results.ToList();
		}

		internal async Task<List<PoolSizeResult>> GetPoolSizeDataAsync()
		{
			List<IAgent> agents = await _agentCollection.FindAsync(status: AgentStatus.Ok, enabled: true);
			List<IAgent> GetAgentsInPool(PoolId poolId) => agents.FindAll(a => a.GetPools().Any(p => p == poolId));
			List<IPool> pools = await _poolCollection.GetAsync();

			return pools.Select(pool => new PoolSizeResult(pool, GetAgentsInPool(pool.Id), null)).ToList();
		}

		internal async Task ResizePoolAsync(PoolSizeResult poolSizeResult, CancellationToken cancellationToken)
		{
			IPool pool = poolSizeResult.Pool;

			if (!pool.EnableAutoscaling || poolSizeResult.DesiredAgentCount == null)
			{
				return;
			}

			int currentAgentCount = poolSizeResult.Agents.Count;
			int desiredAgentCount = poolSizeResult.DesiredAgentCount.Value;
			int deltaAgentCount = desiredAgentCount - currentAgentCount;

			using IScope scope = GlobalTracer.Instance
				.BuildSpan("FleetService.ResizePool")
				.WithTag(Datadog.Trace.OpenTracing.DatadogTags.ResourceName, pool.Id.ToString())
				.WithTag("CurrentAgentCount", currentAgentCount)
				.WithTag("DesiredAgentCount", desiredAgentCount)
				.WithTag("DeltaAgentCount", deltaAgentCount)
				.StartActive();
			
			IFleetManager fleetManager = CreateFleetManager(poolSizeResult.Pool);
			Dictionary<string, object> logScopeMetadata = new()
			{
				["FleetManager"] = fleetManager.GetType().Name,
				["PoolSizeStrategy"] = poolSizeResult.Status ?? new Dictionary<string, object>(),
				["PoolId"] = poolSizeResult.Pool.Id,
			};

			using IDisposable logScope = _logger.BeginScope(logScopeMetadata);
			
			_logger.LogInformation("{PoolName} Current={Current} Target={Target} Delta={Delta}",
				pool.Name, currentAgentCount, desiredAgentCount, deltaAgentCount);

			try
			{
				if (deltaAgentCount > 0)
				{
					if (!_downtimeService.IsDowntimeActive)
					{
						TimeSpan scaleOutCooldown = pool.ScaleOutCooldown ?? _defaultScaleOutCooldown;
						bool isCoolingDown = pool.LastScaleUpTime != null && pool.LastScaleUpTime + scaleOutCooldown > _clock.UtcNow;
						scope.Span.SetTag("isCoolingDown", isCoolingDown);
						if (!isCoolingDown)
						{
							await fleetManager.ExpandPoolAsync(pool, poolSizeResult.Agents, deltaAgentCount, cancellationToken);
							await _poolCollection.TryUpdateAsync(pool, lastScaleUpTime: _clock.UtcNow);
						}
						else
						{
							TimeSpan? cooldownTimeLeft = pool.LastScaleUpTime + _defaultScaleOutCooldown - _clock.UtcNow;
							_logger.LogDebug("Cannot scale out {PoolName}, it's cooling down for another {TimeLeft} secs", pool.Name, cooldownTimeLeft?.TotalSeconds);
						}
					}
					else
					{
						_logger.LogDebug("Cannot scale out {PoolName}, downtime is active", pool.Name);
						scope.Span.SetTag("IsDowntimeActive", true);
					}
				}

				if (deltaAgentCount < 0)
				{
					TimeSpan scaleInCooldown = pool.ScaleInCooldown ?? _defaultScaleInCooldown;
					bool isCoolingDown = pool.LastScaleDownTime != null && pool.LastScaleDownTime + scaleInCooldown > _clock.UtcNow;
					scope.Span.SetTag("isCoolingDown", isCoolingDown);
					if (!isCoolingDown)
					{
						await fleetManager.ShrinkPoolAsync(pool, poolSizeResult.Agents, -deltaAgentCount, cancellationToken);
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
				_logger.LogInformation(ex, "Failed to scale {PoolName}:\n{Exception}", pool.Name, ex);
				return;
			}

			_dogStatsd.Gauge("agentpools.autoscale.target", desiredAgentCount, tags: new []{"pool:" + pool.Name});
			_dogStatsd.Gauge("agentpools.autoscale.current", currentAgentCount, tags: new []{"pool:" + pool.Name});
		}
		
		internal async Task ResizePoolsAsync(List<PoolSizeResult> poolSizeDatas, CancellationToken cancellationToken = default)
		{
			ParallelOptions options = new () { MaxDegreeOfParallelism = MaxParallelTasks, CancellationToken = cancellationToken };
			await Parallel.ForEachAsync(poolSizeDatas.OrderByDescending(x => x.Agents.Count), options, async (poolSizeData, innerCt) =>
			{
				await ResizePoolAsync(poolSizeData, innerCt);
			});
		}
		
		internal IEnumerable<string> GetPropValues(string name)
		{
			DateTime now = _clock.UtcNow;
			return name switch
			{
				"timeUtcYear" => new List<string> { now.Year.ToString() }, // as 1 to 9999
				"timeUtcMonth" => new List<string> { now.Month.ToString() }, // as 1 to 12
				"timeUtcDay" => new List<string> { now.Day.ToString() }, // as 1 to 31
				"timeUtcDayOfWeek" => new List<string> { now.DayOfWeek.ToString(), now.DayOfWeek.ToString().ToLower() },
				"timeUtcHour" => new List<string> { now.Hour.ToString() }, // as 0 to 23
				"timeUtcMin" => new List<string> { now.Minute.ToString() }, // as 0 to 59
				"timeUtcSec" => new List<string> { now.Second.ToString() }, // as 0 to 59
				
				"dayOfWeek" => new List<string> { now.DayOfWeek.ToString().ToLower() }, // Deprecated, use timeUtcDayOfWeek
				_ => Array.Empty<string>()
			};
		}
		
		/// <summary>
		/// Instantiate a fleet manager using the list of conditions/configs in <see cref="IPool" />
		/// </summary>
		/// <param name="pool">Pool to use</param>
		/// <returns>A fleet manager with parameters set, as dictated by the pool argument</returns>
		/// <exception cref="ArgumentException">If fleet manager could not be instantiated</exception>
		public IFleetManager CreateFleetManager(IPool pool)
		{
			foreach (FleetManagerInfo info in pool.FleetManagers)
			{
				if (info.Condition == null || info.Condition.Evaluate(GetPropValues))
				{
					return _fleetManagerFactory.CreateFleetManager(info.Type, info.Config);
				}
			}

			return _fleetManagerFactory.CreateFleetManager(FleetManagerType.Default, "{}");
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
			if (pool.SizeStrategies.Count > 0)
			{
				foreach (PoolSizeStrategyInfo info in pool.SizeStrategies)
				{
					if (info.Condition == null || info.Condition.Evaluate(GetPropValues))
					{
						switch (info.Type)
						{
							case PoolSizeStrategy.JobQueue:
								JobQueueSettings jqSettings = DeserializeConfig<JobQueueSettings>(info.Config);
								return new JobQueueStrategy(_jobCollection, _graphCollection, _streamService, _clock, _cache, jqSettings);
							
							case PoolSizeStrategy.LeaseUtilization:
								LeaseUtilizationSettings luSettings = DeserializeConfig<LeaseUtilizationSettings>(info.Config);
								return new LeaseUtilizationStrategy(_agentCollection, _poolCollection, _leaseCollection, _clock, _cache, luSettings);
							
							// Disabled until moved to a separate factory class as FleetService should not contain AWS-specific classes
							// case PoolSizeStrategy.ComputeQueueAwsMetric:
							// 	ComputeQueueAwsMetricSettings cqamSettings = DeserializeConfig<ComputeQueueAwsMetricSettings>(info.Config);
							// 	return new ComputeQueueAwsMetricStrategy(_awsCloudWatch, _computeService, cqamSettings);
							
							case PoolSizeStrategy.NoOp:
								return new NoOpPoolSizeStrategy();
							
							default:
								throw new ArgumentException("Invalid pool size strategy type " + info.Type);
						}
					}
				}
			}

			// These is the legacy way of creating and configuring strategies (list-based approach above is preferred)
			switch (pool.SizeStrategy ?? _settings.Value.DefaultAgentPoolSizeStrategy)
			{
				case PoolSizeStrategy.JobQueue:
					return new JobQueueStrategy(_jobCollection, _graphCollection, _streamService, _clock, _cache, pool.JobQueueSettings);
				case PoolSizeStrategy.LeaseUtilization:
					LeaseUtilizationSettings luSettings = new();
					if (pool.MinAgents != null) luSettings.MinAgents = pool.MinAgents.Value;
					if (pool.NumReserveAgents != null) luSettings.NumReserveAgents = pool.NumReserveAgents.Value;
					return new LeaseUtilizationStrategy(_agentCollection, _poolCollection, _leaseCollection, _clock, _cache, luSettings);
				case PoolSizeStrategy.NoOp:
					return new NoOpPoolSizeStrategy();
				default:
					throw new ArgumentException("Unknown pool size strategy " + pool.SizeStrategy);
			}
		}

		private static T DeserializeConfig<T>(string json)
		{
			json = String.IsNullOrEmpty(json) ? "{}" : json;
			T? config = JsonSerializer.Deserialize<T>(json);
			if (config == null) throw new ArgumentException("Unable to deserialize config: " + json);
			return config;
		}
	}
}
