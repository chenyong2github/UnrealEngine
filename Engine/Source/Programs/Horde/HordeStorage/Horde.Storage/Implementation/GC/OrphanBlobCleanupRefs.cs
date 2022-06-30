// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using async_enumerable_dotnet;
using Dasync.Collections;
using Datadog.Trace;
using EpicGames.Horde.Storage;
using Horde.Storage.Controllers;
using Horde.Storage.Implementation.Blob;
using Jupiter;
using Jupiter.Common;
using Jupiter.Implementation;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class OrphanBlobCleanupRefs : IBlobCleanup
    {
        private readonly IBlobService _blobService;
        private readonly IObjectService _objectService;
        private readonly IBlobIndex _blobIndex;
        private readonly ILeaderElection _leaderElection;
        private readonly INamespacePolicyResolver _namespacePolicyResolver;
        private readonly ILogger _logger = Log.ForContext<OrphanBlobCleanupRefs>();

        // ReSharper disable once UnusedMember.Global
        public OrphanBlobCleanupRefs(IBlobService blobService, IObjectService objectService, IBlobIndex blobIndex, ILeaderElection leaderElection, INamespacePolicyResolver namespacePolicyResolver)
        {
            _blobService = blobService;
            _objectService = objectService;
            _blobIndex = blobIndex;
            _leaderElection = leaderElection;
            _namespacePolicyResolver = namespacePolicyResolver;
        }

        public bool ShouldRun()
        {
            if (!_leaderElection.IsThisInstanceLeader())
            {
                return false;
            }

            return true;
        }

        public async Task<ulong> Cleanup(CancellationToken cancellationToken)
        {
            if (!_leaderElection.IsThisInstanceLeader())
            {
                _logger.Information("Skipped orphan blob (refs) cleanup run as this instance is not the leader");
                return 0;
            }

            List<NamespaceId> namespaces = await ListNamespaces().Where(NamespaceShouldBeCleaned).ToListAsync();
            
            // enumerate all namespaces, and check if the old blob is valid in any of them to allow for a blob store to just store them in a single pile if it wants to
            ulong countOfBlobsRemoved = 0;
            foreach (NamespaceId @namespace in namespaces)
            {
                if (cancellationToken.IsCancellationRequested)
                {
                    break;
                }

                NamespacePolicy policy = _namespacePolicyResolver.GetPoliciesForNs(@namespace);
                List<NamespaceId> namespacesThatSharePool = namespaces.Where(ns => _namespacePolicyResolver.GetPoliciesForNs(ns).StoragePool == policy.StoragePool).ToList();

                // only consider blobs that have been around for 60 minutes
                // this due to cases were blobs are uploaded first
                DateTime cutoff = DateTime.Now.AddMinutes(-60);
                await foreach ((BlobIdentifier blob, DateTime lastModified) in _blobService.ListObjects(@namespace).WithCancellation(cancellationToken))
                {
                    if (lastModified > cutoff)
                    {
                        continue;
                    }

                    if (cancellationToken.IsCancellationRequested)
                    {
                        break;
                    }

                    using IScope removeBlobScope = Tracer.Instance.StartActive("gc.blob");
                    removeBlobScope.Span.ResourceName = $"{@namespace}.{blob}";

                    bool found = false;

                    // check all other namespaces that share the same storage pool for presence of the blob
                    await Parallel.ForEachAsync(namespacesThatSharePool, cancellationToken, async (blobNamespace, token) =>
                    {
                        if (token.IsCancellationRequested)
                        {
                            return;
                        }

                        BlobInfo? blobIndex = await _blobIndex.GetBlobInfo(blobNamespace, blob);

                        if (blobIndex == null)
                        {
                            return;
                        }

                        await Parallel.ForEachAsync(blobIndex.References, cancellationToken, async (tuple, token) =>
                        {
                            (BucketId bucket, IoHashKey key) = tuple;
                            if (token.IsCancellationRequested)
                            {
                                return;
                            }

                            try
                            {
                                (ObjectRecord, BlobContents?) _ = await _objectService.Get(blobNamespace,
                                    bucket, key, new string[] { "name" });
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
                        });
                    });

                    if (cancellationToken.IsCancellationRequested)
                    {
                        break;
                    }

                    removeBlobScope.Span.SetTag("removed", (!found).ToString());

                    if (!found)
                    {
                        await Parallel.ForEachAsync(namespacesThatSharePool, cancellationToken, async (ns, _) =>
                        {
                            await RemoveBlob(ns, blob);
                        });
                        ++countOfBlobsRemoved;
                    }
                }
            }

            return countOfBlobsRemoved;
        }

        private async Task RemoveBlob(NamespaceId ns, BlobIdentifier blob)
        {
            _logger.Information("Attempting to GC Orphan blob {Blob} from {Namespace}", blob, ns);
            try
            {
                await _blobService.DeleteObject(ns, blob);
            }
            catch (BlobNotFoundException)
            {
                // ignore blob not found exceptions, if it didn't exist it has been removed so we are happy either way
            }
            catch (Exception e)
            {
                _logger.Warning("Failed to delete blob {Blob} from {Namespace} due to {Error}", blob, ns, e.Message);
            }
        }

        private bool NamespaceShouldBeCleaned(NamespaceId ns)
        {
            try
            {
                NamespacePolicy policy = _namespacePolicyResolver.GetPoliciesForNs(ns);

                return policy.IsLegacyNamespace.HasValue && !policy.IsLegacyNamespace.Value;
            }
            catch (UnknownNamespaceException)
            {
                _logger.Warning("Namespace {Namespace} does not configure any policy, not running cleanup on it.", ns);
                return false;
            }
        }

        private IAsyncEnumerable<NamespaceId> ListNamespaces()
        {
            return _objectService.GetNamespaces();
        }
    }
}
