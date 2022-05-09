// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
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
                    List<BlobIdentifier> blobIdentifiers = new List<BlobIdentifier>();

                    parent.IterateAttachments(field =>
                    {
                        IoHash attachmentHash = field.AsAttachment();

                        BlobIdentifier blobIdentifier = BlobIdentifier.FromIoHash(attachmentHash);
                        if (field.IsBinaryAttachment())
                        {
                            pendingContentIdResolves.Add(ResolveContentId(ns, ContentId.FromIoHash(attachmentHash)));
                        }
                        else if (field.IsObjectAttachment())
                        {
                            blobIdentifiers.Add(blobIdentifier);
                            pendingCompactBinaryAttachments.Add(ParseCompactBinaryAttachment(ns, blobIdentifier));
                        }
                        else
                        { 
                            throw new NotImplementedException($"Unknown attachment type for field {field}");
                        }
                    });

                    foreach (BlobIdentifier blobIdentifier in blobIdentifiers)
                    {
                        yield return blobIdentifier;
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

                // if there are pending resolves left, wait for one of them to finish to avoid busy waiting
                if (pendingContentIdResolves.Any() || pendingCompactBinaryAttachments.Any())
                {
                    await Task.WhenAny(pendingContentIdResolves.Concat<Task>(pendingCompactBinaryAttachments));
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
