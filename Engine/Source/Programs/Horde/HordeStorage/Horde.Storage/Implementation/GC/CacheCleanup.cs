// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Datadog.Trace;
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
            using Scope scope = Tracer.Instance.StartActive("gc.refs.namespace");
            scope.Span.ResourceName = ns.ToString();
            if (_settings.CurrentValue.CleanNamespacesV1.Contains(ns.ToString()))
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
            DateTime cutoffTime = DateTime.Now.Add(_settings.CurrentValue.LastAccessCutoff);
            await foreach (ObjectRecord record in _referencesStore.GetOldestRecords(ns).WithCancellation(cancellationToken))
            {
                // the record are returned oldest first, so once we find a record that is newer then our cutoff time we can stop iterating
                if (record.LastAccess > cutoffTime)
                    break;

                // delete the old record from the ref refs
                Task storeDelete = _referencesStore.Delete(record.Namespace, record.Bucket, record.Name);
                // insert a delete event into the transaction log
                Task<(string, Guid)> transactionLogDelete = _replicationLog.InsertDeleteEvent(record.Namespace, record.Bucket, record.Name, record.BlobIdentifier);

                try
                {
                    Task.WaitAll(storeDelete, transactionLogDelete);
                }
                catch (Exception e)
                {
                    _logger.Warning(e, "Exception when attempting to delete record {Record} in {Namespace}", record, ns);
                }
                // we convert the ObjectRecords key (which is a iohash) to a keyid (which is a generic string) not the prettiest but this result is only used for debugging purposes anyway
                deletedRecords.Add(new OldRecord(record.Namespace, record.Bucket, new KeyId(record.Name.ToString())));
            }

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
