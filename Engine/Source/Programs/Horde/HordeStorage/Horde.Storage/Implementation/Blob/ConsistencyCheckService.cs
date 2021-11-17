// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Cassandra;
using Datadog.Trace;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    // ReSharper disable once ClassNeverInstantiated.Global
    public class ConsistencyCheckService : PollingService<ConsistencyCheckService.ConsistencyState>, IDisposable
    {
        private readonly IOptionsMonitor<ConsistencyCheckSettings> _settings;
        private readonly IServiceProvider _provider;
        private readonly ILeaderElection _leaderElection;
        private readonly IRefsStore _refsStore;
        private readonly ILogger _logger = Log.ForContext<ConsistencyCheckService>();

        public class ConsistencyState
        {
        }

        public override bool ShouldStartPolling()
        {
            return _settings.CurrentValue.Enabled;
        }

        public ConsistencyCheckService(IOptionsMonitor<ConsistencyCheckSettings> settings, IServiceProvider provider, ILeaderElection leaderElection, IRefsStore refsStore) :
            base(serviceName: nameof(ConsistencyCheckService), TimeSpan.FromSeconds(settings.CurrentValue.ConsistencyCheckPollFrequencySeconds), new ConsistencyState())
        {
            _settings = settings;
            _provider = provider;
            _leaderElection = leaderElection;
            _refsStore = refsStore;
        }

        public override async Task<bool> OnPoll(ConsistencyState state, CancellationToken cancellationToken)
        {
            if (!_settings.CurrentValue.Enabled)
            {
                _logger.Information("Skipped running consistency check as it is disabled");
                return false;
            }

            if (!_leaderElection.IsThisInstanceLeader())
            {
                _logger.Information("Skipped running consistency check because this instance was not the leader");
                return false;
            }

            using Scope scope = Tracer.Instance.StartActive("consistency_check.run");

            await RunConsistencyCheck();

            return true;
        }

        private async Task RunConsistencyCheck()
        {
            //TODO: This isn't really specific to the S3 store, but it should likely only run on the backing store and not the cache layers which we do not have a way of fetching right now.
            AmazonS3Store? s3Store;
            try
            {
                s3Store = _provider.GetService<AmazonS3Store>();
            }
            catch (Exception)
            {
                s3Store = null;
            }

            if (s3Store == null)
            {
                _logger.Warning("No S3 Store found so will not run any consistency check");
                return;
            }

            long countOfBlobsChecked = 0;
            DateTime objectCutoff = DateTime.Now;
            // technically this does not need to be run per namespace but per s3 bucket
            await foreach (NamespaceId ns in _refsStore.GetNamespaces())
            {
                await foreach (BlobIdentifier blob in s3Store.ListOldObjects(ns, objectCutoff))
                {
                    if (countOfBlobsChecked % 100 == 0)
                        _logger.Information("Consistency check running on S3 blob store, count of blobs processed so far: {CountOfBlobs}", countOfBlobsChecked);
                    Interlocked.Increment(ref countOfBlobsChecked);
                    
                    BlobContents contents = await s3Store.GetObject(ns, blob);
                    await using Stream s = contents.Stream;

                    BlobIdentifier newHash = await BlobIdentifier.FromStream(s);
                    if (!blob.Equals(newHash))
                    {
                        _logger.Error("Mismatching hash for S3 object {Blob} in {Namespace}, new hash has {NewHash}", blob, ns, newHash);
                    }
                }
                
            }
        }

        protected override Task OnStopping(ConsistencyState state)
        {
            return Task.CompletedTask;
        }

        public void Dispose()
        {

        }
    }

    public class ConsistencyCheckSettings
    {
        public bool Enabled { get; set; } = false;
        public double ConsistencyCheckPollFrequencySeconds { get; set; } = TimeSpan.FromHours(2).TotalSeconds;
    }
}
