// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Amazon.AutoScaling;
using Amazon.EC2;
using Horde.Build.Agents.Fleet.Providers;
using Horde.Build.Agents.Leases;
using Horde.Build.Agents.Pools;
using Horde.Build.Jobs;
using Horde.Build.Jobs.Graphs;
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
	public sealed class AutoscaleServiceV2 : IHostedService, IDisposable
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
		private readonly StreamService _streamService;
		private readonly IDogStatsd _dogStatsd;
		private readonly IAmazonEC2 _ec2;
		private readonly IAmazonAutoScaling _awsAutoScaling;
		private readonly IClock _clock;
		private readonly IMemoryCache _cache;
		private readonly ITicker _ticker;
		private readonly ITicker _tickerHighFrequency;
		private readonly TimeSpan _defaultScaleOutCooldown;
		private readonly TimeSpan _defaultScaleInCooldown;
		private readonly IOptions<ServerSettings> _settings;
		private readonly ILogger<AutoscaleServiceV2> _logger;
		private readonly ILoggerFactory _loggerFactory;
		
		/// <summary>Allow overriding the fleet manager during testing</summary>
		private readonly Func<IPool, IFleetManager> _fleetManagerFactory;

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
			IDogStatsd dogStatsd,
			IAmazonEC2 ec2,
			IAmazonAutoScaling awsAutoScaling,
			IClock clock,
			IMemoryCache cache,
			IOptions<ServerSettings> settings,
			ILoggerFactory loggerFactory,
			Func<IPool, IFleetManager>? fleetManagerFactory = null)
		{
			_agentCollection = agentCollection;
			_graphCollection = graphCollection;
			_jobCollection = jobCollection;
			_leaseCollection = leaseCollection;
			_poolCollection = poolCollection;
			_streamService = streamService;
			_dogStatsd = dogStatsd;
			_ec2 = ec2;
			_awsAutoScaling = awsAutoScaling;
			_clock = clock;
			_cache = cache;
			_logger = loggerFactory.CreateLogger<AutoscaleServiceV2>();
			_loggerFactory = loggerFactory;
			_ticker = clock.AddSharedTicker<AutoscaleServiceV2>(TimeSpan.FromMinutes(5.0), TickLeaderAsync, _logger);
			_tickerHighFrequency = clock.AddSharedTicker("AutoscaleServiceV2.TickHighFrequency", TimeSpan.FromSeconds(30), TickHighFrequencyAsync, _logger);
			_settings = settings;
			_defaultScaleOutCooldown = TimeSpan.FromSeconds(settings.Value.AgentPoolScaleOutCooldownSeconds);
			_defaultScaleInCooldown = TimeSpan.FromSeconds(settings.Value.AgentPoolScaleInCooldownSeconds);
			_fleetManagerFactory = fleetManagerFactory ?? CreateFleetManager;
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
			List<PoolSizeData> output = await CalculatePoolSizesAsync(input, stoppingToken);
			await ResizePoolsAsync(output, stoppingToken);

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

		internal async Task<List<PoolSizeData>> CalculatePoolSizesAsync(List<PoolSizeData> poolSizeDatas, CancellationToken cancellationToken)
		{
			using IScope _ = GlobalTracer.Instance.BuildSpan("AutoscaleService.CalculatePoolSizesAsync").StartActive();
			ConcurrentQueue<PoolSizeData> results = new ();
			ParallelOptions options = new () { MaxDegreeOfParallelism = MaxParallelTasks, CancellationToken = cancellationToken };

			await Parallel.ForEachAsync(poolSizeDatas, options, async (input, innerCt) =>
			{
				IPoolSizeStrategy sizeStrategy = CreatePoolSizeStrategy(input.Pool);
				PoolSizeData output = (await sizeStrategy.CalcDesiredPoolSizesAsync(new List<PoolSizeData> { input }))[0];
				results.Enqueue(output);
			});

			return results.ToList();
		}

		internal async Task<List<PoolSizeData>> GetPoolSizeDataAsync()
		{
			List<IAgent> agents = await _agentCollection.FindAsync(status: AgentStatus.Ok, enabled: true);
			List<IAgent> GetAgentsInPool(PoolId poolId) => agents.FindAll(a => a.GetPools().Any(p => p == poolId));
			List<IPool> pools = await _poolCollection.GetAsync();

			return pools.Select(pool => new PoolSizeData(pool, GetAgentsInPool(pool.Id), null)).ToList();
		}

		internal async Task ResizePoolAsync(PoolSizeData poolSizeData, CancellationToken cancellationToken)
		{
			IPool pool = poolSizeData.Pool;

			if (!pool.EnableAutoscaling || poolSizeData.DesiredAgentCount == null)
			{
				return;
			}

			int currentAgentCount = poolSizeData.Agents.Count;
			int desiredAgentCount = poolSizeData.DesiredAgentCount.Value;
			int deltaAgentCount = desiredAgentCount - currentAgentCount;

			IFleetManager fleetManager = _fleetManagerFactory(poolSizeData.Pool);

			_logger.LogInformation("{PoolName,-48} Current={Current,4} Target={Target,4} Delta={Delta,4} Status={Status} FleetManager={FleetManager}",
				pool.Name, currentAgentCount, desiredAgentCount, deltaAgentCount, poolSizeData.StatusMessage, fleetManager.GetType().Name);
			
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
						await fleetManager.ExpandPoolAsync(pool, poolSizeData.Agents, deltaAgentCount, cancellationToken);
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
						await fleetManager.ShrinkPoolAsync(pool, poolSizeData.Agents, -deltaAgentCount, cancellationToken);
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
				return;
			}

			_dogStatsd.Gauge("agentpools.autoscale.target", desiredAgentCount, tags: new []{"pool:" + pool.Name});
			_dogStatsd.Gauge("agentpools.autoscale.current", currentAgentCount, tags: new []{"pool:" + pool.Name});
		}
		
		internal async Task ResizePoolsAsync(List<PoolSizeData> poolSizeDatas, CancellationToken cancellationToken = default)
		{
			using IScope _ = GlobalTracer.Instance.BuildSpan("AutoscaleService.ResizePoolsAsync").StartActive();
			
			ParallelOptions options = new () { MaxDegreeOfParallelism = MaxParallelTasks, CancellationToken = cancellationToken };
			await Parallel.ForEachAsync(poolSizeDatas.OrderByDescending(x => x.Agents.Count), options, async (poolSizeData, innerCt) =>
			{
				await ResizePoolAsync(poolSizeData, innerCt);
			});
		}
		
		private IEnumerable<string> GetPropValues(string name)
		{
			return name switch
			{
				"dayOfWeek" => new List<string> { _clock.UtcNow.DayOfWeek.ToString().ToLower() },
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
					return CreateFleetManager(info.Type, info.Config);
				}
			}

			return GetDefaultFleetManager();
		}
		
		/// <summary>
		/// Instantiate the default fleet manager  />
		/// </summary>
		/// <returns>A fleet manager with parameters from server settings</returns>
		/// <exception cref="ArgumentException">If fleet manager could not be instantiated</exception>
		public IFleetManager GetDefaultFleetManager()
		{
			return CreateFleetManager(_settings.Value.FleetManagerV2, _settings.Value.FleetManagerV2Config ?? "{}");
		}
		
		/// <summary>
		/// Create a fleet manager
		/// </summary>
		/// <param name="type">Type of fleet manager</param>
		/// <param name="config">Config as a serialized JSON string</param>
		/// <returns>An instantiated fleet manager with parameters loaded from config</returns>
		/// <exception cref="ArgumentException">If fleet manager could not be instantiated</exception>
		private IFleetManager CreateFleetManager(FleetManagerType type, string config)
		{
			switch (type)
			{
				case FleetManagerType.NoOp:
					return new NoOpFleetManager(_loggerFactory.CreateLogger<NoOpFleetManager>());
				case FleetManagerType.Aws:
					return new AwsFleetManager(_ec2, _agentCollection, DeserializeSettings<AwsFleetManagerSettings>(config), _loggerFactory.CreateLogger<AwsFleetManager>());
				case FleetManagerType.AwsReuse:
					return new AwsReuseFleetManager(_ec2, _agentCollection, DeserializeSettings<AwsReuseFleetManagerSettings>(config), _loggerFactory.CreateLogger<AwsReuseFleetManager>());
				case FleetManagerType.AwsAsg:
					return new AwsAsgFleetManager(_awsAutoScaling, DeserializeSettings<AwsAsgSettings>(config), _loggerFactory.CreateLogger<AwsAsgFleetManager>());	
				default:
					throw new ArgumentException("Unknown fleet manager type " + type);
			}
		}
		
		private static T DeserializeSettings<T>(string config)
		{
			if (String.IsNullOrEmpty(config)) { config = "{}"; }
			try
			{
				T? settings = JsonSerializer.Deserialize<T>(config);
				if (settings == null) throw new NullReferenceException($"Unable to deserialize");
				return settings;
			}
			catch (ArgumentException e)
			{
				throw new ArgumentException($"Unable to deserialize {typeof(T)} config: '{config}'", e);
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
			if (pool.SizeStrategies.Count > 0)
			{
				foreach (PoolSizeStrategyInfo info in pool.SizeStrategies)
				{
					if (info.Condition == null || info.Condition.Evaluate(GetPropValues))
					{
						switch (info.Type)
						{
							case PoolSizeStrategy.JobQueue:
								JobQueueSettings? jqSettings = JsonSerializer.Deserialize<JobQueueSettings>(info.Config);
								if (jqSettings == null) throw new ArgumentException("Unable to deserialize pool sizing config: " + info.Config);
								return new JobQueueStrategy(_jobCollection, _graphCollection, _streamService, _clock, _cache, jqSettings);
							
							case PoolSizeStrategy.LeaseUtilization:
								LeaseUtilizationSettings? luSettings = JsonSerializer.Deserialize<LeaseUtilizationSettings>(info.Config);
								if (luSettings == null) throw new ArgumentException("Unable to deserialize pool sizing config: " + info.Config);
								return new LeaseUtilizationStrategy(_agentCollection, _poolCollection, _leaseCollection, _clock, _cache, luSettings);
							
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
					return new JobQueueStrategy(_jobCollection, _graphCollection, _streamService, _clock, _cache, pool.JobQueueSettings);
				case PoolSizeStrategy.LeaseUtilization:
					return new LeaseUtilizationStrategy(_agentCollection, _poolCollection, _leaseCollection, _clock, _cache, new());
				case PoolSizeStrategy.NoOp:
					return new NoOpPoolSizeStrategy();
				default:
					throw new ArgumentException("Unknown pool size strategy " + pool.SizeStrategy);
			}
		}
	}
}
