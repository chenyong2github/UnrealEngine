// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Jupiter.Implementation;

namespace Horde.Storage.Implementation.TransactionLog
{
    public class ReplicationLogSnapshotBuilder
    {
        private readonly IReplicationLog _replicationLog;
        private readonly IBlobStore _blobStore;
        private readonly IReferencesStore _referencesStore;

        public ReplicationLogSnapshotBuilder(IReplicationLog replicationLog, IBlobStore blobStore, IReferencesStore referencesStore)
        {
            _replicationLog = replicationLog;
            _blobStore = blobStore;
            _referencesStore = referencesStore;
        }

        public async Task<BlobIdentifier> BuildSnapshot(NamespaceId ns, NamespaceId storeInNamespace, CancellationToken cancellationToken = default(CancellationToken))
        {
            // builds a snapshot and commits it to the blob store with the identifier specified

            SnapshotInfo? snapshotInfo = await _replicationLog.GetLatestSnapshot(ns);

            if (cancellationToken.IsCancellationRequested)
                throw new TaskCanceledException();

            ReplicationLogSnapshot snapshot;
            string? lastBucket;
            Guid? lastEvent;
            if (snapshotInfo != null)
            {
                // append to the previous snapshot if one is available
                await using BlobContents blobContents = await _blobStore.GetObject(snapshotInfo.BlobNamespace, snapshotInfo.SnapshotBlob);
                if (cancellationToken.IsCancellationRequested)
                    throw new TaskCanceledException();
                snapshot = await ReplicationLogSnapshot.DeserializeSnapshot(blobContents.Stream);
                lastBucket = snapshot.LastBucket;
                lastEvent = snapshot.LastEvent;
            }
            else
            {
                snapshot = new ReplicationLogSnapshot(ns);

                lastBucket = null;
                lastEvent = null;
            }

            if (cancellationToken.IsCancellationRequested)
                throw new TaskCanceledException();

            await foreach (ReplicationLogEvent entry in _replicationLog.Get(ns, lastBucket, lastEvent))
            {
                if (cancellationToken.IsCancellationRequested)
                    throw new TaskCanceledException();

                snapshot.ProcessEvent(entry);
            }

            {
                await using MemoryStream ms = new MemoryStream();
                await snapshot.Serialize(ms);
                byte[] buf = ms.GetBuffer();
                BlobIdentifier blobIdentifier = BlobIdentifier.FromBlob(buf);


                CompactBinaryWriter writer = new CompactBinaryWriter();
                writer.BeginObject();
                writer.AddBinaryAttachment(blobIdentifier, "snapshotBlob");
                writer.AddDateTime(DateTime.Now, "timestamp");
                writer.EndObject();

                byte[] cbObjectBytes = writer.Save();
                BlobIdentifier cbBlobId = BlobIdentifier.FromBlob(cbObjectBytes);
                await _referencesStore.Put(storeInNamespace, new BucketId("snapshot"), new KeyId(blobIdentifier.ToString()), cbBlobId, cbObjectBytes, true);
                await _blobStore.PutObject(storeInNamespace, buf, blobIdentifier);

                if (cancellationToken.IsCancellationRequested)
                    throw new TaskCanceledException();

                // update the replication log with the new snapshot
                await _replicationLog.AddSnapshot(new SnapshotInfo(ns, storeInNamespace, blobIdentifier));

                return blobIdentifier;

            }
        }
    }
}
