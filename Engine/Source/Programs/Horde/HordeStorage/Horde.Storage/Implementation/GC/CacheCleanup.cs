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
        private readonly ITransactionLogWriter _transactionLog;
        private readonly ILogger _logger = Log.ForContext<RefCleanup>();

        public RefCleanup(IOptionsMonitor<GCSettings> settings, IRefsStore refs, ITransactionLogWriter transactionLog)
        {
            _settings = settings;
            _refs = refs;
            _transactionLog = transactionLog;
        }

        public async Task<List<OldRecord>> Cleanup(NamespaceId ns, CancellationToken cancellationToken)
        {
            using Scope scope = Tracer.Instance.StartActive("gc.refs.namespace");
            scope.Span.ResourceName = ns.ToString();
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
