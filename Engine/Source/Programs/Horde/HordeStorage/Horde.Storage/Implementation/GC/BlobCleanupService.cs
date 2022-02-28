// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Jupiter;
using Microsoft.Extensions.Options;
using Datadog.Trace;
using Jupiter.Implementation;
using Microsoft.Extensions.DependencyInjection;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class BlobCleanupService : PollingService<BlobCleanupService.BlobCleanupState>
    {
        private readonly IOptionsMonitor<GCSettings> _settings;
        private volatile bool _alreadyPolling;
        private readonly ILogger _logger = Log.ForContext<BlobCleanupService>();

        protected override bool ShouldStartPolling()
        {
            return _settings.CurrentValue.BlobCleanupServiceEnabled;
        }

        public class BlobCleanupState
        {
            public List<IBlobCleanup> BlobCleanups { get; } = new List<IBlobCleanup>();
        }

        public BlobCleanupService(IServiceProvider provider, IOptionsMonitor<GCSettings> settings) :
            base(serviceName: nameof(BlobCleanupService), settings.CurrentValue.BlobCleanupPollFrequency,
                new BlobCleanupState())
        {
            _settings = settings;

            if (settings.CurrentValue.CleanOldBlobsLegacy)
            {
                OrphanBlobCleanup orphanBlobCleanup = provider.GetService<OrphanBlobCleanup>()!;
                RegisterCleanup(orphanBlobCleanup);
            }

            
            if (settings.CurrentValue.CleanOldBlobs)
            {
                OrphanBlobCleanupRefs orphanBlobCleanupRefs = provider.GetService<OrphanBlobCleanupRefs>()!;
                RegisterCleanup(orphanBlobCleanupRefs);
            }

            FileSystemStore? fileSystemStore = provider.GetService<FileSystemStore>();
            if (fileSystemStore != null)
            {
                RegisterCleanup(fileSystemStore);
            }
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

        public async Task Cleanup(BlobCleanupState state, CancellationToken cancellationToken)
        {
            List<BlobIdentifier> removedBlobs = new List<BlobIdentifier>();
            
            foreach (IBlobCleanup blobCleanup in state.BlobCleanups)
            {
                if (!blobCleanup.ShouldRun())
                    continue;

                string type = blobCleanup.GetType().ToString();
                _logger.Information("Blob cleanup running for {BlobCleanup}", type);
                using IScope scope = Tracer.Instance.StartActive("gc.blob");
                scope.Span.SetTag("type", type);

                _logger.Information("Attempting to run Blob Cleanup {BlobCleanup}. ", type);
                try
                {
                    ulong countOfBlobsCleaned = await blobCleanup.Cleanup(cancellationToken);
                    _logger.Information("Ran blob cleanup {BlobCleanup}. Deleted {CountBlobRecords}", type, countOfBlobsCleaned);
                }
                catch (Exception e)
                {
                    _logger.Error(e, "Exception running Blob Cleanup {BlobCleanup} .", type);
                }
            }
        }
    }
}

