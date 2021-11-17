// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    // ReSharper disable once ClassNeverInstantiated.Global
    public class ReplicationService : PollingService<ReplicationService.ReplicationState>, IDisposable
    {
        private readonly IOptionsMonitor<ReplicationSettings> _settings;
        private readonly ILeaderElection _leaderElection;
        private readonly ILogger _logger = Log.ForContext<ReplicationService>();
        private readonly Dictionary<string, Task<bool>> _currentReplications = new Dictionary<string, Task<bool>>();

        public class ReplicationState
        {
            public List<IReplicator> Replicators { get; } = new List<IReplicator>();
        }

        public override bool ShouldStartPolling()
        {
            return _settings.CurrentValue.Enabled;
        }

        public ReplicationService(IOptionsMonitor<ReplicationSettings> settings, IServiceProvider provider, ILeaderElection leaderElection) :
            base(serviceName: nameof(ReplicationService), TimeSpan.FromSeconds(settings.CurrentValue.ReplicationPollFrequencySeconds), new ReplicationState())
        {
            _settings = settings;
            _leaderElection = leaderElection;

            _leaderElection.OnLeaderChanged += OnLeaderChanged;

            DirectoryInfo di = new DirectoryInfo(settings.CurrentValue.StateRoot);
            Directory.CreateDirectory(di.FullName);

            foreach (var replicator in settings.CurrentValue.Replicators)
            {
                try
                {
                    State.Replicators.Add(CreateReplicator(replicator, provider));
                }
                catch (Exception e)
                {
                    _logger.Error(e, "Failed to create replicator {Name}", replicator.ReplicatorName);
                }
            }

        }

        private void OnLeaderChanged(object? sender, ILeaderElection.OnLeaderChangedEventArgs e)
        {
            if (e.IsLeader)
                return;

            // if we are no longer the leader cancel any pending replications
            foreach (IReplicator replicator in State.Replicators)
            {
                //TODO: Cancel replications
            }
        }

        private static IReplicator CreateReplicator(ReplicatorSettings replicatorSettings, IServiceProvider provider)
        {
            switch (replicatorSettings.Version)
            {
                case ReplicatorVersion.V1:
                    return ActivatorUtilities.CreateInstance<ReplicatorV1>(provider, replicatorSettings);
                case ReplicatorVersion.Refs:
                    return ActivatorUtilities.CreateInstance<RefsReplicator>(provider, replicatorSettings);
                default:
                    throw new ArgumentOutOfRangeException();
            }
        }

        public override async Task<bool> OnPoll(ReplicationState state, CancellationToken cancellationToken)
        {
            if (!_settings.CurrentValue.Enabled)
            {
                _logger.Information("Skipped running replication as it is disabled");
                return false;
            }

            if (!_leaderElection.IsThisInstanceLeader())
            {
                _logger.Information("Skipped running replicators because this instance was not the leader");
                return false;
            }

            foreach (IReplicator replicator in state.Replicators)
            {
                if (_currentReplications.TryGetValue(replicator.Info.ReplicatorName, out Task<bool>? replicationTask))
                {
                    if (replicationTask.IsCompleted)
                    {
                        _currentReplications.Remove(replicator.Info.ReplicatorName);

                        if (replicationTask.IsFaulted)
                        {
                            // we log the error but avoid raising it to make sure all replicators actually get to run even if there is a faulting one
                            _logger.Error(replicationTask.Exception, "Unhandled exception in replicator {Name}", replicator.Info.ReplicatorName);
                            continue;
                        }
                     
                        await replicationTask;
                    }
                    else
                    {
                        // if the replication is still running let it continue to run
                        continue;
                    }
                }

                // start a new run of the replication
                Task<bool> newReplication = replicator.TriggerNewReplications();
                _currentReplications[replicator.Info.ReplicatorName] = newReplication;
            }

            if (state.Replicators.Count == 0)
            {
                _logger.Information("Finished replication poll, but no replicators configured to run.");
            }

            return state.Replicators.Count != 0;

        }

        protected override async Task OnStopping(ReplicationState state)
        {
            await Task.WhenAll(state.Replicators.Select(replicator => replicator.StopReplicating()).ToArray());
        }

        public void Dispose()
        {
            // we should have been stopped first so this should not be needed, but to make sure state is stored we dispose of it again
            foreach (IReplicator replicator in State.Replicators)
            {
                replicator.Dispose();
            }
        }

        public IEnumerable<IReplicator> GetReplicators(NamespaceId ns)
        {
            return State.Replicators
                .Where(pair => ns == pair.Info.NamespaceToReplicate)
                .Select(pair => pair);
        }
    }
}
