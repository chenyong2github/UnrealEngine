// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Threading.Tasks;
using Jupiter.Implementation;
using Newtonsoft.Json;

namespace Horde.Storage.Implementation.TransactionLog
{
    public class ReplicationLogSnapshot
    {
        private readonly NamespaceId _namespace;
        private readonly Dictionary<string, SnapshotLiveObject> _liveObjects = new Dictionary<string, SnapshotLiveObject>();

        public class ReplicationLogSnapshotState
        {
            public ReplicationLogSnapshotState(SnapshotHeader header, List<SnapshotLiveObject> liveObjects)
            {
                Header = header;
                LiveObjects = liveObjects;
            }

            public SnapshotHeader Header { get; set; }
            public List<SnapshotLiveObject> LiveObjects { get; set; }
        }

        public class SnapshotHeader
        {
            public NamespaceId Namespace { get; set; }
            public string? LastBucket { get; set; }
            public Guid LastEvent { get; set; }
        }

        public class SnapshotLiveObject
        {
            public SnapshotLiveObject(BucketId bucket, KeyId key, BlobIdentifier blob)
            {
                Bucket = bucket;
                Key = key;
                Blob = blob;
            }

            public BucketId Bucket { get; set; }
            public KeyId Key { get; set; }
            public BlobIdentifier Blob { get; set; }
        }

        public ReplicationLogSnapshot(NamespaceId ns)
        {
            _namespace = ns;
        }

        private ReplicationLogSnapshot(ReplicationLogSnapshotState snapshotState)
        {
            _namespace = snapshotState.Header.Namespace;
            LastEvent = snapshotState.Header.LastEvent;
            LastBucket = snapshotState.Header.LastBucket;

            _liveObjects = snapshotState.LiveObjects.ToDictionary(o => BuildObjectKey(o.Bucket, o.Key));
        }

        public Guid? LastEvent { get; set; }
        public string? LastBucket { get; set; }

        public IEnumerable<SnapshotLiveObject> LiveObjects
        {
            get { return _liveObjects.Values; }
        }

        public static async Task<ReplicationLogSnapshot> DeserializeSnapshot(Stream stream)
        {
            await using GZipStream gzipStream = new GZipStream(stream, CompressionMode.Decompress);
            using TextReader textReader = new StreamReader(gzipStream);
            using JsonReader reader = new JsonTextReader(textReader);
            JsonSerializer serializer = JsonSerializer.Create();
            ReplicationLogSnapshotState? snapshotState = serializer.Deserialize<ReplicationLogSnapshotState>(reader);
            if (snapshotState == null)
            {
                throw new NotImplementedException();
            }
            return new ReplicationLogSnapshot(snapshotState);

        }

        public async Task Serialize(Stream stream)
        {
            // apply gzip on the stream as we have a lot of redundant text from bucket, but also within the keys
            await using GZipStream gzipStream = new GZipStream(stream, CompressionMode.Compress);
            await using TextWriter textWriter = new StreamWriter(gzipStream);
            JsonSerializer serializer = JsonSerializer.Create();

            List<SnapshotLiveObject> liveObjects = LiveObjects.ToList();
            if (!liveObjects.Any())
                throw new Exception("You must have at least one live object to build a snapshot");
            if (LastBucket == null)
                throw new Exception("No last bucket found when serializing state, did you really have events?");
            if (LastEvent == null)
                throw new Exception("No last event found when serializing state, did you really have events?");

            ReplicationLogSnapshotState state = new ReplicationLogSnapshotState(new SnapshotHeader()
            {
                Namespace = _namespace,
                LastBucket = LastBucket,
                LastEvent = LastEvent.Value,
            }, liveObjects);
            serializer.Serialize(textWriter, state);
        }

        public void ProcessEvent(ReplicationLogEvent entry)
        {
            LastBucket = entry.TimeBucket;
            LastEvent = entry.EventId;

            switch (entry.Op)
            {
                case ReplicationLogEvent.OpType.Added:
                    ProcessAddEvent(entry.Bucket, entry.Key, entry.Blob);
                    break;
                case ReplicationLogEvent.OpType.Deleted:
                    ProcessDeleteEvent(entry.Bucket, entry.Key);
                    break;
                default:
                    throw new NotImplementedException();
            }
        }


        private string BuildObjectKey(BucketId bucket, KeyId key)
        {
            return $"{bucket}.{key}";
        }

        private void ProcessAddEvent(BucketId bucket, KeyId key, BlobIdentifier blob)
        {
            _liveObjects.TryAdd(BuildObjectKey(bucket, key), new SnapshotLiveObject(bucket, key, blob));
        }
        private void ProcessDeleteEvent(BucketId bucket, KeyId key)
        {
            _liveObjects.Remove(BuildObjectKey(bucket, key));
        }
    }
}
