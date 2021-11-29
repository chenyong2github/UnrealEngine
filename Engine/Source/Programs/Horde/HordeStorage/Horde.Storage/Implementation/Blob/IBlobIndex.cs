// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using Jupiter.Implementation;

namespace Horde.Storage.Implementation.Blob
{
    public interface IBlobIndex
    {
        public class BlobInfo
        {
            public HashSet<string> Regions = new HashSet<string>();
        }

        Task AddBlobToIndex(NamespaceId ns, BlobIdentifier id);

        Task<BlobInfo?> GetBlobInfo(NamespaceId ns, BlobIdentifier id);

        Task<bool> RemoveBlobFromIndex(NamespaceId ns, BlobIdentifier id);
        Task<bool> BlobExistsInRegion(NamespaceId ns, BlobIdentifier blobIdentifier);
    }
}
