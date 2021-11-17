// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using async_enumerable_dotnet;
using Dasync.Collections;
using Datadog.Trace;
using Jupiter.Implementation;

namespace Horde.Storage.Implementation
{
    public interface IBlobStore
    {
        Task<BlobIdentifier> PutObject(NamespaceId ns, byte[] blob, BlobIdentifier identifier);
        Task<BlobIdentifier> PutObject(NamespaceId ns, ReadOnlyMemory<byte> blob, BlobIdentifier identifier);
        Task<BlobIdentifier> PutObject(NamespaceId ns, Stream content, BlobIdentifier identifier);

        Task<BlobContents> GetObject(NamespaceId ns, BlobIdentifier blob);
        Task<bool> Exists(NamespaceId ns, BlobIdentifier blob);

        // Delete a object
        Task DeleteObject(NamespaceId ns, BlobIdentifier blob);


        // delete the whole namespace
        Task DeleteNamespace(NamespaceId ns);

        IAsyncEnumerable<BlobIdentifier> ListOldObjects(NamespaceId ns, DateTime cutoff);
    }

    public class BlobNotFoundException : Exception
    {
        public NamespaceId Ns { get; }
        public BlobIdentifier Blob { get; }

        public BlobNotFoundException(NamespaceId ns, BlobIdentifier blob) : base($"No Blob in Namespace {ns} with id {blob}")
        {
            Ns = ns;
            Blob = blob;
        }
    }

    public class NamespaceNotFoundException : Exception
    {
        public NamespaceId Namespace { get; }

        public NamespaceNotFoundException(NamespaceId @namespace) : base($"Could not find namespace {@namespace}")
        {
            Namespace = @namespace;
        }

    }

    public class BlobToLargeException : Exception
    {
        public BlobIdentifier Blob { get; }

        public BlobToLargeException(BlobIdentifier blob) : base($"Blob {blob} was to large to cache")
        {
            Blob = blob;
        }

    }

    public static class BlobStoreUtils
    {
        public static async Task<BlobIdentifier[]> FilterOutKnownBlobs(this IBlobStore blobStore, NamespaceId ns, BlobIdentifier[] blobs)
        {
            var tasks = blobs.Select(async blobIdentifier => new { BlobIdentifier = blobIdentifier, Exists = await blobStore.Exists(ns, blobIdentifier) });
            var blobResults = await Task.WhenAll(tasks);
            var filteredBlobs = blobResults.Where(ac => !ac.Exists).Select(ac => ac.BlobIdentifier);
            return filteredBlobs.ToArray();
        }

        public static async Task<BlobIdentifier[]> FilterOutKnownBlobs(this IBlobStore blobStore, NamespaceId ns, IAsyncEnumerable<BlobIdentifier> blobs)
        {
            ConcurrentBag<BlobIdentifier> missingBlobs = new ConcurrentBag<BlobIdentifier>();

            try
            {
                await blobs.ParallelForEachAsync(async identifier =>
                {
                    bool exists = await blobStore.Exists(ns, identifier);

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


        public static async Task<BlobContents> GetObjects(this IBlobStore blobStore, NamespaceId ns, BlobIdentifier[] blobs)
        {
            using Scope _ = Tracer.Instance.StartActive("blob.combine");
            Task<BlobContents>[] tasks = new Task<BlobContents>[blobs.Length];
            for (int i = 0; i < blobs.Length; i++)
            {
                tasks[i] = blobStore.GetObject(ns, blobs[i]);
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

}
