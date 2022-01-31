// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Datadog.Trace;
using EpicGames.Horde.Storage;
using Jupiter.Common;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    public interface IRefCleanup
    {
        Task<List<OldRecord>> Cleanup(NamespaceId ns, CancellationToken cancellationToken);
    }

    public class RefCleanup : IRefCleanup
    {
        private readonly IOptionsMonitor<GCSettings> _settings;
        private readonly IRefsStore _refs;
        private readonly IReferencesStore _referencesStore;
        private readonly IReplicationLog _replicationLog;
        private readonly ITransactionLogWriter _transactionLog;
        private readonly ILogger _logger = Log.ForContext<RefCleanup>();

        public RefCleanup(IOptionsMonitor<GCSettings> settings, IRefsStore refs, ITransactionLogWriter transactionLog, IReferencesStore referencesStore, IReplicationLog replicationLog)
        {
            _settings = settings;
            _refs = refs;
            _transactionLog = transactionLog;
            _referencesStore = referencesStore;
            _replicationLog = replicationLog;
        }

        public Task<List<OldRecord>> Cleanup(NamespaceId ns, CancellationToken cancellationToken)
        {
            using IScope scope = Tracer.Instance.StartActive("gc.refs.namespace");
            scope.Span.ResourceName = ns.ToString();
            if (ns == INamespacePolicyResolver.JupiterInternalNamespace)
            {
                // do not apply our cleanup policies to the internal namespace
                return Task.FromResult(new List<OldRecord>());
            }
            else if (_settings.CurrentValue.CleanNamespacesV1.Contains(ns.ToString()))
            {
                return CleanNamespaceV1(ns, cancellationToken);
            }
            else if (_settings.CurrentValue.CleanNamespaces.Contains(ns.ToString()))
            {
                return CleanNamespace(ns, cancellationToken);
            }
            else
                throw new NotImplementedException(
                    $"Namespace {ns} not present in CleanNamespaces or CleanNamespacesV1 lists, unable to clean it as we do not know which method to use.");
           
        }

        private async Task<List<OldRecord>> CleanNamespace(NamespaceId ns, CancellationToken cancellationToken)
        {
            List<OldRecord> deletedRecords = new List<OldRecord>();
            DateTime cutoffTime = DateTime.Now.AddSeconds(-1 * _settings.CurrentValue.LastAccessCutoff.TotalSeconds);
            int consideredCount = 0;
            await foreach ((BucketId bucket, IoHashKey name, DateTime lastAccessTime) in _referencesStore.GetRecords(ns).WithCancellation(cancellationToken))
            {
                _logger.Debug("Considering object in {Namespace} {Bucket} {Name} for deletion, was last updated {LastAccessTime}", ns, bucket, name, lastAccessTime);
                Interlocked.Increment(ref consideredCount);

                if (lastAccessTime > cutoffTime)
                    continue;

                _logger.Information("Attempting to delete object {Namespace} {Bucket} {Name} as it was last updated {LastAccessTime} which is older then {CutoffTime}", ns, bucket, name, lastAccessTime, cutoffTime);
                using IScope scope = Tracer.Instance.StartActive("refCleanup.delete_record");
                scope.Span.ResourceName = $"{ns}:{bucket}.{name}";
                // delete the old record from the ref refs
                Task<bool> storeDelete = _referencesStore.Delete(ns, bucket, name);
                // insert a delete event into the transaction log
                Task<(string, Guid)> transactionLogDelete = _replicationLog.InsertDeleteEvent(ns, bucket, name, null);

                try
                {
                    Task.WaitAll(storeDelete, transactionLogDelete);
                }
                catch (Exception e)
                {
                    _logger.Warning(e, "Exception when attempting to delete record {Bucket} {Name} in {Namespace}", bucket, name, ns);
                }

                if (await storeDelete)
                {
                    // we convert the ObjectRecords key (which is a iohash) to a keyid (which is a generic string) not the prettiest but this result is only used for debugging purposes anyway
                    deletedRecords.Add(new OldRecord(ns, bucket, new KeyId(name.ToString())));
                }
                else
                {
                    _logger.Warning("Failed to delete record {Bucket} {Name} in {Namespace}", bucket, name, ns);            
                }
            }

            _logger.Information("Finished cleaning {Namespace}. Refs considered: {ConsideredCount} Refs Deleted: {DeletedCount}", ns, consideredCount, deletedRecords.Count);

            return deletedRecords;
        }

        private async Task<List<OldRecord>> CleanNamespaceV1(NamespaceId ns, CancellationToken cancellationToken)
        {
            List<OldRecord> deletedRecords = new List<OldRecord>();
            await foreach (OldRecord record in _refs.GetOldRecords(ns, _settings.CurrentValue.LastAccessCutoff).WithCancellation(cancellationToken))
            {
                // delete the old record from the ref refs
                Task storeDelete = _refs.Delete(record.Namespace, record.Bucket, record.RefName);
                // insert a delete event into the transaction log
                Task transactionLogDelete = _transactionLog.Delete(record.Namespace, record.Bucket, record.RefName);

                try
                {
                    Task.WaitAll(storeDelete, transactionLogDelete);
                }
                catch (Exception e)
                {
                    _logger.Warning(e, "Exception when attempting to delete record {Record} in {Namespace}", record, ns);
                }
                deletedRecords.Add(record);
            }

            return deletedRecords;
        }
    }
}
