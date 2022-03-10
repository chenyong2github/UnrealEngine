// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;
using Cassandra;
using Cassandra.Mapping;
using Datadog.Trace;
using EpicGames.Horde.Storage;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class ScyllaReferencesStore : IReferencesStore
    {
        private readonly ISession _session;
        private readonly IMapper _mapper;
        private readonly IOptionsMonitor<ScyllaSettings> _settings;
        private readonly ILogger _logger = Log.ForContext<ScyllaReferencesStore>();
        private readonly PreparedStatement _getObjectsStatement;
        private readonly PreparedStatement _getNamespacesStatement;

        public ScyllaReferencesStore(IScyllaSessionManager scyllaSessionManager, IOptionsMonitor<ScyllaSettings> settings)
        {
            _session = scyllaSessionManager.GetSessionForReplicatedKeyspace();
            _settings = settings;

            _mapper = new Mapper(_session);

            _session.Execute(new SimpleStatement(@"CREATE TABLE IF NOT EXISTS objects (
                namespace text, 
                bucket text, 
                name text, 
                payload_hash blob_identifier, 
                inline_payload blob, 
                is_finalized boolean,
                last_access_time timestamp,
                PRIMARY KEY ((namespace, bucket, name))
            );"
            ));
            
            _session.Execute(new SimpleStatement(@"CREATE TABLE IF NOT EXISTS buckets (
                namespace text, 
                bucket set<text>, 
                PRIMARY KEY (namespace)
            );"
            ));

            _session.Execute(new SimpleStatement(@"CREATE TABLE IF NOT EXISTS object_last_access (
                namespace text,
                partition_index tinyint,
                accessed_at date,
                objects set<text>,
                PRIMARY KEY ((namespace, partition_index, accessed_at))
            );"
            ));


            _getObjectsStatement = _session.Prepare("SELECT bucket, name, last_access_time FROM objects WHERE namespace = ? ALLOW FILTERING");
            _getNamespacesStatement = _session.Prepare("SELECT DISTINCT namespace FROM buckets");
        }

        public async Task<ObjectRecord> Get(NamespaceId ns, BucketId bucket, IoHashKey name, IReferencesStore.FieldFlags flags)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.get");

            ScyllaObject? o;
            bool includePayload = (flags & IReferencesStore.FieldFlags.IncludePayload) != 0;
            if (includePayload)
            {
                o = await _mapper.SingleOrDefaultAsync<ScyllaObject>("WHERE namespace = ? AND bucket = ? AND name = ?", ns.ToString(), bucket.ToString(), name.ToString());
            }
            else
            {
                // fetch everything except for the inline blob which is quite large
                o = await _mapper.SingleOrDefaultAsync<ScyllaObject>("SELECT namespace, bucket, name , payload_hash, is_finalized, last_access_time FROM objects WHERE namespace = ? AND bucket = ? AND name = ?", ns.ToString(), bucket.ToString(), name.ToString());
            }

            if (o == null)
                throw new ObjectNotFoundException(ns, bucket, name);

            if (o.RequiredFieldsAreMissing())
            {
                _logger.Warning("Partial object found {Namespace} {Bucket} {Name} ignoring object", ns, bucket, name);
                throw new ObjectNotFoundException(ns, bucket, name);
            }

            return new ObjectRecord(new NamespaceId(o.Namespace!), new BucketId(o.Bucket!), new IoHashKey(o.Name!), o.LastAccessTime, o.InlinePayload, o.PayloadHash!.AsBlobIdentifier(), o.IsFinalized!.Value);
        }

        public async Task Put(NamespaceId ns, BucketId bucket, IoHashKey name, BlobIdentifier blobHash, byte[] blob, bool isFinalized)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.put");
            if (blob.LongLength > _settings.CurrentValue.InlineBlobMaxSize)
            {
                // do not inline large blobs
                blob = Array.Empty<byte>();
            }

            // add the bucket in parallel with inserting the actual object
            var addBucketTask = MaybeAddBucket(ns, bucket);
            // add the ttl records in parallel with the object
            var createTTLRecords = CreateTTLRecord(ns, bucket, name, (sbyte)blobHash.HashData[0]);

            await _mapper.InsertAsync<ScyllaObject>(new ScyllaObject(ns, bucket, name, blob, blobHash, isFinalized));
            await Task.WhenAll(addBucketTask, createTTLRecords);
        }


        public async Task Finalize(NamespaceId ns, BucketId bucket, IoHashKey name, BlobIdentifier blobIdentifier)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.finalize");

            await _mapper.UpdateAsync<ScyllaObject>("SET is_finalized=true WHERE namespace=? AND bucket=? AND name=?", ns.ToString(), bucket.ToString(), name.ToString());
        }

        private async Task CreateTTLRecord(NamespaceId ns, BucketId bucket, IoHashKey name, sbyte partitionIndex)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.create_ttl_record");
            DateTime now = DateTime.Now;
            LocalDate lastAccessAt = new LocalDate(now.Year, now.Month, now.Day);
            await _mapper.UpdateAsync<ScyllaObjectLastAccessAt>("SET objects = objects + ? WHERE accessed_at = ? AND namespace = ? AND partition_index = ? ", new string[] {$"{bucket}#{name}"},  lastAccessAt, ns.ToString(), partitionIndex);
        }

        private Task RemoveTTLRecord(ObjectRecord record)
        {
            return RemoveTTLRecord(record.Namespace, record.Bucket, record.Name, record.LastAccess, (sbyte)record.BlobIdentifier.HashData[0]);
        }

        private async Task RemoveTTLRecord(NamespaceId ns, BucketId bucket, IoHashKey name, DateTime lastAccess, sbyte partitionIndex)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.remove_ttl_record");
            LocalDate lastAccessAt = new LocalDate(lastAccess.Year, lastAccess.Month, lastAccess.Day);
            await _mapper.UpdateAsync<ScyllaObjectLastAccessAt>("SET objects = objects - ? WHERE accessed_at = ? AND namespace = ? AND partition_index = ?", new string[] {$"{bucket}#{name}"},  lastAccessAt, ns.ToString(), partitionIndex);
        }

        public async Task UpdateLastAccessTime(NamespaceId ns, BucketId bucket, IoHashKey name, DateTime lastAccessTime)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.update_last_access_time");
            // fetch the old record
            /* ObjectRecord objectRecord = await Get(ns, bucket, name);

            // update the object tracking
            Task updateObjectTracking = _mapper.UpdateAsync<ScyllaObject>("SET last_access_time = ? WHERE namespace=? AND bucket = ? AND name = ?", lastAccessTime, ns.ToString(), bucket.ToString(), name.ToString());

            // the object was accessed during the same day so no need to move it
            if (objectRecord.LastAccess.Date != lastAccessTime.Date)
            {
                sbyte partitionIndex = (sbyte)objectRecord.BlobIdentifier.HashData[0];
                // move which last access partition is used for the object
                LocalDate lastAccessAt = new LocalDate(objectRecord.LastAccess.Year, objectRecord.LastAccess.Month, objectRecord.LastAccess.Day);
                await _mapper.UpdateAsync<ScyllaObjectLastAccessAt>("SET objects = objects - ? WHERE accessed_at=? AND namespace = ? AND partition_index = ?", new string[] {$"{bucket}#{name}"}, lastAccessAt, ns.ToString(), partitionIndex);
                LocalDate newLastAccessAt = new LocalDate(lastAccessTime.Year, lastAccessTime.Month, lastAccessTime.Day);
                await _mapper.UpdateAsync<ScyllaObjectLastAccessAt>("SET objects = objects + ? WHERE accessed_at=? AND namespace = ? AND partition_index = ?", new string[] {$"{bucket}#{name}"}, newLastAccessAt, ns.ToString(), partitionIndex);
            }

            await updateObjectTracking;*/

            await _mapper.UpdateAsync<ScyllaObject>("SET last_access_time = ? WHERE namespace = ? AND bucket = ? AND name = ?", lastAccessTime, ns.ToString(), bucket.ToString(), name.ToString());

            
        }

        /*public async IAsyncEnumerable<ObjectRecord> GetRecords(NamespaceId ns)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.get_records");
            for (sbyte partitionIndex = sbyte.MinValue; partitionIndex < sbyte.MaxValue; partitionIndex++)
            {
                using IScope scope = Tracer.Instance.StartActive("scylla.get_records_partition");
                scope.Span.ResourceName = $"partition_{partitionIndex}";

                SortedSet<DateTime> dateBuckets = new SortedSet<DateTime>();
                {
                    using IScope dateBucketsScope = Tracer.Instance.StartActive("scylla.get_records.date_buckets");

                    RowSet rowSetDateBuckets = await _session.ExecuteAsync(new SimpleStatement("SELECT accessed_at FROM object_last_access WHERE namespace = ? AND partition_index = ? PER PARTITION LIMIT 1 ALLOW FILTERING", ns.ToString(), partitionIndex));
                    foreach (Row row in rowSetDateBuckets)
                    {
                        if (rowSetDateBuckets.GetAvailableWithoutFetching() == 0)
                            await rowSetDateBuckets.FetchMoreResultsAsync();

                        LocalDate date = row.GetValue<LocalDate>("accessed_at");

                        dateBuckets.Add(new DateTime(date.Year, date.Month, date.Day));
                    }
                }


                foreach (DateTime dateBucket in dateBuckets)
                {
                    LocalDate date = new LocalDate(dateBucket.Year, dateBucket.Month, dateBucket.Day);
                    foreach (ScyllaObjectLastAccessAt lastAccessRecord in await _mapper.FetchAsync<ScyllaObjectLastAccessAt>("WHERE namespace = ? AND accessed_at = ? AND partition_index = ?", ns.ToString(), date, partitionIndex))
                    {
                        foreach (string o in lastAccessRecord.Objects)
                        {
                            int bucketSeparator = o.IndexOf("#", StringComparison.InvariantCultureIgnoreCase);
                            BucketId bucket = new BucketId(o.Substring(0, bucketSeparator));
                            IoHashKey name = new IoHashKey(o.Substring(bucketSeparator + 1));

                            ObjectRecord record;
                            try
                            {
                                 record = await Get(ns, bucket, name);
                            }
                            catch (ObjectNotFoundException)
                            {
                                await RemoveTTLRecord(ns, bucket, name, dateBucket, partitionIndex);
                                continue;
                            }
                            yield return record;
                        }
                    }
                }

            }
        }*/
        
        public async IAsyncEnumerable<(BucketId, IoHashKey, DateTime)> GetRecords(NamespaceId ns)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.get_records");

            RowSet rowSet = await _session.ExecuteAsync(_getObjectsStatement.Bind(ns.ToString()));
            foreach (Row row in rowSet)
            {
                if (rowSet.GetAvailableWithoutFetching() == 0)
                    await rowSet.FetchMoreResultsAsync();

                string bucket = row.GetValue<string>("bucket");
                string name = row.GetValue<string>("name");
                DateTime lastAccessTime = row.GetValue<DateTime>("last_access_time");

                // skip any names that are not conformant to io hash
                if (name.Length != 40)
                    continue;
                yield return (new BucketId(bucket), new IoHashKey(name), lastAccessTime);
            }
        }

        public async IAsyncEnumerable<NamespaceId> GetNamespaces()
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.get_namespaces");
            RowSet rowSet = await _session.ExecuteAsync(_getNamespacesStatement.Bind());

            foreach (Row row in rowSet)
            {
                if (rowSet.GetAvailableWithoutFetching() == 0)
                    await rowSet.FetchMoreResultsAsync();

                yield return new NamespaceId(row.GetValue<string>(0));
            }
        }

        public async Task<bool> Delete(NamespaceId ns, BucketId bucket, IoHashKey key)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.delete_record");
            ObjectRecord record;
            try
            {
                record = await Get(ns, bucket, key, IReferencesStore.FieldFlags.None);
            }
            catch (ObjectNotFoundException)
            {
                // if the record does not exist we do not need to do anything
                return false;
            }

            Task removeTTL = RemoveTTLRecord(record);
            AppliedInfo<ScyllaObject> info = await _mapper.DeleteIfAsync<ScyllaObject>("WHERE namespace=? AND bucket=? AND name=?", ns.ToString(), bucket.ToString(), key.ToString());

            await removeTTL;
            if (info.Applied)
                return true;

            return false;
        }

        public async Task<long> DropNamespace(NamespaceId ns)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.delete_namespace");
            RowSet rowSet = await _session.ExecuteAsync(new SimpleStatement("SELECT bucket, name FROM objects WHERE namespace = ? ALLOW FILTERING;", ns.ToString()));
            long deletedCount = 0;
            foreach (Row row in rowSet)
            {
                string bucket = row.GetValue<string>("bucket");
                string name = row.GetValue<string>("name");

                await Delete(ns, new BucketId(bucket), new IoHashKey(name));

                deletedCount++;
            }

            // remove the tracking in the buckets table as well
            await _session.ExecuteAsync(new SimpleStatement("DELETE FROM buckets WHERE namespace = ?", ns.ToString()));

            return deletedCount;
        }

        public async Task<long> DeleteBucket(NamespaceId ns, BucketId bucket)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.delete_bucket");
            RowSet rowSet = await _session.ExecuteAsync(new SimpleStatement("SELECT name FROM objects WHERE namespace = ? AND bucket = ? ALLOW FILTERING;", ns.ToString(), bucket.ToString()));
            long deletedCount = 0;
            foreach (Row row in rowSet)
            {
                string name = row.GetValue<string>("name");

                await Delete(ns, bucket, new IoHashKey(name));
                deletedCount++;
            }

            // remove the tracking in the buckets table as well
            await _mapper.UpdateAsync<ScyllaBucket>("SET bucket = bucket - ? WHERE namespace = ?", new string[] {bucket.ToString()}, ns.ToString());

            return deletedCount;
        }

        private async Task MaybeAddBucket(NamespaceId ns, BucketId bucket)
        {
            using IScope _ = Tracer.Instance.StartActive("scylla.add_bucket");
            await _mapper.UpdateAsync<ScyllaBucket>("SET bucket = bucket + ? WHERE namespace = ?", new string[] {bucket.ToString()}, ns.ToString());
        }
    }

    public class ObjectHashMismatchException : Exception
    {
        public ObjectHashMismatchException(NamespaceId ns, BucketId bucket, IoHashKey name, BlobIdentifier suppliedHash, BlobIdentifier actualHash) : base($"Object {name} in bucket {bucket} and namespace {ns} did not reference hash {suppliedHash} was referencing {actualHash}")
        {
        }
    }

    public class ScyllaBlobIdentifier
    {
        public ScyllaBlobIdentifier()
        {
            Hash = null;
        }

        public ScyllaBlobIdentifier(ContentHash hash)
        {
            Hash = hash.HashData;
        }

        public byte[]? Hash { get;set; }

        public BlobIdentifier AsBlobIdentifier()
        {
            return new BlobIdentifier(Hash!); 
        }
    }

    public class ScyllaObjectReference
    {
        // used by the cassandra mapper
        public ScyllaObjectReference()
        {
            Bucket = null!;
            Key = null!;
        }

        public ScyllaObjectReference(BucketId bucket, IoHashKey key)
        {
            Bucket = bucket.ToString();
            Key = key.ToString();
        }

        public string Bucket { get;set; }
        public string Key { get; set; }

        public (BucketId, IoHashKey) AsTuple()
        {
            return (new BucketId(Bucket), new IoHashKey(Key));
        }
    }

    public class ScyllaSettings
    {
        public string ConnectionString { get; set; } = "Contact Points=localhost,scylla";
        public long InlineBlobMaxSize { get; set; } = 32 * 1024; // default to 32 kb blobs max
        public string[] ContactPoints { get; set; } = new string[] {"localhost", "scylla"};
        public int MaxSnapshotsPerNamespace { get; set; } = 10;

        /// <summary>
        /// Set to override the replication strategy used to create keyspace
        /// Note that this only applies when creating the keyspace
        /// To modify a existing keyspace see https://docs.datastax.com/en/dse/6.7/dse-admin/datastax_enterprise/operations/opsChangeKSStrategy.html
        /// </summary>
        public Dictionary<string, string>? KeyspaceReplicationStrategy { get; set; }

        /// <summary>
        /// Set to override the replication strategy used to create the local keyspace
        /// Note that this only applies when creating the keyspace
        /// To modify a existing keyspace see https://docs.datastax.com/en/dse/6.7/dse-admin/datastax_enterprise/operations/opsChangeKSStrategy.html
        /// </summary>
        public Dictionary<string, string>? LocalKeyspaceReplicationStrategy { get; set; }

        /// <summary>
        /// Used to configure the load balancing policy to stick to this specified datacenter
        /// </summary>
        [Required]
        public string LocalDatacenterName { get; set; } = null!;
        
        [Required] 
        public string LocalKeyspaceSuffix { get; set; } = null!;

        /// <summary>
        /// Max number of connections for each scylla host before switching to another host
        /// See https://docs.datastax.com/en/developer/nodejs-driver/4.6/features/connection-pooling/
        /// </summary>
        public int MaxConnectionForLocalHost { get; set; } = 8192;

        /// <summary>
        /// The time for a replication log event to live in the incremental state before being deleted, assumption is that the snapshot will have processed the event within this time
        /// </summary>
        public TimeSpan ReplicationLogTimeToLive { get; set; } = TimeSpan.FromDays(7);

        /// <summary>
        /// Whether a Cosmos DB Cassandra layer from Azure should be used.
        /// </summary>
        public bool UseAzureCosmosDB { get; set; } = false;
    }

    [Cassandra.Mapping.Attributes.Table("objects")]
    public class ScyllaObject
    {
        public ScyllaObject()
        {

        }

        public ScyllaObject(NamespaceId ns, BucketId bucket, IoHashKey name, byte[] payload, BlobIdentifier payloadHash, bool isFinalized)
        {
            Namespace = ns.ToString();
            Bucket = bucket.ToString();
            Name = name.ToString();
            InlinePayload = payload;
            PayloadHash = new ScyllaBlobIdentifier(payloadHash);

            IsFinalized = isFinalized;

            LastAccessTime = DateTime.Now;
        }

        [Cassandra.Mapping.Attributes.PartitionKey(0)]
        public string? Namespace { get; set; }

        [Cassandra.Mapping.Attributes.PartitionKey(1)]
        public string? Bucket { get; set; }

        [Cassandra.Mapping.Attributes.PartitionKey(2)]
        public string? Name { get; set; }

        [Cassandra.Mapping.Attributes.Column("payload_hash")]
        public ScyllaBlobIdentifier? PayloadHash { get; set; }

        [Cassandra.Mapping.Attributes.Column("inline_payload")]
        public byte[]? InlinePayload {get; set; }

        [Cassandra.Mapping.Attributes.Column("is_finalized")]
        public bool? IsFinalized { get;set; }
        [Cassandra.Mapping.Attributes.Column("last_access_time")]
        public DateTime LastAccessTime { get; set; }

        public bool RequiredFieldsAreMissing()
        {
            if (string.IsNullOrEmpty(Namespace))
                //throw new ArgumentException("Namespace was not valid", nameof(Namespace));
                return true;
            if (string.IsNullOrEmpty(Bucket))
                //throw new ArgumentException("Bucket was not valid", nameof(Bucket));
                return true;
            if (string.IsNullOrEmpty(Name))
                //throw new ArgumentException("Name was not valid", nameof(Name));
                return true;
            if (PayloadHash == null)
                //throw new ArgumentException("PayloadHash was not valid", nameof(PayloadHash));
                return true;
            if (!IsFinalized.HasValue)
                //throw new ArgumentException("IsFinalized was not valid", nameof(IsFinalized));
                return true;

            return false;
        }
    }

    [Cassandra.Mapping.Attributes.Table("buckets")]
    public class ScyllaBucket
    {
        public ScyllaBucket()
        {

        }

        public ScyllaBucket(NamespaceId ns, BucketId[] buckets)
        {
            Namespace = ns.ToString();
            Buckets = buckets.Select(b => b.ToString()).ToList();
        }

        [Cassandra.Mapping.Attributes.PartitionKey]
        public string? Namespace { get; set; }

        public List<string> Buckets { get; set; } = new List<string>();
    }

    [Cassandra.Mapping.Attributes.Table("object_last_access")]
    public class ScyllaObjectLastAccessAt
    {
        public ScyllaObjectLastAccessAt()
        {

        }

        [Cassandra.Mapping.Attributes.PartitionKey(0)]
        public string? Namespace { get; set; }

        [Cassandra.Mapping.Attributes.PartitionKey(1)]
        [Cassandra.Mapping.Attributes.Column("partition_index")]
        public sbyte? PartitionIndex { get;set; }

        [Cassandra.Mapping.Attributes.PartitionKey(2)]
        [Cassandra.Mapping.Attributes.Column("accessed_at")]
        public LocalDate? AccessedAt { get; set; }


        public List<string> Objects { get; set; } = new List<string>();

    }
}
