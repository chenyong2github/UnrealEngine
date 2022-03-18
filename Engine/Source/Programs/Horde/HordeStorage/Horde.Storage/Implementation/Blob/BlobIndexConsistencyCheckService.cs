// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Datadog.Trace;
using Horde.Storage.Implementation.Blob;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    // ReSharper disable once ClassNeverInstantiated.Global
    public class BlobIndexConsistencyCheckService : PollingService<BlobIndexConsistencyCheckService.ConsistencyState>
    {
        private readonly IOptionsMonitor<ConsistencyCheckSettings> _settings;
        private readonly IOptionsMonitor<JupiterSettings> _jupiterSettings;
        private readonly ILeaderElection _leaderElection;
        private readonly IBlobIndex _blobIndex;
        private readonly IBlobService _blobService;
        private readonly ILogger _logger = Log.ForContext<BlobIndexConsistencyCheckService>();

        public class ConsistencyState
        {
        }

        protected override bool ShouldStartPolling()
        {
            return _settings.CurrentValue.EnableBlobIndexChecks;
        }

        public BlobIndexConsistencyCheckService(IOptionsMonitor<ConsistencyCheckSettings> settings, IOptionsMonitor<JupiterSettings> jupiterSettings, ILeaderElection leaderElection, IBlobIndex blobIndex, IBlobService blobService) :
            base(serviceName: nameof(BlobStoreConsistencyCheckService), TimeSpan.FromSeconds(settings.CurrentValue.ConsistencyCheckPollFrequencySeconds), new ConsistencyState())
        {
            _settings = settings;
            _jupiterSettings = jupiterSettings;
            _leaderElection = leaderElection;
            _blobIndex = blobIndex;
            _blobService = blobService;
        }

        public override async Task<bool> OnPoll(ConsistencyState state, CancellationToken cancellationToken)
        {
            if (!_settings.CurrentValue.EnableBlobIndexChecks)
            {
                _logger.Information("Skipped running blob index consistency check as it is disabled");
                return false;
            }

            if (!_leaderElection.IsThisInstanceLeader())
            {
                _logger.Information("Skipped running blob index consistency check because this instance was not the leader");
                return false;
            }

            using IScope scope = Tracer.Instance.StartActive("blob_index.consistency.poll");

            await RunConsistencyCheck();

            return true;
        }

        private async Task RunConsistencyCheck()
        {
            string currentRegion = _jupiterSettings.CurrentValue.CurrentSite;
            await foreach (IBlobIndex.BlobInfo blobInfo in _blobIndex.GetAllBlobs())
            {
                if (!blobInfo.Regions.Any())
                {
                    _logger.Warning("Blob {Blob} in namespace {Namespace} is not tracked to exist in any region", blobInfo.BlobIdentifier, blobInfo.Namespace);

                    if (await _blobService.ExistsInRootStore(blobInfo.Namespace, blobInfo.BlobIdentifier))
                    {
                        _logger.Warning("Blob {Blob} in namespace {Namespace} was found to exist in our blob store so re-adding it to the index", blobInfo.BlobIdentifier, blobInfo.Namespace);

                        // we did have it in our blob store so we adjust the index
                        await _blobIndex.AddBlobToIndex(blobInfo.Namespace, blobInfo.BlobIdentifier);
                        continue;
                    }
                    else
                    {
                        // this blob doesn't exist anywhere so we just cleanup the blob index
                        _logger.Warning("Blob {Blob} in namespace {Namespace} was removed from the blob index as it didnt exist anywhere", blobInfo.BlobIdentifier, blobInfo.Namespace);

                        await _blobIndex.RemoveBlobFromIndex(blobInfo.Namespace, blobInfo.BlobIdentifier);
                    }
                }

                if (blobInfo.Regions.Contains(currentRegion))
                {
                    if (!await _blobService.ExistsInRootStore(blobInfo.Namespace, blobInfo.BlobIdentifier))
                    {
                        _logger.Warning("Blob {Blob} in namespace {Namespace} did not exist in root store but is tracked as doing so in the blob index. Attempting to replicate it.", blobInfo.BlobIdentifier, blobInfo.Namespace);
                        try
                        {
                            BlobContents _ = await _blobService.ReplicateObject(blobInfo.Namespace, blobInfo.BlobIdentifier, force: true);
                        }
                        catch (BlobReplicationException e)
                        {
                            _logger.Error(e, "Failed to replicate Blob {Blob} in namespace {Namespace}. Unable to repair the blob index", blobInfo.BlobIdentifier, blobInfo.Namespace);

                            // we update the blob index to accurately reflect that we do not have the blob, this is not good though as it means a upload that we thought happened now lacks content
                            await _blobIndex.RemoveBlobFromRegion(blobInfo.Namespace, blobInfo.BlobIdentifier);
                        }
                    }

                }
            }

            await Task.CompletedTask;
        }

        protected override Task OnStopping(ConsistencyState state)
        {
            return Task.CompletedTask;
        }
    }
}
