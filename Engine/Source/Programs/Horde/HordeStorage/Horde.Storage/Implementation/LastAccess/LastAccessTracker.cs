// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class LastAccessTrackerRefRecord : LastAccessTracker<RefRecord>
    {
        protected override string BuildCacheKey(RefRecord record)
        {
            return $"{record.Namespace}.{record.Bucket}.{record.RefName}";
        }
    }
    public abstract class LastAccessTracker<T> : ILastAccessTracker<T>, ILastAccessCache<T>
    {
        private readonly ILogger _logger = Log.ForContext<LastAccessTracker<T>>();

        private ConcurrentDictionary<string, LastAccessRecord> _cache = new ConcurrentDictionary<string, LastAccessRecord>();

        // we will exchange the refs dictionary when fetching the records and use a rw lock to make sure no-one is trying to add things at the same time
        private readonly ReaderWriterLock _rwLock = new ReaderWriterLock();


        protected abstract string BuildCacheKey(T record);

        public Task TrackUsed(T record)
        {
            return Task.Run(() =>
            {
                try
                {
                    _rwLock.AcquireReaderLock(-1);

                    _logger.Debug("Last Access time updated for {@RefRecord}", record);
                    string cacheKey = BuildCacheKey(record);
                    _cache.AddOrUpdate(cacheKey, _ => new LastAccessRecord(record, DateTime.Now),
                        (_, cacheRecord) =>
                        {
                            cacheRecord.LastAccessTime = DateTime.Now;
                            return cacheRecord;
                        });
                }
                finally
                {
                    _rwLock.ReleaseLock();
                }
            });
        }

        public async Task<List<(T, DateTime)>> GetLastAccessedRecords()
        {
            return await Task.Run(() =>
            {
                try
                {
                    _rwLock.AcquireWriterLock(-1);

                    _logger.Debug("Last Access Records collected");
                    ConcurrentDictionary<string, LastAccessRecord> localReference = _cache;

                    _cache = new ConcurrentDictionary<string, LastAccessRecord>();

                    // ToArray is important here to make sure this is thread safe as just using linq queries on a concurrent dict is not thead safe
                    // http://blog.i3arnon.com/2018/01/16/concurrent-dictionary-tolist/
                    return localReference.ToArray().Select(pair => (pair.Value.Record, pair.Value.LastAccessTime))
                        .ToList();
                }
                finally
                {
                    _rwLock.ReleaseWriterLock();
                }
            });
        }

        private class LastAccessRecord
        {
            public T Record { get; }
            public DateTime LastAccessTime { get; set; }

            public LastAccessRecord(T record, in DateTime lastAccessTime)
            {
                Record = record;
                LastAccessTime = lastAccessTime;
            }
        }
    }

}
