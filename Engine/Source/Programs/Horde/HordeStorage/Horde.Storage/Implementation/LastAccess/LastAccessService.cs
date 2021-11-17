// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Datadog.Trace;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    // ReSharper disable once ClassNeverInstantiated.Global
    public class LastAccessService : IHostedService, IDisposable
    {
        private readonly ILastAccessCache<RefRecord> _lastAccessCacheRecord;
        private readonly IRefsStore _refsStore;
        private readonly ILogger _logger = Log.ForContext<LastAccessService>();
        private Timer? _timer;
        private readonly HordeStorageSettings _settings;
        
        public bool Running { get; private set; }

        public LastAccessService(IOptionsMonitor<HordeStorageSettings> settings, ILastAccessCache<RefRecord> lastAccessCache, IRefsStore refsStore)
        {
            _lastAccessCacheRecord = lastAccessCache;
            _refsStore = refsStore;
            _settings = settings.CurrentValue;
        }

        public Task StartAsync(CancellationToken cancellationToken)
        {
            _logger.Information("Last Access Aggregation service starting.");

            _timer = new Timer(OnUpdate, null, TimeSpan.Zero,
                period: TimeSpan.FromSeconds(_settings.LastAccessRollupFrequencySeconds));
            Running = true;

            return Task.CompletedTask;
        }

        public async Task StopAsync(CancellationToken cancellationToken)
        {
            _logger.Information("Last Access Aggregation service stopping.");

            _timer?.Change(Timeout.Infinite, 0);
            Running = false;

            // process the last records we have built up
            await ProcessLastAccessRecords();
        }

        private void OnUpdate(object? state)
        {
            // call results to make sure we join the task
            Task.WaitAll(
                ProcessLastAccessRecords()
            );
        }

        internal async Task<List<(RefRecord, DateTime)>> ProcessLastAccessRecords()
        {
            using Scope _ = Tracer.Instance.StartActive("lastAccess.update");
            _logger.Information("Running Last Access Aggregation");
            List<(RefRecord, DateTime)> records = await _lastAccessCacheRecord.GetLastAccessedRecords();
            foreach ((RefRecord record, DateTime lastAccessTime) in records)
            {
                using Scope scope = Tracer.Instance.StartActive("lastAccess.update.record");
                scope.Span.ResourceName = $"{record.Namespace}:{record.Bucket}.{record.RefName}";
                _logger.Debug("Updating last access time to {LastAccessTime} for {Record}", lastAccessTime, record);
                await _refsStore.UpdateLastAccessTime(record, lastAccessTime);
            }

            return records;
        }

        public void Dispose()
        {
            _timer?.Dispose();
        }
    }
}
