// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection.Metadata;
using System.Threading.Tasks;
using Datadog.Trace;
using Jupiter.Implementation;
using Jupiter.Utils;

namespace Horde.Storage.Implementation
{
    public interface IReferenceResolver
    {
        IAsyncEnumerable<BlobIdentifier> ResolveReferences(NamespaceId ns, CompactBinaryObject cb);
    }

    public class ReferenceResolver : IReferenceResolver
    {
        private readonly IBlobStore _blobStore;
        private readonly IContentIdStore _contentIdStore;

        public ReferenceResolver(IBlobStore blobStore, IContentIdStore contentIdStore)
        {
            _blobStore = blobStore;
            _contentIdStore = contentIdStore;
        }

        public async IAsyncEnumerable<BlobIdentifier> ResolveReferences(NamespaceId ns, CompactBinaryObject cb)
        {
            // TODO: This is cacheable and we should store the result somewhere
            Queue<CompactBinaryObject> objectsToVisit = new Queue<CompactBinaryObject>();
            objectsToVisit.Enqueue(cb);
            List<BlobIdentifier> unresolvedReferences = new List<BlobIdentifier>();

            List<Task<(BlobIdentifier, BlobIdentifier[]?)>> pendingContentIdResolves = new();
            List<Task<CompactBinaryObject>> pendingCompactBinaryAttachments = new();

            while(pendingCompactBinaryAttachments.Count != 0 || pendingContentIdResolves.Count != 0 || objectsToVisit.Count != 0)
            {
                if (objectsToVisit.TryDequeue(out CompactBinaryObject? parent))
                {
                    // enumerate all fields in the compact binary and start to resolve their dependencies
                    foreach (CompactBinaryField field in parent.GetAllFields())
                    {
                        if (!field.IsAttachment())
                            continue;

                        BlobIdentifier? blobIdentifier = field.AsAttachment();
                        if (blobIdentifier == null)
                            continue;

                        if (field.IsBinaryAttachment())
                        {
                            pendingContentIdResolves.Add(ResolveContentId(ns, blobIdentifier));
                        }
                        else
                        {
                            // the identifier as is points to a blob
                            yield return blobIdentifier;
                        }

                        // if its a reference to another compact binary we need to fetch it and resolve it
                        if (!field.IsCompactBinaryAttachment())
                            continue;

                        pendingCompactBinaryAttachments.Add(ParseCompactBinaryAttachment(ns, blobIdentifier));
                    }
                }

                List<Task<(BlobIdentifier, BlobIdentifier[]?)>> contentIdResolvesToRemove = new();

                foreach (Task<(BlobIdentifier, BlobIdentifier[]?)> pendingContentIdResolveTask in pendingContentIdResolves)
                {
                    // check for any content id resolve that has finished and return those blobs it found
                    if (pendingContentIdResolveTask.IsCompleted)
                    {
                        (BlobIdentifier id, BlobIdentifier[]? resolvedBlobs) = await pendingContentIdResolveTask;
                        if (resolvedBlobs != null)
                        {
                            foreach (BlobIdentifier b in resolvedBlobs)
                            {
                                yield return b;
                            }
                        }
                        else
                        {
                            unresolvedReferences.Add(id);
                        }
                        contentIdResolvesToRemove.Add(pendingContentIdResolveTask);
                    }
                }

                // cleanup finished tasks
                foreach (Task<(BlobIdentifier, BlobIdentifier[]?)> finishedTask in contentIdResolvesToRemove)
                {
                    pendingContentIdResolves.Remove(finishedTask);
                }

                // check for any compact binary attachment fetches and add those to the objects we are handling
                List<Task<CompactBinaryObject>> finishedCompactBinaryResolves = new();
                foreach (Task<CompactBinaryObject> pendingCompactBinaryAttachment in pendingCompactBinaryAttachments)
                {
                    if (pendingCompactBinaryAttachment.IsCompleted)
                    {
                        try
                        {
                            CompactBinaryObject childBinaryObject = await pendingCompactBinaryAttachment;
                            objectsToVisit.Enqueue(childBinaryObject);
                        }
                        catch (BlobNotFoundException e)
                        {
                            unresolvedReferences.Add(e.Blob);
                        }
                        finishedCompactBinaryResolves.Add(pendingCompactBinaryAttachment);
                    }
                }

                // cleanup finished tasks
                foreach (Task<CompactBinaryObject> finishedTask in finishedCompactBinaryResolves)
                {
                    pendingCompactBinaryAttachments.Remove(finishedTask);
                }
            }

            if (unresolvedReferences.Count != 0)
            {
                throw new PartialReferenceResolveException(unresolvedReferences);
            }
        }

        private async Task<CompactBinaryObject> ParseCompactBinaryAttachment(NamespaceId ns, BlobIdentifier blobIdentifier)
        {
            BlobContents contents = await _blobStore.GetObject(ns, blobIdentifier);
            byte[] data = await contents.Stream.ToByteArray();
            CompactBinaryObject childBinaryObject = CompactBinaryObject.Load(data);

            return childBinaryObject;
        }

        private async Task<(BlobIdentifier, BlobIdentifier[]?)> ResolveContentId(NamespaceId ns, BlobIdentifier blobIdentifier)
        {
            using Scope scope = Tracer.Instance.StartActive("ReferenceResolver.ResolveContentId");
            scope.Span.ResourceName = blobIdentifier.ToString();
            BlobIdentifier[]? resolvedBlobs = await _contentIdStore.Resolve(ns, blobIdentifier);
            return (blobIdentifier, resolvedBlobs);
        }
    }

    public class PartialReferenceResolveException : Exception
    {
        public List<BlobIdentifier> UnresolvedReferences { get; }

        public PartialReferenceResolveException(List<BlobIdentifier> unresolvedReferences) : base($"References missing: {string.Join(',', unresolvedReferences)}")
        {
            UnresolvedReferences = unresolvedReferences;
        }
    }
}
