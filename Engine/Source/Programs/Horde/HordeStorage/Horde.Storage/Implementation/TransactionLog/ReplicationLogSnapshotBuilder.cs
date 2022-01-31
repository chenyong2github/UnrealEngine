// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using Jupiter.Implementation;

namespace Horde.Storage.Implementation.TransactionLog
{
    public class ReplicationLogSnapshotBuilder
    {
        private readonly IReplicationLog _replicationLog;
        private readonly IBlobService _blobService;
        private readonly IObjectService _objectService;

        public ReplicationLogSnapshotBuilder(IReplicationLog replicationLog, IBlobService blobService, IObjectService objectService)
        {
            _replicationLog = replicationLog;
            _blobService = blobService;
            _objectService = objectService;
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
                await using BlobContents blobContents = await _blobService.GetObject(snapshotInfo.BlobNamespace, snapshotInfo.SnapshotBlob);
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


            byte[] buf;
            {
                await using MemoryStream ms = new MemoryStream();
                await snapshot.Serialize(ms);
                await ms.FlushAsync(cancellationToken);
                buf = ms.ToArray();
            }
            
            
            {
                BlobIdentifier blobIdentifier = BlobIdentifier.FromBlob(buf);

                CompactBinaryWriter writer = new CompactBinaryWriter();
                writer.BeginObject();
                writer.AddBinaryAttachment(blobIdentifier, "snapshotBlob");
                writer.AddDateTime(DateTime.Now, "timestamp");
                writer.EndObject();

                byte[] cbObjectBytes = writer.Save();
                BlobIdentifier cbBlobId = BlobIdentifier.FromBlob(cbObjectBytes);

                if (cancellationToken.IsCancellationRequested)
                    throw new TaskCanceledException();
                
                // upload the attachment first so we are not missing any references when we go to create the ref
                await _blobService.PutObject(storeInNamespace, buf, blobIdentifier);
                
                (ContentId[] missingContentIds, BlobIdentifier[] missingBlobs) = await _objectService.Put(storeInNamespace, new BucketId("snapshot"), new IoHashKey(blobIdentifier.ToString()), cbBlobId, CompactBinaryObject.Load(cbObjectBytes));
                List<ContentHash> missingHashes = new List<ContentHash>(missingContentIds);
                missingHashes.AddRange(missingBlobs);
                if (missingHashes.Count != 0)
                    throw new Exception($"Failed to upload snapshot to object service, missing references {string.Join(',' , missingHashes.Select(b => b.ToString()))}");

                if (cancellationToken.IsCancellationRequested)
                    throw new TaskCanceledException();

                // update the replication log with the new snapshot
                await _replicationLog.AddSnapshot(new SnapshotInfo(ns, storeInNamespace, blobIdentifier, DateTime.Now));

                return blobIdentifier;

            }
        }
    }
}
