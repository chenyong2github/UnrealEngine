// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Datadog.Trace;
using Jupiter.Implementation;
using Jupiter.Utils;

namespace Horde.Storage.Implementation
{
    public class HierarchicalBlobStoreException : Exception
    {
        public HierarchicalBlobStoreException(string? message) : base(message)
        {
        }
    }
    
    /// <summary>
    /// A hierarchical blob store
    /// Accepts a sequence of other blob stores and will return a result as soon as one is found when traversing.
    /// </summary>
    internal class HierarchicalBlobStore : IBlobStore
    {
        public IBlobStore[] BlobStores { get; }
        
        public HierarchicalBlobStore(IEnumerable<IBlobStore> blobStores)
        {
            this.BlobStores = blobStores.ToArray();
        }

        public async Task<BlobIdentifier> PutObject(NamespaceId ns, byte[] blob, BlobIdentifier identifier)
        {
            BlobIdentifier lastIdentifier = null!;
            foreach (IBlobStore store in BlobStores)
            {
                using Scope scope = Tracer.Instance.StartActive("HierarchicalStore.PutObject");
                scope.Span.ResourceName = identifier.ToString();
                scope.Span.SetTag("BlobStore", store.GetType().Name);
                lastIdentifier = await store.PutObject(ns, blob, identifier);
            }

            return lastIdentifier;
        }

        public async Task<BlobIdentifier> PutObject(NamespaceId ns, ReadOnlyMemory<byte> blob, BlobIdentifier identifier)
        {
            BlobIdentifier lastIdentifier = null!;
            foreach (IBlobStore store in BlobStores)
            {
                using Scope scope = Tracer.Instance.StartActive("HierarchicalStore.PutObject");
                scope.Span.ResourceName = identifier.ToString();
                scope.Span.SetTag("BlobStore", store.GetType().Name);
                lastIdentifier = await store.PutObject(ns, blob, identifier);
            }
            return lastIdentifier;
        }

        public async Task<BlobIdentifier> PutObject(NamespaceId ns, Stream content, BlobIdentifier identifier)
        {
            BlobIdentifier? lastIdentifier = null;
            foreach (IBlobStore store in BlobStores)
            {
                using Scope scope = Tracer.Instance.StartActive("HierarchicalStore.PutObject");
                scope.Span.ResourceName = identifier.ToString();
                scope.Span.SetTag("BlobStore", store.GetType().Name);
                try
                {
                    lastIdentifier = await store.PutObject(ns, content, identifier);
                }
                catch (BlobToLargeException)
                {
                    // if the object is to large to cache we hope for the other stores to cache it
                }
            }

            if (lastIdentifier == null)
            {
                throw new Exception($"Failed to cache blob {identifier}, the object might be to large to fit into any of the available stores");
            }
            return lastIdentifier;
        }

        public async Task<BlobContents> GetObject(NamespaceId ns, BlobIdentifier blob)
        {
            bool seenBlobNotFound = false;
            bool seenNamespaceNotFound = false;
            int numStoreMisses = 0;
            BlobContents? blobContents = null;
            foreach (IBlobStore store in BlobStores)
            {
                using Scope scope = Tracer.Instance.StartActive("HierarchicalStore.GetObject");
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
                throw new HierarchicalBlobStoreException("blobContents is null");
            }

            if (numStoreMisses >= 1)
            {
                using Scope _ = Tracer.Instance.StartActive("HierarchicalStore.Populate");
                await using MemoryStream tempStream = new MemoryStream();
                await blobContents.Stream.CopyToAsync(tempStream);
                byte[] data = tempStream.ToArray();
                
                // Don't populate the last store, as that is where we got the hit
                for (int i = 0; i < numStoreMisses; i++)
                {
                    var blobStore = BlobStores[i];
                    using Scope scope = Tracer.Instance.StartActive("HierarchicalStore.PopulateStore");
                    scope.Span.SetTag("BlobStore", blobStore.GetType().Name);
                    // Populate each store traversed that did not have the content found lower in the hierarchy
                    await blobStore.PutObject(ns, data, blob);
                }

                blobContents = new BlobContents(new MemoryStream(data), data.Length);
            }
            
            return blobContents;
        }

        public async Task<bool> Exists(NamespaceId ns, BlobIdentifier blob)
        {
            foreach (IBlobStore store in BlobStores)
            {
                using Scope scope = Tracer.Instance.StartActive("HierarchicalStore.ObjectExists");
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

        public async Task DeleteObject(NamespaceId ns, BlobIdentifier blob)
        {
            bool blobNotFound = false;
            bool deletedAtLeastOnce = false;
            foreach (IBlobStore store in BlobStores)
            {
                try
                {
                    using Scope scope = Tracer.Instance.StartActive("HierarchicalStore.DeleteObject");
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

            if (deletedAtLeastOnce)
                return;

            if (blobNotFound)
                throw new BlobNotFoundException(ns, blob);

            throw new NamespaceNotFoundException(ns);
        }

        public async Task DeleteNamespace(NamespaceId ns)
        {
            bool deletedAtLeastOnce = false;
            foreach (IBlobStore store in BlobStores)
            {
                using Scope scope = Tracer.Instance.StartActive("HierarchicalStore.DeleteNamespace");
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

        public async IAsyncEnumerable<BlobIdentifier> ListOldObjects(NamespaceId ns, DateTime cutoff)
        {
            bool blobIdentifierAdded = false;
            HashSet<BlobIdentifier> uniqueBlobIds = new HashSet<BlobIdentifier>();
            foreach (IBlobStore store in BlobStores)
            {
                try
                {
                    using Scope scope = Tracer.Instance.StartActive("HierarchicalStore.ListOldObjects");
                    scope.Span.SetTag("BlobStore", store.GetType().Name);
                    await foreach (BlobIdentifier blobId in store.ListOldObjects(ns, cutoff))
                    {
                        uniqueBlobIds.Add(blobId);
                    }

                    blobIdentifierAdded = true;
                }
                catch (NamespaceNotFoundException)
                {
                    // Ignore
                }
            }

            if (!blobIdentifierAdded)
            {
                throw new NamespaceNotFoundException(ns);
            }

            foreach (BlobIdentifier blobId in uniqueBlobIds)
            {
                yield return blobId;
            }
        }
    }
}
