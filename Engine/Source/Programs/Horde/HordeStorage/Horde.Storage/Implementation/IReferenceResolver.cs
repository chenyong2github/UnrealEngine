// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Datadog.Trace;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Jupiter.Implementation;
using Jupiter.Utils;

namespace Horde.Storage.Implementation
{
    public interface IReferenceResolver
    {
        IAsyncEnumerable<BlobIdentifier> ResolveReferences(NamespaceId ns, CbObject cb);
    }

    public class ReferenceResolver : IReferenceResolver
    {
        private readonly IBlobService _blobStore;
        private readonly IContentIdStore _contentIdStore;

        public ReferenceResolver(IBlobService blobStore, IContentIdStore contentIdStore)
        {
            _blobStore = blobStore;
            _contentIdStore = contentIdStore;
        }

        public async IAsyncEnumerable<BlobIdentifier> ResolveReferences(NamespaceId ns, CbObject cb)
        {
            // TODO: This is cacheable and we should store the result somewhere
            Queue<CbObject> objectsToVisit = new Queue<CbObject>();
            objectsToVisit.Enqueue(cb);
            List<ContentId> unresolvedContentIdReferences = new List<ContentId>();
            List<BlobIdentifier> unresolvedBlobReferences = new List<BlobIdentifier>();

            List<Task<(ContentId, BlobIdentifier[]?)>> pendingContentIdResolves = new();
            List<Task<CbObject>> pendingCompactBinaryAttachments = new();

            while(pendingCompactBinaryAttachments.Count != 0 || pendingContentIdResolves.Count != 0 || objectsToVisit.Count != 0)
            {
                if (objectsToVisit.TryDequeue(out CbObject? parent))
                {
                    // enumerate all fields in the compact binary and start to resolve their dependencies
                    foreach (CbField field in parent)
                    {
                        if (!field.IsAttachment())
                            continue;

                        IoHash attachmentHash = field.AsAttachment();
                        if (field.HasError())
                            continue;

                        BlobIdentifier blobIdentifier = BlobIdentifier.FromIoHash(attachmentHash);
                        if (field.IsBinaryAttachment())
                        {
                            pendingContentIdResolves.Add(ResolveContentId(ns, ContentId.FromIoHash(attachmentHash)));
                        }
                        else
                        {
                            // the identifier as is points to a blob
                            yield return blobIdentifier;
                        }

                        // if its a reference to another compact binary we need to fetch it and resolve it
                        if (!field.IsObjectAttachment())
                            continue;

                        pendingCompactBinaryAttachments.Add(ParseCompactBinaryAttachment(ns, blobIdentifier));
                    }
                }

                List<Task<(ContentId, BlobIdentifier[]?)>> contentIdResolvesToRemove = new();

                foreach (Task<(ContentId, BlobIdentifier[]?)> pendingContentIdResolveTask in pendingContentIdResolves)
                {
                    // check for any content id resolve that has finished and return those blobs it found
                    if (pendingContentIdResolveTask.IsCompleted)
                    {
                        (ContentId contentId, BlobIdentifier[]? resolvedBlobs) = await pendingContentIdResolveTask;
                        if (resolvedBlobs != null)
                        {
                            foreach (BlobIdentifier b in resolvedBlobs)
                            {
                                yield return b;
                            }
                        }
                        else
                        {
                            unresolvedContentIdReferences.Add(contentId);
                        }
                        contentIdResolvesToRemove.Add(pendingContentIdResolveTask);
                    }
                }

                // cleanup finished tasks
                foreach (Task<(ContentId, BlobIdentifier[]?)> finishedTask in contentIdResolvesToRemove)
                {
                    pendingContentIdResolves.Remove(finishedTask);
                }

                // check for any compact binary attachment fetches and add those to the objects we are handling
                List<Task<CbObject>> finishedCompactBinaryResolves = new();
                foreach (Task<CbObject> pendingCompactBinaryAttachment in pendingCompactBinaryAttachments)
                {
                    if (pendingCompactBinaryAttachment.IsCompleted)
                    {
                        try
                        {
                            CbObject childBinaryObject = await pendingCompactBinaryAttachment;
                            objectsToVisit.Enqueue(childBinaryObject);
                        }
                        catch (BlobNotFoundException e)
                        {
                            unresolvedBlobReferences.Add(e.Blob);
                        }
                        finishedCompactBinaryResolves.Add(pendingCompactBinaryAttachment);
                    }
                }

                // cleanup finished tasks
                foreach (Task<CbObject> finishedTask in finishedCompactBinaryResolves)
                {
                    pendingCompactBinaryAttachments.Remove(finishedTask);
                }
            }

            if (unresolvedContentIdReferences.Count != 0)
            {
                throw new PartialReferenceResolveException(unresolvedContentIdReferences);
            }

            if (unresolvedBlobReferences.Count != 0)
            {
                throw new ReferenceIsMissingBlobsException(unresolvedBlobReferences);
            }
        }

        private async Task<CbObject> ParseCompactBinaryAttachment(NamespaceId ns, BlobIdentifier blobIdentifier)
        {
            BlobContents contents = await _blobStore.GetObject(ns, blobIdentifier);
            byte[] data = await contents.Stream.ToByteArray();
            CbObject childBinaryObject = new CbObject(data);

            return childBinaryObject;
        }

        private async Task<(ContentId, BlobIdentifier[]?)> ResolveContentId(NamespaceId ns, ContentId contentId)
        {
            using IScope scope = Tracer.Instance.StartActive("ReferenceResolver.ResolveContentId");
            scope.Span.ResourceName = contentId.ToString();
            BlobIdentifier[]? resolvedBlobs = await _contentIdStore.Resolve(ns, contentId);
            return (contentId, resolvedBlobs);
        }
    }

    public class PartialReferenceResolveException : Exception
    {
        public List<ContentId> UnresolvedReferences { get; }

        public PartialReferenceResolveException(List<ContentId> unresolvedReferences) : base($"References missing: {string.Join(',', unresolvedReferences)}")
        {
            UnresolvedReferences = unresolvedReferences;
        }
    }

    public class ReferenceIsMissingBlobsException : Exception
    {
        public List<BlobIdentifier> MissingBlobs { get; }

        public ReferenceIsMissingBlobsException(List<BlobIdentifier> missingBlobs) : base($"References is missing these blobs: {string.Join(',', missingBlobs)}")
        {
            MissingBlobs = missingBlobs;
        }
    }
}
