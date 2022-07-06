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
    public abstract class Attachment
    {
        public abstract IoHash AsIoHash();
    }

    public class BlobAttachment : Attachment
    {
        public BlobIdentifier Identifier { get; }

        public BlobAttachment(BlobIdentifier blobIdentifier)
        {
            Identifier = blobIdentifier;
        }

        public override IoHash AsIoHash()
        {
            return Identifier.AsIoHash();
        }
    }

    public class ObjectAttachment : Attachment
    {
        public BlobIdentifier Identifier { get; }

        public ObjectAttachment(BlobIdentifier blobIdentifier)
        {
            Identifier = blobIdentifier;
        }

        public override IoHash AsIoHash()
        {
            return Identifier.AsIoHash();
        }
    }

    public class ContentIdAttachment : Attachment
    {
        public ContentId Identifier { get; }

        public ContentIdAttachment(ContentId contentId)
        {
            Identifier = contentId;
        }

        public override IoHash AsIoHash()
        {
            return Identifier.AsIoHash();
        }
    }

    public interface IReferenceResolver
    {

        IAsyncEnumerable<BlobIdentifier> GetReferencedBlobs(NamespaceId ns, CbObject cb);
        IAsyncEnumerable<Attachment> GetAttachments(NamespaceId ns, CbObject cb);
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

        public async IAsyncEnumerable<Attachment> GetAttachments(NamespaceId ns, CbObject cb)
        {
            // TODO: This is cacheable and we should store the result somewhere
            Queue<CbObject> objectsToVisit = new Queue<CbObject>();
            objectsToVisit.Enqueue(cb);
            List<BlobIdentifier> unresolvedBlobReferences = new List<BlobIdentifier>();

            List<Task<CbObject>> pendingCompactBinaryAttachments = new();

            while (pendingCompactBinaryAttachments.Count != 0 || objectsToVisit.Count != 0)
            {
                if (objectsToVisit.TryDequeue(out CbObject? parent))
                {
                    List<Attachment> attachments = new List<Attachment>();

                    parent.IterateAttachments(field =>
                    {
                        IoHash attachmentHash = field.AsAttachment();

                        BlobIdentifier blobIdentifier = BlobIdentifier.FromIoHash(attachmentHash);
                        ContentId contentId = ContentId.FromIoHash(attachmentHash);

                        if (field.IsBinaryAttachment())
                        {
                            bool isContentId = false;
                            try
                            {
                                // TODO: Having to do a proper resolve here is not good as its quite expensive
                                // it would be much better if the attachment type indicated if it was a content id or not
                                Task<BlobIdentifier[]?> resolveContentId = _contentIdStore.Resolve(ns, contentId, mustBeContentId: true);
                                resolveContentId.Wait();
                                isContentId = resolveContentId.Result != null;
                            }
                            catch (InvalidContentIdException)
                            {
                                isContentId = false;
                            }
                            
                            if (isContentId)
                            {
                                attachments.Add(new ContentIdAttachment(contentId));
                            }
                            else
                            {
                                attachments.Add(new BlobAttachment(blobIdentifier));
                            }
                        }
                        else if (field.IsObjectAttachment())
                        {
                            attachments.Add(new ObjectAttachment(blobIdentifier));
                            pendingCompactBinaryAttachments.Add(ParseCompactBinaryAttachment(ns, blobIdentifier));
                        }
                        else
                        {
                            throw new NotImplementedException($"Unknown attachment type for field {field}");
                        }
                    });

                    foreach (Attachment attachment in attachments)
                    {
                        yield return attachment;
                    }
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
                if (pendingCompactBinaryAttachments.Any())
                {
                    await Task.WhenAny(pendingCompactBinaryAttachments);
                }
            }

            if (unresolvedBlobReferences.Count != 0)
            {
                throw new ReferenceIsMissingBlobsException(unresolvedBlobReferences);
            }
        }

        public async IAsyncEnumerable<BlobIdentifier> GetReferencedBlobs(NamespaceId ns, CbObject cb)
        {
            List<Task<(ContentId, BlobIdentifier[]?)>> pendingContentIdResolves = new();
            List<ContentId> unresolvedContentIdReferences = new List<ContentId>();

            // Resolve all the attachments
            await foreach (Attachment attachment in GetAttachments(ns, cb))
            {
                if (attachment is BlobAttachment blobAttachment)
                {
                    yield return blobAttachment.Identifier;
                }
                else if (attachment is ContentIdAttachment contentIdAttachment)
                {
                    // If we find a content id we resolve that into the actual blobs it references
                    pendingContentIdResolves.Add(ResolveContentId(ns, contentIdAttachment.Identifier));
                }
                else if (attachment is ObjectAttachment objectAttachment)
                {
                    // a object just references the same blob, traversing the object attachment is done in GetAttachments
                    yield return objectAttachment.Identifier;
                }
                else
                {
                    throw new NotSupportedException($"Unknown attachment type {attachment.GetType()}");
                }
            }

            // Return the results of all the content id resolves
            foreach (Task<(ContentId, BlobIdentifier[]?)> pendingContentIdResolveTask in pendingContentIdResolves)
            {
                (ContentId contentId, BlobIdentifier[]? resolvedBlobs) =  await pendingContentIdResolveTask;
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
            }

            // if there were any content ids we did not recognize we throw a partial reference exception
            if (unresolvedContentIdReferences.Count != 0)
            {
                throw new PartialReferenceResolveException(unresolvedContentIdReferences);
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
