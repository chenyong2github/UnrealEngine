// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Threading;
using System.Threading.Tasks;
using Dasync.Collections;
using Horde.Storage.Implementation.TransactionLog;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    // ReSharper disable once ClassNeverInstantiated.Global
    public class ReplicationSnapshotService : PollingService<ReplicationSnapshotService.SnapshotState>
    {
        public class SnapshotState
        {
        }

        private readonly IServiceProvider _provider;
        private readonly IOptionsMonitor<SnapshotSettings> _settings;
        private readonly IReplicationLog _replicationLog;
        private readonly ILeaderElection _leaderElection;
        private readonly ILogger _logger = Log.ForContext<ReplicationSnapshotService>();
        private readonly CancellationTokenSource _cancellationTokenSource = new CancellationTokenSource();
        private Task? _snapshotBuildTask = null;

        public override bool ShouldStartPolling()
        {
            return _settings.CurrentValue.Enabled;
        }

        public ReplicationSnapshotService(IServiceProvider provider, IOptionsMonitor<SnapshotSettings> settings, IReplicationLog replicationLog, ILeaderElection leaderElection) :
            base(serviceName: nameof(ReplicationSnapshotService), TimeSpan.FromSeconds(settings.CurrentValue.SnapshotFrequencySeconds), new SnapshotState())
        {
            _provider = provider;
            _settings = settings;
            _replicationLog = replicationLog;
            _leaderElection = leaderElection;
        }

        public override async Task<bool> OnPoll(SnapshotState state, CancellationToken cancellationToken)
        {
            if (!_settings.CurrentValue.Enabled)
            {
                _logger.Information("Skipped running replication snapshot service as it is disabled");
                return false;
            }

            if (!_leaderElection.IsThisInstanceLeader())
            {
                _logger.Information("Skipped running snapshot service because this instance was not the leader");
                return false;
            }

            bool ran = false;
            _snapshotBuildTask = _replicationLog.GetNamespaces().ParallelForEachAsync(async ns =>
            {
                ReplicationLogSnapshotBuilder builder = ActivatorUtilities.CreateInstance<ReplicationLogSnapshotBuilder>(_provider);
                BlobIdentifier snapshotBlob = await builder.BuildSnapshot(ns, _settings.CurrentValue.SnapshotStorageNamespace, _cancellationTokenSource.Token);
                _logger.Information("Snapshot built for {Namespace} with id {Id}", ns, snapshotBlob);

                ran = true;
            }, _cancellationTokenSource.Token);
            await _snapshotBuildTask;
            _snapshotBuildTask = null;
            return ran;
        }

        protected override async Task OnStopping(SnapshotState state)
        {
            _cancellationTokenSource.Cancel();

            if (_snapshotBuildTask != null)
                await _snapshotBuildTask;
        }
    }

    public class SnapshotSettings
    {
        /// <summary>
        /// Enable to start a replicating another Jupiter instance into this one
        /// </summary>
        public bool Enabled { get; set; } = false;

        /// <summary>
        /// The frequency at which to poll for new replication events
        /// </summary>
        [Required]
        public int SnapshotFrequencySeconds { get; set; } = 3600; // default to once a day

        public NamespaceId SnapshotStorageNamespace { get; set; } = new NamespaceId("jupiter-internal");
    }
}
