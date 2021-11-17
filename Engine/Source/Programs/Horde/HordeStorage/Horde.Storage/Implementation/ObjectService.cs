// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using async_enumerable_dotnet;
using Datadog.Trace;
using Jupiter.Implementation;
using Jupiter.Utils;

namespace Horde.Storage.Implementation
{
    public interface IObjectService
    {
        Task<(ObjectRecord, BlobContents)> Get(NamespaceId ns, BucketId bucket, KeyId key, string[] fields);
        Task<PutObjectResult> Put(NamespaceId ns, BucketId bucket, KeyId key, BlobIdentifier blobHash, CompactBinaryObject payload);
        Task<BlobIdentifier[]> Finalize(NamespaceId ns, BucketId bucket, KeyId key, BlobIdentifier blobHash);

        IAsyncEnumerator<NamespaceId> GetNamespaces();

        Task<long> Delete(NamespaceId ns, BucketId bucket, KeyId key);
        Task<long> DropNamespace(NamespaceId ns);
        Task<long> DeleteBucket(NamespaceId ns, BucketId bucket);
    }

    public class ObjectService : IObjectService
    {
        private readonly IReferencesStore _referencesStore;
        private readonly IBlobStore _blobStore;
        private readonly IReferenceResolver _referenceResolver;
        private readonly IReplicationLog _replicationLog;

        public ObjectService(IReferencesStore referencesStore, IBlobStore blobStore, IReferenceResolver referenceResolver, IReplicationLog replicationLog)
        {
            _referencesStore = referencesStore;
            _blobStore = blobStore;
            _referenceResolver = referenceResolver;
            _replicationLog = replicationLog;
        }

        public async Task<(ObjectRecord, BlobContents)> Get(NamespaceId ns, BucketId bucket, KeyId key, string[]? fields = null)
        {
            ObjectRecord o = await _referencesStore.Get(ns, bucket, key);

            BlobContents blobContents;
            if (o.InlinePayload != null && o.InlinePayload.Length != 0)
            {
                blobContents = new BlobContents(o.InlinePayload);
            }
            else
            {
                blobContents = await _blobStore.GetObject(ns, o.BlobIdentifier);
            }
            return (o, blobContents);
        }

        public async Task<PutObjectResult> Put(NamespaceId ns, BucketId bucket, KeyId key, BlobIdentifier blobHash, CompactBinaryObject payload)
        {
            bool hasReferences = payload.GetAllFields().Any(field => field.IsAttachment());

            // if we have no references we are always finalized, e.g. there are no referenced blobs to upload
            bool isFinalized = !hasReferences;

            Task objectStorePut = _referencesStore.Put(ns, bucket, key, blobHash, payload.Data, isFinalized);

            Task<BlobIdentifier> blobStorePut = _blobStore.PutObject(ns, payload.Data, blobHash);

            BlobIdentifier[] missingReferences = Array.Empty<BlobIdentifier>();
            if (hasReferences)
            {
                using Scope _ = Tracer.Instance.StartActive("ObjectService.ResolveReferences");
                try
                {
                    IAsyncEnumerable<BlobIdentifier> references = _referenceResolver.ResolveReferences(ns, payload);
                    missingReferences = await _blobStore.FilterOutKnownBlobs(ns, references);
                }
                catch (PartialReferenceResolveException e)
                {
                    missingReferences = e.UnresolvedReferences.ToArray();
                }
            }

            await Task.WhenAll(objectStorePut, blobStorePut);

            if (missingReferences.Length == 0)
            {
                await _referencesStore.Finalize(ns, bucket, key, blobHash);
                await _replicationLog.InsertAddEvent(ns, bucket, key, blobHash);
            }

            return new PutObjectResult(missingReferences);
        }

        public async Task<BlobIdentifier[]> Finalize(NamespaceId ns, BucketId bucket, KeyId key, BlobIdentifier blobHash)
        {
            (ObjectRecord o, BlobContents blob) = await Get(ns, bucket, key);
            byte[] blobContents = await blob.Stream.ToByteArray();
            CompactBinaryObject payload = CompactBinaryObject.Load(blobContents);

            bool hasReferences = payload.GetAllFields().Any(field => field.IsAttachment());

            BlobIdentifier[] missingReferences = Array.Empty<BlobIdentifier>();
            if (hasReferences)
            {
                using Scope _ = Tracer.Instance.StartActive("ObjectService.ResolveReferences");
                try
                {
                    IAsyncEnumerable<BlobIdentifier> references = _referenceResolver.ResolveReferences(ns, payload);
                    missingReferences = await _blobStore.FilterOutKnownBlobs(ns, references);
                }
                catch (PartialReferenceResolveException e)
                {
                    missingReferences = e.UnresolvedReferences.ToArray();
                }
            }

            if (missingReferences.Length == 0)
            {
                await _referencesStore.Finalize(ns, bucket, key, blobHash);
                await _replicationLog.InsertAddEvent(ns, bucket, key, blobHash);
            }

            return missingReferences;
        }

        public IAsyncEnumerator<NamespaceId> GetNamespaces()
        {
            return _referencesStore.GetNamespaces();
        }

        public Task<long> Delete(NamespaceId ns, BucketId bucket, KeyId key)
        {
            return _referencesStore.Delete(ns, bucket, key);
        }

        public Task<long> DropNamespace(NamespaceId ns)
        {
            return _referencesStore.DropNamespace(ns);
        }

        public Task<long> DeleteBucket(NamespaceId ns, BucketId bucket)
        {
            return _referencesStore.DeleteBucket(ns, bucket);
        }
    }

    public class PutObjectResult
    {
        public PutObjectResult(BlobIdentifier[] missingReferences)
        {
            MissingReferences = missingReferences;
        }

        public BlobIdentifier[] MissingReferences { get; set; }
    }
}
