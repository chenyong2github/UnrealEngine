// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading.Tasks;
using Jupiter.Implementation;

namespace Horde.Storage.Implementation
{
    public interface IContentIdStore
    {
        /// <summary>
        /// Resolve a content id from its hash into the actual blob (that can in turn be chunked into a set of blobs)
        /// </summary>
        /// <param name="ns">The namespace to operate in</param>
        /// <param name="contentId">The identifier for the content id</param>
        /// <returns></returns>
        Task<BlobIdentifier[]?> Resolve(NamespaceId ns, BlobIdentifier contentId);

        /// <summary>
        /// Add a mapping from contentId to blobIdentifier
        /// </summary>
        /// <param name="ns">The namespace to operate in</param>
        /// <param name="contentId">The contentId</param>
        /// <param name="blobIdentifier">The blob the content id maps to</param>
        /// <param name="contentWeight">Weight of this identifier compared to previous mappings, used to determine which is more important, lower weight is considered a better fit</param>
        /// <returns></returns>
        Task Put(NamespaceId ns, BlobIdentifier contentId, BlobIdentifier blobIdentifier, int contentWeight);
    }

    public class InvalidContentIdException : Exception
    {
        public InvalidContentIdException(BlobIdentifier contentId) : base($"Unknown content id {contentId}")
        {
            
        }
    }

    public class ContentIdResolveException : Exception
    {
        public BlobIdentifier ContentId { get; }

        public ContentIdResolveException(BlobIdentifier contentId): base($"Unable to find any mapping of contentId {contentId} that has all blobs present")
        {
            ContentId = contentId;
        }
    }
}
