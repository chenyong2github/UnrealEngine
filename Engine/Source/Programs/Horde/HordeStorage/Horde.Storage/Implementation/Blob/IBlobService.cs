// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Dasync.Collections;
using Datadog.Trace;
using EpicGames.Horde.Storage;
using Horde.Storage.Controllers;
using Horde.Storage.Implementation.Blob;
using Jupiter.Common.Implementation;
using Jupiter.Implementation;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;
using Nito.AsyncEx;
using Serilog;

namespace Horde.Storage.Implementation;

public interface IBlobService
{
    Task<ContentHash> VerifyContentMatchesHash(Stream content, ContentHash identifier);
    Task<BlobIdentifier> PutObjectKnownHash(NamespaceId ns, IBufferedPayload content, BlobIdentifier identifier);
    Task<BlobIdentifier> PutObject(NamespaceId ns, IBufferedPayload payload, BlobIdentifier identifier);
    Task<BlobIdentifier> PutObject(NamespaceId ns, byte[] payload, BlobIdentifier identifier);

    Task<BlobContents> GetObject(NamespaceId ns, BlobIdentifier blob);
    Task<bool> Exists(NamespaceId ns, BlobIdentifier blob);

    // Delete a object
    Task DeleteObject(NamespaceId ns, BlobIdentifier blob);


    // delete the whole namespace
    Task DeleteNamespace(NamespaceId ns);

    IAsyncEnumerable<(BlobIdentifier,DateTime)> ListObjects(NamespaceId ns);
    Task<BlobIdentifier[]> FilterOutKnownBlobs(NamespaceId ns, BlobIdentifier[] blobs);
    Task<BlobIdentifier[]> FilterOutKnownBlobs(NamespaceId ns, IAsyncEnumerable<BlobIdentifier> blobs);
    Task<BlobContents> GetObjects(NamespaceId ns, BlobIdentifier[] refRequestBlobReferences);

}

public class BlobService : IBlobService
{
    private List<IBlobStore> _blobStores;
    private readonly IOptionsMonitor<HordeStorageSettings> _settings;
    private readonly IBlobIndex _blobIndex;
    private readonly ILogger _logger = Log.ForContext<BlobService>();

    internal IEnumerable<IBlobStore> BlobStore
    {
        get { return _blobStores; }
        set { _blobStores = value.ToList(); } 
    }

    public BlobService(IServiceProvider provider, IOptionsMonitor<HordeStorageSettings> settings, IBlobIndex blobIndex)
    {
        _blobStores = GetBlobStores(provider, settings).ToList();
        _settings = settings;
        _blobIndex = blobIndex;
    }

    private IEnumerable<IBlobStore> GetBlobStores(IServiceProvider provider, IOptionsMonitor<HordeStorageSettings> settings)
    {
        return settings.CurrentValue.GetStorageImplementations().Select(impl => ToStorageImplementation(provider, impl));
    }

    private IBlobStore ToStorageImplementation(IServiceProvider provider, HordeStorageSettings.StorageBackendImplementations impl)
    {
        IBlobStore? store = impl switch
        {
            HordeStorageSettings.StorageBackendImplementations.S3 => provider.GetService<AmazonS3Store>(),
            HordeStorageSettings.StorageBackendImplementations.Azure => provider.GetService<AzureBlobStore>(),
            HordeStorageSettings.StorageBackendImplementations.FileSystem => provider.GetService<FileSystemStore>(),
            HordeStorageSettings.StorageBackendImplementations.Memory => provider.GetService<MemoryCacheBlobStore>(),
            HordeStorageSettings.StorageBackendImplementations.MemoryBlobStore => provider.GetService<MemoryBlobStore>(),
            HordeStorageSettings.StorageBackendImplementations.Relay => provider.GetService<RelayBlobStore>(),
            _ => throw new ArgumentOutOfRangeException()
        };
        if (store == null)
            throw new ArgumentException($"Failed to find a provider service for type: {impl}");

        return store;
    }

    public async Task<ContentHash> VerifyContentMatchesHash(Stream content, ContentHash identifier)
    {
        ContentHash blobHash;
        {
            using IScope _ = Tracer.Instance.StartActive("web.hash");
            blobHash = await BlobIdentifier.FromStream(content);
        }

        if (!identifier.Equals(blobHash))
        {
            throw new HashMismatchException(identifier, blobHash);
        }

        return identifier;
    }

    public async Task<BlobIdentifier> PutObject(NamespaceId ns, IBufferedPayload payload, BlobIdentifier identifier)
    {
        using IScope _ = Tracer.Instance.StartActive("put_blob");

        await using Stream hashStream = payload.GetStream();
        BlobIdentifier id = BlobIdentifier.FromContentHash(await VerifyContentMatchesHash(hashStream, identifier));

        Task<BlobIdentifier> putObjectTask = PutObjectToStores(ns, payload, id);
        Task addToBlobIndexTask = _blobIndex.AddBlobToIndex(ns, id);

        await Task.WhenAll(putObjectTask, addToBlobIndexTask);

        return await putObjectTask;

    }

    public async Task<BlobIdentifier> PutObject(NamespaceId ns, byte[] payload, BlobIdentifier identifier)
    {
        using IScope _ = Tracer.Instance.StartActive("put_blob");

        await using Stream hashStream = new MemoryStream(payload);
        BlobIdentifier id = BlobIdentifier.FromContentHash(await VerifyContentMatchesHash(hashStream, identifier));

        Task<BlobIdentifier> putObjectTask = PutObjectToStores(ns, payload, id);
        Task addToBlobIndexTask = _blobIndex.AddBlobToIndex(ns, id);

        await Task.WhenAll(putObjectTask, addToBlobIndexTask);

        return await putObjectTask;
    }

    public async Task<BlobIdentifier> PutObjectKnownHash(NamespaceId ns, IBufferedPayload content, BlobIdentifier identifier)
    {
        using IScope _ = Tracer.Instance.StartActive("put_blob");
        Task<BlobIdentifier> putObjectTask = PutObjectToStores(ns, content, identifier);
        Task addToBlobIndexTask = _blobIndex.AddBlobToIndex(ns, identifier);

        await Task.WhenAll(putObjectTask, addToBlobIndexTask);

        return await putObjectTask;
    }

    private async Task<BlobIdentifier> PutObjectToStores(NamespaceId ns, IBufferedPayload bufferedPayload, BlobIdentifier identifier)
    {
        if (_settings.CurrentValue.BlobStoreParallel)
        {
            await Parallel.ForEachAsync(_blobStores, async (store, cancellationToken) =>
            {
                using IScope scope = Tracer.Instance.StartActive("put_blob_to_store");
                scope.Span.ResourceName = identifier.ToString();
                scope.Span.SetTag("store", store.ToString());

                await using Stream s = bufferedPayload.GetStream();
                await store.PutObject(ns, s, identifier);
            });
        }
        else
        {
            foreach (IBlobStore store in _blobStores)
            {
                using IScope scope = Tracer.Instance.StartActive("put_blob_to_store");
                scope.Span.ResourceName = identifier.ToString();
                scope.Span.SetTag("store", store.ToString());

                await using Stream s = bufferedPayload.GetStream();
                await store.PutObject(ns, s, identifier);
            }
        }


        return identifier;
    }

    private async Task<BlobIdentifier> PutObjectToStores(NamespaceId ns, byte[] payload, BlobIdentifier identifier)
    {
        if (_settings.CurrentValue.BlobStoreParallel)
        {
            await Parallel.ForEachAsync(_blobStores,
                async (store, cancellationToken) => { await store.PutObject(ns, payload, identifier); });
        }
        else
        {
            foreach (IBlobStore store in _blobStores)
            {
                await store.PutObject(ns, payload, identifier);
            }
        }

        return identifier;
    }

    public Task<BlobContents> GetObject(NamespaceId ns, BlobIdentifier blob)
    {
        if (_settings.CurrentValue.BlobStoreParallel)
        {
            return GetObjectParallel(ns, blob);
        }
        else
        {
            return GetObjectSequential(ns, blob);
        }
    }

    private async Task<BlobContents> GetObjectSequential(NamespaceId ns, BlobIdentifier blob)
    {
        bool seenBlobNotFound = false;
        bool seenNamespaceNotFound = false;
        int numStoreMisses = 0;
        BlobContents? blobContents = null;
        foreach (IBlobStore store in _blobStores)
        {
            using IScope scope = Tracer.Instance.StartActive("HierarchicalStore.GetObject");
            scope.Span.SetTag("BlobStore", store.GetType().Name);
            scope.Span.SetTag("ObjectFound", false.ToString());
            try
            {
                blobContents = await store.GetObject(ns, blob);
                scope.Span.SetTag("ObjectFound", true.ToString());
                break;
            }
            catch (BlobNotFoundException)
            {
                seenBlobNotFound = true;
                numStoreMisses++;
            }
            catch (NamespaceNotFoundException)
            {
                seenNamespaceNotFound = true;
            }
        }

        if (seenBlobNotFound && blobContents == null)
        {
            throw new BlobNotFoundException(ns, blob);
        }

        if (seenNamespaceNotFound && blobContents == null)
        {
            throw new NamespaceNotFoundException(ns);
        }
        
        if (blobContents == null)
        {
            // Should not happen but exists to safeguard against the null pointer
            throw new Exception("blobContents is null");
        }

        if (numStoreMisses >= 1)
        {
            using IScope _ = Tracer.Instance.StartActive("HierarchicalStore.Populate");
            await using MemoryStream tempStream = new MemoryStream();
            await blobContents.Stream.CopyToAsync(tempStream);
            byte[] data = tempStream.ToArray();
            
            // Don't populate the last store, as that is where we got the hit
            for (int i = 0; i < numStoreMisses; i++)
            {
                var blobStore = _blobStores[i];
                using IScope scope = Tracer.Instance.StartActive("HierarchicalStore.PopulateStore");
                scope.Span.SetTag("BlobStore", blobStore.GetType().Name);
                // Populate each store traversed that did not have the content found lower in the hierarchy
                await blobStore.PutObject(ns, data, blob);
            }

            blobContents = new BlobContents(new MemoryStream(data), data.Length);
        }
        
        return blobContents;
    }


    private async Task<BlobContents> GetObjectParallel(NamespaceId ns, BlobIdentifier blob)
    {
        bool seenBlobNotFound = false;
        bool seenNamespaceNotFound = false;

        List<Task<(BlobContents?, IBlobStore?)>> getTasks = new();

        foreach (IBlobStore store in _blobStores)
        {
            getTasks.Add(Task.Run( async () =>
            {
                using IScope scope = Tracer.Instance.StartActive("HierarchicalStore.GetObject");
                scope.Span.SetTag("BlobStore", store.GetType().Name);
                scope.Span.SetTag("ObjectFound", false.ToString());
                try
                {
                    BlobContents? blobContents = await store.GetObject(ns, blob);
                    scope.Span.SetTag("ObjectFound", true.ToString());
                    return ((BlobContents?)blobContents, (IBlobStore?)store);
                }
                catch (BlobNotFoundException)
                {
                    seenBlobNotFound = true;
                }
                catch (NamespaceNotFoundException)
                {
                    seenNamespaceNotFound = true;
                }

                return (null, null);
            }));
        }

        BlobContents? blobContents = null;
        IBlobStore? storeWithBlob = null;
        while (getTasks.Count != 0)
        {
            Task<(BlobContents?, IBlobStore?)> completedTask = await getTasks.WhenAny();
            (BlobContents? contents, IBlobStore? store)= await completedTask;
            if (contents == null)
            {
                // the task did not find a object, we wait for the other tasks to run
                getTasks.Remove(completedTask);
                continue;
            }

            blobContents = contents;
            storeWithBlob = store;
            // object found, we can break
            break;
        }

        if (seenBlobNotFound && blobContents == null)
        {
            throw new BlobNotFoundException(ns, blob);
        }

        if (seenNamespaceNotFound && blobContents == null)
        {
            throw new NamespaceNotFoundException(ns);
        }
        
        if (blobContents == null)
        {
            // Should not happen but exists to safeguard against the null pointer
            throw new Exception("blobContents is null");
        }
        if (storeWithBlob == null)
        {
            // Should not happen but exists to safeguard against the null pointer
            throw new Exception("storeWithBlob is null");
        }

        int indexOfStoreWithBlob = _blobStores.IndexOf(storeWithBlob);
        // if it wasn't the first store we need to populate the stores before it with the blob
        if (indexOfStoreWithBlob != 0)
        {
            using IScope _ = Tracer.Instance.StartActive("HierarchicalStore.Populate");
            // buffer the blob contents as it could be to large for us to buffer into memory before forwarding to the blob stores
            await using Stream s = blobContents.Stream;
            using FilesystemBufferedPayload payload = await FilesystemBufferedPayload.Create(s);

            // Don't populate the last store, as that is where we got the hit
            // Populate each store traversed that did not have the content found lower in the hierarchy
            await Parallel.ForEachAsync(_blobStores.GetRange(0, indexOfStoreWithBlob), async (store, token) =>
            {
                using IScope scope = Tracer.Instance.StartActive("HierarchicalStore.PopulateStore");
                scope.Span.SetTag("BlobStore", store.GetType().Name);

                await using Stream s = payload.GetStream();
                await store.PutObject(ns, s, blob);
            });

            // we just pushed the content to all stores, thus consuming it, so we just refetch it from the store that just reported it had it
            blobContents = await storeWithBlob.GetObject(ns, blob);
        }
        
        return blobContents;
    }

    public Task<bool> Exists(NamespaceId ns, BlobIdentifier blob)
    {
        if (_settings.CurrentValue.BlobStoreParallel)
        {
            return ExistsParallel(ns, blob);
        }
        else
        {
            return ExistsSequential(ns, blob);
        }
    }

    private async Task<bool> ExistsSequential(NamespaceId ns, BlobIdentifier blob)
    {
        foreach (IBlobStore store in _blobStores)
        {
            using IScope scope = Tracer.Instance.StartActive("HierarchicalStore.ObjectExists");
            scope.Span.SetTag("BlobStore", store.GetType().Name);
            if (await store.Exists(ns, blob))
            {
                scope.Span.SetTag("ObjectFound", true.ToString());
                return true;
            }
            scope.Span.SetTag("ObjectFound", false.ToString());
        }

        return false;
    }


    private async Task<bool> ExistsParallel(NamespaceId ns, BlobIdentifier blob)
    {
        List<Task<bool>> existsTasks = new();

        foreach (IBlobStore store in _blobStores)
        {
            existsTasks.Add(Task.Run( async () =>
            {
                using IScope scope = Tracer.Instance.StartActive("HierarchicalStore.ObjectExists");
                scope.Span.SetTag("BlobStore", store.GetType().Name);
                if (await store.Exists(ns, blob))
                {
                    scope.Span.SetTag("ObjectFound", true.ToString());
                    return true;
                }

                scope.Span.SetTag("ObjectFound", false.ToString());
                return false;
            }));
        }

        while (existsTasks.Count != 0)
        {
            Task<bool> completedTask = await existsTasks.WhenAny();
            bool exist = await completedTask;
            if (!exist)
            {
                // the task did not find a object, we wait for the other tasks to run
                existsTasks.Remove(completedTask);
                continue;
            }

            // object found, we can break
            return true;
        }
        return false;
    }

    public async Task DeleteObject(NamespaceId ns, BlobIdentifier blob)
    {
        bool blobNotFound = false;
        bool deletedAtLeastOnce = false;
        Task removeFromIndexTask = _blobIndex.RemoveBlobFromIndex(ns, blob);

        foreach (IBlobStore store in _blobStores)
        {
            try
            {
                using IScope scope = Tracer.Instance.StartActive("HierarchicalStore.DeleteObject");
                scope.Span.SetTag("BlobStore", store.GetType().Name);
                await store.DeleteObject(ns, blob);
                deletedAtLeastOnce = true;
            }
            catch (NamespaceNotFoundException)
            {
                // Ignore
            }
            catch (BlobNotFoundException)
            {
                blobNotFound = true;
            }
        }

        await removeFromIndexTask;

        if (deletedAtLeastOnce)
            return;

        if (blobNotFound)
            throw new BlobNotFoundException(ns, blob);

        throw new NamespaceNotFoundException(ns);
    }

    public async Task DeleteNamespace(NamespaceId ns)
    {
        bool deletedAtLeastOnce = false;
        foreach (IBlobStore store in _blobStores)
        {
            using IScope scope = Tracer.Instance.StartActive("HierarchicalStore.DeleteNamespace");
            scope.Span.SetTag("BlobStore", store.GetType().Name);
            try
            {
                await store.DeleteNamespace(ns);
                deletedAtLeastOnce = true;
            }
            catch (NamespaceNotFoundException)
            {
                // Ignore
            }
        }

        if (deletedAtLeastOnce)
            return;

        throw new NamespaceNotFoundException(ns);
    }

    public IAsyncEnumerable<(BlobIdentifier,DateTime)> ListObjects(NamespaceId ns)
    {
        // as this is a hierarchy of blob stores the last blob store should contain the superset of all stores
        return _blobStores.Last().ListObjects(ns);
    }

    public async Task<BlobIdentifier[]> FilterOutKnownBlobs(NamespaceId ns, BlobIdentifier[] blobs)
    {
        var tasks = blobs.Select(async blobIdentifier => new { BlobIdentifier = blobIdentifier, Exists = await Exists(ns, blobIdentifier) });
        var blobResults = await Task.WhenAll(tasks);
        var filteredBlobs = blobResults.Where(ac => !ac.Exists).Select(ac => ac.BlobIdentifier);
        return filteredBlobs.ToArray();
    }

    public async Task<BlobIdentifier[]> FilterOutKnownBlobs(NamespaceId ns, IAsyncEnumerable<BlobIdentifier> blobs)
    {
        ConcurrentBag<BlobIdentifier> missingBlobs = new ConcurrentBag<BlobIdentifier>();

        try
        {
            await blobs.ParallelForEachAsync(async identifier =>
            {
                bool exists = await Exists(ns, identifier);

                if (!exists)
                {
                    missingBlobs.Add(identifier);
                }
            });
        }
        catch (ParallelForEachException e)
        {
            if (e.InnerException is PartialReferenceResolveException)
                throw e.InnerException;

            throw;
        }

        return missingBlobs.ToArray();
    }

    public async Task<BlobContents> GetObjects(NamespaceId ns, BlobIdentifier[] blobs)
    {
        using IScope _ = Tracer.Instance.StartActive("blob.combine");
        Task<BlobContents>[] tasks = new Task<BlobContents>[blobs.Length];
        for (int i = 0; i < blobs.Length; i++)
        {
            tasks[i] = GetObject(ns, blobs[i]);
        }

        MemoryStream ms = new MemoryStream();
        foreach (Task<BlobContents> task in tasks)
        {
            BlobContents blob = await task;
            await using Stream s = blob.Stream;
            await s.CopyToAsync(ms);
        }

        ms.Seek(0, SeekOrigin.Begin);

        return new BlobContents(ms, ms.Length);
    }


}
