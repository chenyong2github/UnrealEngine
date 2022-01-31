// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;

namespace Horde.Storage.Implementation.Blob;

public class MemoryBlobIndex : IBlobIndex
{
    private class MemoryBlobInfo : IBlobIndex.BlobInfo
    {

    }

    private readonly ConcurrentDictionary<NamespaceId, ConcurrentDictionary<BlobIdentifier, MemoryBlobInfo>> _index = new ();
    private readonly IOptionsMonitor<JupiterSettings> _jupiterSettings;

    public MemoryBlobIndex(IOptionsMonitor<JupiterSettings> settings)
    {
        _jupiterSettings = settings;
    }

    private ConcurrentDictionary<BlobIdentifier, MemoryBlobInfo> GetNamespaceContainer(NamespaceId ns)
    {
        return _index.GetOrAdd(ns, id => new ConcurrentDictionary<BlobIdentifier, MemoryBlobInfo>());
    }

    public Task AddBlobToIndex(NamespaceId ns, BlobIdentifier id)
    {
        ConcurrentDictionary<BlobIdentifier, MemoryBlobInfo> index = GetNamespaceContainer(ns);
        index[id] = NewBlobInfo(ns);
        return Task.CompletedTask;
    }

    public Task<IBlobIndex.BlobInfo?> GetBlobInfo(NamespaceId ns, BlobIdentifier id)
    {
        ConcurrentDictionary<BlobIdentifier, MemoryBlobInfo> index = GetNamespaceContainer(ns);

        if (!index.TryGetValue(id, out MemoryBlobInfo? blobInfo))
            return Task.FromResult<IBlobIndex.BlobInfo?>(null);

        return Task.FromResult<IBlobIndex.BlobInfo?>(blobInfo);
    }

    public Task<bool> RemoveBlobFromIndex(NamespaceId ns, BlobIdentifier id)
    {
        ConcurrentDictionary<BlobIdentifier, MemoryBlobInfo> index = GetNamespaceContainer(ns);

        return Task.FromResult(index.TryRemove(id, out MemoryBlobInfo? _));
    }

    public async Task<bool> BlobExistsInRegion(NamespaceId ns, BlobIdentifier blobIdentifier)
    {
        IBlobIndex.BlobInfo? blobInfo = await GetBlobInfo(ns, blobIdentifier);
        return blobInfo?.Regions.Contains(_jupiterSettings.CurrentValue.CurrentSite) ?? false;
    }

    public Task AddRefToBlobs(NamespaceId ns, BucketId bucket, IoHashKey key, BlobIdentifier[] blobs)
    {
        foreach (BlobIdentifier id in blobs)
        {
            ConcurrentDictionary<BlobIdentifier, MemoryBlobInfo> index = GetNamespaceContainer(ns);

            index.AddOrUpdate(id, _ =>
            {
                MemoryBlobInfo info = NewBlobInfo(ns);
                info.References.Add((bucket, key));
                return info;
            }, (_, info) =>
            {
                info.References.Add((bucket, key));
                return info;
            });
        };

        return Task.CompletedTask;
    }

    private MemoryBlobInfo NewBlobInfo(NamespaceId ns)
    {
        MemoryBlobInfo info = new MemoryBlobInfo
        {
            Regions = new HashSet<string> { _jupiterSettings.CurrentValue.CurrentSite },
            Namespace = ns,
        };
        return info;
    }
}
