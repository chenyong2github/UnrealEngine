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
using EpicGames.Horde.Storage;
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

        IAsyncEnumerable<(BlobIdentifier,DateTime)> ListObjects(NamespaceId ns);
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

}
