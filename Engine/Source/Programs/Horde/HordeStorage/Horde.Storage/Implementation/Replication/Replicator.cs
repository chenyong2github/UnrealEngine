// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Threading;
using System.Threading.Tasks;
using Datadog.Trace;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;
using Newtonsoft.Json;
using Nito.AsyncEx;
using Nito.AsyncEx.Synchronous;
using RestSharp;
using RestSharp.Serializers.NewtonsoftJson;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class ReplicatorV1 : Replicator<TransactionEvent>
    {
        private readonly ITransactionLogWriter _transactionLogWriter;

        public ReplicatorV1(ReplicatorSettings replicatorSettings, IOptionsMonitor<ReplicationSettings> replicationSettings, IBlobStore blobStore, ITransactionLogWriter transactionLogWriter, IServiceCredentials serviceCredentials) : base(replicatorSettings, replicationSettings, blobStore, CreateRemoteClient(replicatorSettings, serviceCredentials))
        {
            _transactionLogWriter = transactionLogWriter;
        }

        internal ReplicatorV1(ReplicatorSettings replicatorSettings, IOptionsMonitor<ReplicationSettings> replicationSettings, IBlobStore blobStore, ITransactionLogWriter transactionLogWriter, IRestClient remoteClient) : base(replicatorSettings, replicationSettings, blobStore, remoteClient)
        {
            _transactionLogWriter = transactionLogWriter;
        }

        protected override IAsyncEnumerable<TransactionEvent> GetCallistoOp(long stateReplicatorOffset, Guid? stateReplicatingGeneration, string currentSite,
            OpsEnumerationState enumerationState, CancellationToken replicationToken)
        {
            CallistoReader remoteCallistoReader = new CallistoReader(_restClient, _replicatorSettings.NamespaceToReplicate);

            return remoteCallistoReader.GetOps(stateReplicatorOffset, stateReplicatingGeneration, currentSite, enumerationState: enumerationState, cancellationToken: replicationToken);
        }

        protected override async Task ReplicateOp(IRestClient remoteClient, TransactionEvent op, CancellationToken replicationToken)
        {
            using Scope scope = Tracer.Instance.StartActive("replicator.replicate_blobs");
            NamespaceId ns = _replicatorSettings.NamespaceToReplicate;

            // now we replicate the blobs
            switch (op)
            {
                case AddTransactionEvent addEvent:
                    BlobIdentifier[] blobs = addEvent.Blobs;
                    await ReplicateBlobs(remoteClient, ns, blobs, replicationToken);
                    break;
                case RemoveTransactionEvent removeEvent:
                    // TODO: Do we even want to do anything? we can not delete the blob in the store because it may be used by something else
                    // so we wait for the GC to remove it.
                    break;
                default:
                    throw new ArgumentOutOfRangeException();
            }
        }

        protected override async Task<long?> ReplicateOpInline(IRestClient remoteClient, TransactionEvent op, CancellationToken replicationToken)
        {
            using Scope scope = Tracer.Instance.StartActive("replicator.replicate_inline");
            //scope.Span.ResourceName = op.transactionId;

            NamespaceId ns = _replicatorSettings.NamespaceToReplicate;

            // Put a copy of the event in the local callisto store
            // Make sure this is put before the io put request as we want to pin the reference before uploading the content to avoid the gc removing it right away
            long newTransactionId = await _transactionLogWriter.Add(ns, op);

            _logger.Information("{Name} Replicated Op {@Op} to local store as transaction id {@TransactionId}", _name, op, newTransactionId);

            // TODO: Add Refstore replication
            //_refStore.Add();

            return newTransactionId;
        }

    }

    public abstract class Replicator<T> : IReplicator
        where T: IReplicationEvent
    {
        private readonly IBlobStore _blobStore;
        protected readonly string _name;
        private readonly string _currentSite;
        protected readonly ReplicatorSettings _replicatorSettings;
        protected readonly ILogger _logger = Log.ForContext<Replicator<T>>();

        private readonly FileInfo _stateFile;
        private bool _replicationRunning;
        private readonly AsyncManualResetEvent _replicationFinishedEvent = new AsyncManualResetEvent(true);
        private readonly CancellationTokenSource _replicationTokenSource = new CancellationTokenSource();
        private readonly IReplicator.ReplicatorInfo _replicatorInfo;
        protected readonly IRestClient _restClient;


        public IReplicator.ReplicatorState State { get; private set; }

        public IReplicator.ReplicatorInfo Info
        {
            get { return _replicatorInfo; }
        }

        protected static IRestClient CreateRemoteClient(ReplicatorSettings replicatorSettings, IServiceCredentials serviceCredentials)
        {
            var remoteClient = new RestClient(replicatorSettings.ConnectionString).UseSerializer(() => new JsonNetSerializer());
            remoteClient.Authenticator = serviceCredentials.GetAuthenticator();
            return remoteClient;
        }

        protected Replicator(ReplicatorSettings replicatorSettings, IOptionsMonitor<ReplicationSettings> replicationSettings, IBlobStore blobStore, IRestClient remoteClient)
        {
            _name = replicatorSettings.ReplicatorName;
            _blobStore = blobStore;
            _currentSite = replicationSettings.CurrentValue.CurrentSite;
            _replicatorSettings = replicatorSettings;

            string stateFileName = $"{_name}.json";
            DirectoryInfo stateRoot = new DirectoryInfo(replicationSettings.CurrentValue.StateRoot);
            _stateFile = new FileInfo(Path.Combine(stateRoot.FullName, stateFileName));

            if (_stateFile.Exists)
            {
                State = ReadState(_stateFile) ?? new IReplicator.ReplicatorState {ReplicatorOffset = null, ReplicatingGeneration = null};
            }
            else
            {
                State = new IReplicator.ReplicatorState {ReplicatorOffset = null, ReplicatingGeneration = null};
            }

            _replicatorInfo = new IReplicator.ReplicatorInfo(_replicatorSettings.ReplicatorName, _replicatorSettings.NamespaceToReplicate, State);
            _restClient = remoteClient;
        }

        ~Replicator()
        {
            Dispose();
        }

        /// <summary>
        /// Attempt to run a new replication, if a replication is already in flight for this replicator this will early exist.
        /// </summary>
        /// <returns>True if the replication actually attempted to run</returns>
        public async Task<bool> TriggerNewReplications()
        {
            if (_replicationRunning)
            {
                return false;
            }

            bool hasRun = false;
            OpsEnumerationState enumerationState = new();
            DateTime startedReplicationAt = DateTime.Now;
            long countOfReplicatedEvents = 0L;
            try
            {
                using Scope scope = Tracer.Instance.StartActive("replicator.run");
                scope.Span.ResourceName =_name;

                _replicationRunning = true;
                _replicationFinishedEvent.Reset();
                CancellationToken replicationToken = _replicationTokenSource.Token;

                // read the state again to allow it to be modified by the admin controller
                _stateFile.Refresh();
                if (_stateFile.Exists)
                {
                    State = ReadState(_stateFile) ?? new IReplicator.ReplicatorState {ReplicatorOffset = null, ReplicatingGeneration = null};
                }
                else
                {
                    State = new IReplicator.ReplicatorState {ReplicatorOffset = null, ReplicatingGeneration = null};
                }

                LogReplicationHeartbeat(_name, State, 0);
                _logger.Information("{Name} Looking for new transaction. Previous state: {@State}", _name, State);

                SortedList<long, Task> replicationTasks = new();
                await foreach (T op in GetCallistoOp(State.ReplicatorOffset ?? 0, State.ReplicatingGeneration, _currentSite, enumerationState, replicationToken))
                {
                    hasRun = true;
                    _logger.Information("{Name} New transaction to replicate found. New op: {@Op} . Count of running replications: {CurrentReplications}", _name, op, replicationTasks.Count);

                    Info.CountOfRunningReplications = replicationTasks.Count;

                    if (!op.Identifier.HasValue || !op.NextIdentifier.HasValue)
                    {
                        _logger.Error("{Name} Missing identifier in {@Op}", _name, op);
                        throw new Exception("Missing identifier in op. Version mismatch?");
                    }
                    
                    long currentOffset = op.Identifier.Value;
                    long nextIdentifier = op.NextIdentifier.Value;
                    Guid replicatingGeneration = enumerationState.ReplicatingGeneration;
                    try
                    {
                        // first replicate the things which are order sensitive like the transaction log
                        long? _ = await ReplicateOpInline(_restClient, op, replicationToken);

                        // next we replicate the large things that overlap with other replications like blobs
                        Task currentReplicationTask = ReplicateOp(_restClient, op, replicationToken);

                        Task OnDone(Task task)
                        {
                            Interlocked.Increment(ref countOfReplicatedEvents);
                            try
                            {
                                bool wasOldest;
                                lock (replicationTasks)
                                {
                                    wasOldest = currentOffset <= replicationTasks.First().Key;
                                }

                                if (task.IsFaulted)
                                {
                                    // rethrow the exception if there was any
                                    task.WaitAndUnwrapException(replicationToken);
                                }

                                // only update the state when we have replicated everything up that point
                                if (wasOldest)
                                {
                                    // we have finished replicating 
                                    State.ReplicatorOffset = nextIdentifier;
                                    State.ReplicatingGeneration = replicatingGeneration;
                                    SaveState(_stateFile, State);
                                }

                                Info.LastRun = DateTime.Now;
                                Info.CountOfRunningReplications = replicationTasks.Count;

                                LogReplicationHeartbeat(_name, State, replicationTasks.Count);

                                return task;
                            }
                            finally
                            {
                                lock (replicationTasks)
                                {
                                    replicationTasks.Remove(currentOffset);
                                }
                            }
                        }

                        lock (replicationTasks)
                        {
                            Task<Task> commitStateTask = currentReplicationTask.ContinueWith(OnDone, replicationToken);

                            replicationTasks.Add(currentOffset, commitStateTask);
                        }
                    }
                    catch (BlobNotFoundException)
                    {
                        _logger.Warning("{Name} Failed to replicate {@Op} in {Namespace} because blob was not present in remote store. Skipping.", _name, op, Info.NamespaceToReplicate);
                    }

                    // if we have reached the max amount of parallel replications we wait for one of them to finish before starting a new one
                    // if max replications is set to -1 we do not limit the concurrency
                    if ( _replicatorSettings.MaxParallelReplications != -1 && replicationTasks.Count >= _replicatorSettings.MaxParallelReplications)
                    {
                        Task[] currentReplicationTasks;
                        lock (replicationTasks)
                        {
                            currentReplicationTasks = replicationTasks.Values.ToArray();
                        }
                        await Task.WhenAny(currentReplicationTasks);
                    }

                    Info.CountOfRunningReplications = replicationTasks.Count;
                    LogReplicationHeartbeat(_name, State, replicationTasks.Count);

                    if (replicationToken.IsCancellationRequested)
                    {
                        break;
                    }
                }

                // make a copy to avoid the collection being modified while we wait for it
                List<Task> tasksToWaitFor = replicationTasks.Values.ToList();
                await Task.WhenAll(tasksToWaitFor);
            }
            finally
            {
                _replicationRunning = false;
                _replicationFinishedEvent.Set();
            }

            DateTime endedReplicationAt = DateTime.Now;
            _logger.Information("{Name} {Namespace} Replication Finished at {EndTime} and last run was at {LastTime}, replicated {CountOfEvents} events. Thus was {TimeDifference} minutes behind.", _name, Info.NamespaceToReplicate, endedReplicationAt, startedReplicationAt, countOfReplicatedEvents, (endedReplicationAt - startedReplicationAt).TotalMinutes);

            if (!hasRun)
            {
                // if no events were found we still persist the offsets we have run thru so we don't have to read them again.
                State.ReplicatorOffset = enumerationState.LastOffset;
                State.ReplicatingGeneration = enumerationState.ReplicatingGeneration;
                SaveState(_stateFile, State);
            }

            return hasRun;
        }

        protected abstract IAsyncEnumerable<T> GetCallistoOp(long stateReplicatorOffset, Guid? stateReplicatingGeneration, string currentSite, OpsEnumerationState enumerationState, CancellationToken replicationToken);
        
        private void LogReplicationHeartbeat(string name, IReplicator.ReplicatorState? state, int countOfCurrentReplications)
        {
            if (state == null)
                return;

            // log message used to generate metric for how many replications are currently running
            _logger.Information("{Name} replication has run . Count of running replications: {CurrentReplications}", _name, countOfCurrentReplications);

            // log message used to verify replicators are actually running
            _logger.Information("{Name} starting replication. Last transaction was {TransactionId} {Generation}", name, state.ReplicatorOffset.GetValueOrDefault(0L), state.ReplicatingGeneration.GetValueOrDefault(Guid.Empty) );
        }

        protected abstract Task<long?> ReplicateOpInline(IRestClient remoteClient, T op, CancellationToken replicationToken);

        protected abstract Task ReplicateOp(IRestClient remoteClient, T op, CancellationToken replicationToken);

        protected async Task ReplicateBlobs(IRestClient remoteClient, NamespaceId ns, BlobIdentifier[] blobs, CancellationToken replicationToken)
        {
            Task[] blobReplicateTasks = new Task[blobs.Length];
            for (int index = 0; index < blobs.Length; index++)
            {
                BlobIdentifier blob = blobs[index];
                blobReplicateTasks[index] = Task.Run(async () =>
                {
                    // check if this blob exists locally before replicating
                    if (await _blobStore.Exists(ns, blob))
                        return;

                    // attempt to replicate for a few tries with some delay in between
                    // this because new transactions are written to callisto first (to establish the GC handle) before content is uploaded to io.
                    // as such its possible (though not very likely) when we replicate the very newest transaction that we find a transaction but no content
                    // and as the content upload can be large it can take a little time for it to exist in io.
                    byte[]? rawContent = null;
                    BlobIdentifier? calculatedBlob = null;
                    const int MaxAttempts = 3;
                    for (int attempts = 0; attempts < MaxAttempts; attempts++)
                    {
                        RestRequest remoteIoGet = new RestRequest("api/v1/s/{ns}/{blob}");
                        remoteIoGet.AddUrlSegment("ns", ns);
                        remoteIoGet.AddUrlSegment("blob", blob);

                        IRestResponse response = await remoteClient.ExecuteGetAsync(remoteIoGet, replicationToken);

                        if (response.StatusCode == HttpStatusCode.NotFound)
                        {
                            _logger.Information("{Blob} was not found in remote blob store. Retry attempt {Attempts}", blob, attempts);
                            await Task.Delay(TimeSpan.FromSeconds(5), replicationToken);
                            continue;
                        }

                        if (response.StatusCode == HttpStatusCode.GatewayTimeout)
                        {
                            _logger.Information("GatewayTimeout while replicating {Blob}, retrying. Retry attempt {Attempts}", blob, attempts);
                            continue;
                        }

                        if (response.StatusCode == HttpStatusCode.BadRequest || response.StatusCode == HttpStatusCode.NotFound)
                        {
                            _logger.Warning("Remote blob {Blob} missing, unable to replicate.", blob);
                            return;
                        }

                        if (response.IsSuccessful)
                        {
                            rawContent = response.RawBytes;

                            calculatedBlob = BlobIdentifier.FromBlob(rawContent);
                            if (!blob.Equals(calculatedBlob))
                            {
                                _logger.Warning("Mismatching blob when replicating {Blob}. Determined Hash was {Hash} size was {Size} HttpStatusCode {StatusCode} HttpMessage {HttpMessage} ResponseUri {Url}", blob, calculatedBlob, rawContent.LongLength, response.StatusCode, response.ErrorMessage, response.ResponseUri);
                                continue; // attempt to replicate again
                            }
    
                            // this blob is good and should be added to the blob store
                            break;
                        }

                        if (response.ErrorException is WebException we)
                        {
                            if (we.Status == WebExceptionStatus.Timeout)
                            {
                                const string template = "Operation timed out while replicating {Blob}, retrying. Retry attempt {Attempts}";
                                _logger.Information(we, template, blob, attempts);
                                continue;
                            }
                        }

                        throw new Exception($"Replicator \"{_name}\" failed to replicate blob {blob} due unsuccessful status from remote blob store: {response.StatusCode} . Error message: \"{response.ErrorMessage}\"", response.ErrorException);
                    }

                    if (rawContent == null || calculatedBlob == null)
                    {
                        _logger.Warning("Remote blob {Blob} not present in remote blob store after multiple attempts to replicate. Assuming this blob has been GCed and continuing replication.", blob);
                        return;
                    }

                    if (!blob.Equals(calculatedBlob))
                    {
                        _logger.Warning("Mismatching blob when replicating {Blob}. Determined Hash was {Hash} size was {Size}. Multiple attempts failed, giving up.", blob, calculatedBlob, rawContent.LongLength);
                        return; 
                    }

                    await _blobStore.PutObject(ns, rawContent, calculatedBlob);
                }, replicationToken);
            }

            await Task.WhenAll(blobReplicateTasks);
        }

        private static IReplicator.ReplicatorState? ReadState(FileInfo stateFile)
        {
            using StreamReader streamReader = stateFile.OpenText();
            using JsonReader reader = new JsonTextReader(streamReader);
            JsonSerializer serializer = new JsonSerializer();
            return serializer.Deserialize<IReplicator.ReplicatorState>(reader);
        }

        private static void SaveState(FileInfo stateFile, IReplicator.ReplicatorState newState)
        {
            using StreamWriter writer = stateFile.CreateText();
            JsonSerializer serializer = new JsonSerializer();
            serializer.Serialize(writer, newState);
        }

        public void Dispose()
        {
            SaveState(_stateFile, State);

            _replicationTokenSource.Dispose();
        }

        public async Task StopReplicating()
        {
            _replicationTokenSource.Cancel(true);
            await _replicationFinishedEvent.WaitAsync();
        }

        public void SetReplicationOffset(long offset)
        {
            State.ReplicatorOffset = offset;
            SaveState(_stateFile, State);
        }

        public Task DeleteState()
        {
            State = new IReplicator.ReplicatorState()
            {
                ReplicatingGeneration = null,
                ReplicatorOffset = 0
            };
            SaveState(_stateFile, State);

            return Task.CompletedTask;
        }
    }
}
