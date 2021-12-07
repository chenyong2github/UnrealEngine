// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using async_enumerable_dotnet;
using Dasync.Collections;
using Horde.Storage.Controllers;
using Horde.Storage.Implementation.Blob;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class OrphanBlobCleanupRefs : IBlobCleanup
    {
        private readonly IBlobService _blobService;
        private readonly IObjectService _objectService;
        private readonly IBlobIndex _blobIndex;
        private readonly ILeaderElection _leaderElection;
        private readonly IOptionsMonitor<GCSettings> _gcSettings;
        private readonly ILogger _logger = Log.ForContext<OrphanBlobCleanupRefs>();

        // ReSharper disable once UnusedMember.Global
        public OrphanBlobCleanupRefs(IBlobService blobService, IObjectService objectService, IBlobIndex blobIndex, ILeaderElection leaderElection, IOptionsMonitor<GCSettings> gcSettings)
        {
            _blobService = blobService;
            _objectService = objectService;
            _blobIndex = blobIndex;
            _leaderElection = leaderElection;
            _gcSettings = gcSettings;
        }

        public async Task<List<BlobIdentifier>> Cleanup(CancellationToken cancellationToken)
        {
            if (!_leaderElection.IsThisInstanceLeader())
            {
                _logger.Information("Skipped orphan blob (refs) cleanup run as this instance is not the leader");
                return new List<BlobIdentifier>();
            }

            List<NamespaceId> namespaces = await ListNamespaces().Where(NamespaceShouldBeCleaned).ToListAsync();
          

            // enumerate all namespaces, and check if the old blob is valid in any of them to allow for a blob store to just store them in a single pile if it wants to
            List<BlobIdentifier> removedBlobs = new List<BlobIdentifier>();
            foreach (NamespaceId @namespace in namespaces)
            {
                if (cancellationToken.IsCancellationRequested)
                    break;

                // only consider blobs that have been around for 60 minutes
                // this due to cases were blobs are uploaded first
                await foreach ((BlobIdentifier blob, DateTime lastModified) in _blobService.ListObjects(@namespace).WithCancellation(cancellationToken))
                {
                    if (lastModified > DateTime.Now.AddMinutes(-60))
                        continue;
                    
                    if (cancellationToken.IsCancellationRequested)
                        break;

                    IBlobIndex.BlobInfo? blobIndex = await _blobIndex.GetBlobInfo(@namespace, blob);

                    bool shouldDelete = blobIndex == null;
                    if (blobIndex != null)
                    {
                        bool found = false;
                        foreach ((BucketId bucket, IoHashKey key) in blobIndex.References)
                        {
                            if (cancellationToken.IsCancellationRequested)
                                break;

                            try
                            {
                                (ObjectRecord, BlobContents) _ = await _objectService.Get(@namespace, bucket, key, Array.Empty<string>());
                                found = true;
                            }
                            catch (ObjectNotFoundException)
                            {
                                // this is not a valid reference so we should delete
                            }
                            catch (MissingBlobsException)
                            {
                                // we do not care if there are missing blobs, as long as the record is valid we keep this blob around
                                found = true;
                            }
                        }

                        if (!found)
                            shouldDelete = true;
                    }
                    
                    if (shouldDelete)
                    {
                        await RemoveBlob(@namespace, blob);
                        removedBlobs.Add(blob);
                    }
                }
            }

            return removedBlobs;
        }

        private async Task RemoveBlob(NamespaceId ns, BlobIdentifier blob)
        {
            _logger.Information("Attempting to GC Orphan blob {Blob} from {Namespace}", blob, ns);
            try
            {
                await _blobService.DeleteObject(ns, blob);
            }
            catch (Exception e)
            {
                _logger.Warning("Failed to delete blob {Blob} from {Namespace} due to {Error}", blob, ns, e.Message);
            }
        }

        private bool NamespaceShouldBeCleaned(NamespaceId ns)
        {
            return _gcSettings.CurrentValue.CleanNamespaces.Contains(ns.ToString());
        }

        private IAsyncEnumerable<NamespaceId> ListNamespaces()
        {
            return _objectService.GetNamespaces();
        }
    }
}
