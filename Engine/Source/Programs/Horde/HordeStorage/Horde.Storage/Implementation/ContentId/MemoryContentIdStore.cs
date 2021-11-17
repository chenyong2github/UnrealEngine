// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Cassandra;
using Cassandra.Mapping;
using Jupiter.Implementation;

namespace Horde.Storage.Implementation
{
    public class MemoryContentIdStore : IContentIdStore
    {
        private readonly IBlobStore _blobStore;
        private readonly ConcurrentDictionary<NamespaceId, ConcurrentDictionary<BlobIdentifier, SortedList<int, BlobIdentifier[]>>> _contentIds = new ConcurrentDictionary<NamespaceId, ConcurrentDictionary<BlobIdentifier, SortedList<int, BlobIdentifier[]>>>();
        
        public MemoryContentIdStore(IBlobStore blobStore)
        {
            _blobStore = blobStore;
        }

        public async Task<BlobIdentifier[]?> Resolve(NamespaceId ns, BlobIdentifier contentId)
        {
            if (_contentIds.TryGetValue(ns, out ConcurrentDictionary<BlobIdentifier, SortedList<int, BlobIdentifier[]>>? contentIdsForNamespace))
            {
                if (contentIdsForNamespace.TryGetValue(contentId, out SortedList<int, BlobIdentifier[]>? contentIdMappings))
                {
                    foreach ((int weight, BlobIdentifier[] blobs) in contentIdMappings)
                    {
                        BlobIdentifier[] missingBlobs = await _blobStore.FilterOutKnownBlobs(ns, blobs);
                        if (missingBlobs.Length == 0)
                            return blobs;
                        // blobs are missing continue testing with the next content id in the weighted list as that might exist
                    }
                }
            }

            // if no content id is found, but we have a blob that matches the content id (so a unchunked and uncompressed version of the data) we use that instead
            if (await _blobStore.Exists(ns, contentId))
                return new[] { contentId };

            return null;
        }

        public Task Put(NamespaceId ns, BlobIdentifier contentId, BlobIdentifier blobIdentifier, int contentWeight)
        {
            _contentIds.AddOrUpdate(ns, (_) =>
            {
                ConcurrentDictionary<BlobIdentifier, SortedList<int, BlobIdentifier[]>> dict = new()
                {
                    [contentId] = new SortedList<int, BlobIdentifier[]>
                    {
                        {contentWeight, new BlobIdentifier[] { blobIdentifier } }
                    }
                };

                return dict;
            }, (_, dict) =>
            {
                dict.AddOrUpdate(contentId, (_) =>
                {
                    return new SortedList<int, BlobIdentifier[]>
                    {
                        { contentWeight, new BlobIdentifier[] { blobIdentifier } }
                    };
                }, (_, mappings) =>
                {
                    mappings[contentWeight] = new[] { blobIdentifier };
                    return mappings;
                });

                return dict;
            });

            return Task.CompletedTask;
        }
    }
}
