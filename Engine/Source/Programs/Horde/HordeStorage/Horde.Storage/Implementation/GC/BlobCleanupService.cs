// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Jupiter;
using Microsoft.Extensions.Options;
using Datadog.Trace;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class BlobCleanupService : PollingService<BlobCleanupService.BlobCleanupState>
    {
        private readonly IOptionsMonitor<GCSettings> _settings;
        private volatile bool _alreadyPolling;
        private readonly ILogger _logger = Log.ForContext<BlobCleanupService>();

        public override bool ShouldStartPolling()
        {
            return _settings.CurrentValue.BlobCleanupServiceEnabled;
        }

        public class BlobCleanupState
        {
            public List<IBlobCleanup> BlobCleanups { get; } = new List<IBlobCleanup>();
        }

        public BlobCleanupService(IOptionsMonitor<GCSettings> settings) :
            base(serviceName: nameof(RefCleanupService), settings.CurrentValue.BlobCleanupPollFrequency,
                new BlobCleanupState())
        {
            _settings = settings;
        }

        public void RegisterCleanup(IBlobCleanup cleanup)
        {
            State.BlobCleanups.Add(cleanup);
        }

        public override async Task<bool> OnPoll(BlobCleanupState state, CancellationToken cancellationToken)
        {
            if (_alreadyPolling)
                return false;

            _alreadyPolling = true;
            try
            {
                await Cleanup(state, cancellationToken);
                return true;
            }
            finally
            {
                _alreadyPolling = false;
            }
        }

        public async Task<List<RemovedBlobs>> Cleanup(BlobCleanupState state, CancellationToken cancellationToken)
        {
            List<RemovedBlobs> removedBlobs = new List<RemovedBlobs>();
            
            foreach (IBlobCleanup blobCleanup in state.BlobCleanups)
            {
                string type = blobCleanup.GetType().ToString();
                _logger.Information("Blob cleanup running for {BlobCleanup}", type);
                using Scope scope = Tracer.Instance.StartActive("gc.blob");
                scope.Span.SetTag("type", type);

                _logger.Information("Attempting to run Blob Cleanup {BlobCleanup}. ", type);
                try
                {
                    List<RemovedBlobs> tempBlobs = await blobCleanup.Cleanup(cancellationToken);
                    _logger.Information("Ran blob cleanup {BlobCleanup}. Deleted {CountBlobRecords}", type, tempBlobs.Count);
                    removedBlobs.AddRange(tempBlobs);
                }
                catch (Exception e)
                {
                    _logger.Error(e, "Exception running Blob Cleanup {BlobCleanup} .", type);
                }
            }

            return removedBlobs;
        }
    }
}

