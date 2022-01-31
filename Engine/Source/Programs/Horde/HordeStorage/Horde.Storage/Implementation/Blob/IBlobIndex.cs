// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using Jupiter.Implementation;

namespace Horde.Storage.Implementation.Blob
{
    public interface IBlobIndex
    {
        public class BlobInfo
        {
            public HashSet<string> Regions { get; init; } = new HashSet<string>();
            public NamespaceId Namespace { get; init; }
            public List<(BucketId, IoHashKey)> References { get; init; } = new List<(BucketId, IoHashKey)>();
        }

        Task AddBlobToIndex(NamespaceId ns, BlobIdentifier id);

        Task<BlobInfo?> GetBlobInfo(NamespaceId ns, BlobIdentifier id);

        Task<bool> RemoveBlobFromIndex(NamespaceId ns, BlobIdentifier id);
        Task<bool> BlobExistsInRegion(NamespaceId ns, BlobIdentifier blobIdentifier);
        Task AddRefToBlobs(NamespaceId ns, BucketId bucket, IoHashKey key, BlobIdentifier[] blobs);
    }
}
