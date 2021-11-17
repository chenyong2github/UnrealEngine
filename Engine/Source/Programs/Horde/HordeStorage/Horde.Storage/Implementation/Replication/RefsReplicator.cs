// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Dasync.Collections;
using Datadog.Trace;
using Horde.Storage.Controllers;
using Horde.Storage.Implementation.TransactionLog;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using Newtonsoft.Json;
using Nito.AsyncEx;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class RefsReplicator : IReplicator
    {
        private readonly string _name;
        private readonly ILogger _logger = Log.ForContext<RefsReplicator>();
        private readonly FileInfo _stateFile;
        private readonly AsyncManualResetEvent _replicationFinishedEvent = new AsyncManualResetEvent(true);
        private readonly CancellationTokenSource _replicationTokenSource = new CancellationTokenSource();
        private readonly ReplicatorSettings _replicatorSettings;
        private readonly IBlobStore _blobStore;
        private readonly IReplicationLog _replicationLog;
        private readonly IServiceCredentials _serviceCredentials;
        private readonly HttpClient _httpClient;
        private RefsState _refsState;
        private bool _replicationRunning;

        public RefsReplicator(ReplicatorSettings replicatorSettings, IOptionsMonitor<ReplicationSettings> replicationSettings, IBlobStore blobStore, IHttpClientFactory httpClientFactory, IReplicationLog replicationLog, IServiceCredentials serviceCredentials)
        {
            _name = replicatorSettings.ReplicatorName;
            _replicatorSettings = replicatorSettings;
            _blobStore = blobStore;
            _replicationLog = replicationLog;
            _serviceCredentials = serviceCredentials;

            _httpClient = httpClientFactory.CreateClient();
            _httpClient.BaseAddress = new Uri(replicatorSettings.ConnectionString);

            string stateFileName = $"{_name}.json";
            DirectoryInfo stateRoot = new DirectoryInfo(replicationSettings.CurrentValue.StateRoot);
            _stateFile = new FileInfo(Path.Combine(stateRoot.FullName, stateFileName));

            if (_stateFile.Exists)
            {
                _refsState = ReadState(_stateFile)!;
            }
            else
            {
                _refsState = new RefsState();
            }

            Info = new IReplicator.ReplicatorInfo(replicatorSettings.ReplicatorName, replicatorSettings.NamespaceToReplicate, _refsState);
        }

        public void Dispose()
        {
            SaveState(_stateFile, _refsState);

            _replicationTokenSource.Dispose();
        }

        private static RefsState ReadState(FileInfo stateFile)
        {
            using StreamReader streamReader = stateFile.OpenText();
            using JsonReader reader = new JsonTextReader(streamReader);
            JsonSerializer serializer = new JsonSerializer();
            RefsState? state =  serializer.Deserialize<RefsState>(reader);
            if (state == null)
                throw new Exception("Failed to read state");

            return state;
        }

        private static void SaveState(FileInfo stateFile, RefsState newState)
        {
            using StreamWriter writer = stateFile.CreateText();
            JsonSerializer serializer = new JsonSerializer();
            serializer.Serialize(writer, newState);
        }

        public async Task<bool> TriggerNewReplications()
        {
            if (_replicationRunning)
            {
                return false;
            }

            // read the state again to allow it to be modified by the admin controller / other instances of horde-storage connected to the same filesystem
            _stateFile.Refresh();
            if (_stateFile.Exists)
            {
                _refsState = ReadState(_stateFile) ?? new RefsState();
            }
            else
            {
                _refsState = new RefsState();
            }

            LogReplicationHeartbeat(0);

            bool hasRun;

            try
            {
                using Scope scope = Tracer.Instance.StartActive("replicator.run");
                scope.Span.ResourceName =_name;

                _replicationRunning = true;
                _replicationFinishedEvent.Reset();
                CancellationToken replicationToken = _replicationTokenSource.Token;

                NamespaceId ns = _replicatorSettings.NamespaceToReplicate;

                int countOfReplicationsDone = 0;
                Info.CountOfRunningReplications = 0;

                string? lastBucket = null;
                Guid? lastEvent = null;

                bool haveRunBefore = _refsState.LastBucket != null && _refsState.LastEvent != null;
                if (!haveRunBefore)
                {
                    // have not run before, replicate a snapshot
                    _logger.Information("{Name} Have not run replication before, attempting to use snapshot. State: {@State}", _name, _refsState);
                    try
                    {
                        (string eventBucket, Guid eventId, int countOfEventsReplicated) = await ReplicateFromSnapshot(ns, replicationToken);
                        countOfReplicationsDone += countOfEventsReplicated;

                        // finished replicating the snapshot, persist the state
                        _refsState.LastBucket = eventBucket;
                        _refsState.LastEvent = eventId;
                        SaveState(_stateFile, _refsState);
                        hasRun = true;
                    }
                    catch (NoSnapshotAvailableException)
                    {
                        // no snapshot available so we attempt a incremental replication with no bucket set
                        _refsState.LastBucket = null;
                        _refsState.LastEvent = null;
                    }
                }

                lastBucket = _refsState.LastBucket;
                lastEvent = _refsState.LastEvent;

                bool retry;
                bool haveAttemptedSnapshot = false;
                do
                {
                    retry = false;
                    UseSnapshotException? useSnapshotException = null;
                    try
                    {
                        countOfReplicationsDone += await ReplicateIncrementally(ns, lastBucket, lastEvent, replicationToken);
                    }
                    catch (AggregateException ae)
                    {
                        ae.Handle(e =>
                        {
                            if (e is UseSnapshotException snapshotException)
                            {
                                useSnapshotException = snapshotException;
                                return true;
                            }

                            return false;
                        });
                    }
                    catch (UseSnapshotException e)
                    {
                        useSnapshotException = e;
                    }

                    if (useSnapshotException != null)
                    {
                        // if we have already attempted to recover using a snapshot and we still fail we just give up and throw the exception onwards.
                        if (haveAttemptedSnapshot)
                            throw new AggregateException(useSnapshotException);

                        // if we fail to replicate incrementally we revert to using a snapshot
                        haveAttemptedSnapshot = true;
                        (string eventBucket, Guid eventId, int countOfEventsReplicated) = await ReplicateFromSnapshot(ns, replicationToken, useSnapshotException.SnapshotBlob);
                        countOfReplicationsDone += countOfEventsReplicated;

                        // resume from these new events instead
                        lastBucket = eventBucket;
                        lastEvent = eventId;

                        _refsState.LastBucket = eventBucket;
                        _refsState.LastEvent = eventId;
                        SaveState(_stateFile, _refsState);
                        retry = true;
                    }
                    
                } while (retry);
                hasRun = countOfReplicationsDone != 0;
            }
            finally
            {
                _replicationRunning = false;
                _replicationFinishedEvent.Set();
            }

            return hasRun;
        }

        private async Task<(string, Guid, int)> ReplicateFromSnapshot(NamespaceId ns, CancellationToken cancellationToken, BlobIdentifier? snapshotBlob = null)
        {
            // determine latest snapshot if no specific blob was specified
            if (snapshotBlob == null)
            {
                HttpRequestMessage snapshotRequest = BuildHttpRequest(HttpMethod.Get, $"api/v1/replication-log/snapshots/{ns}");
                HttpResponseMessage snapshotResponse = await _httpClient.SendAsync(snapshotRequest, cancellationToken);
                snapshotResponse.EnsureSuccessStatusCode();

                string s = await snapshotResponse.Content.ReadAsStringAsync(cancellationToken);
                ReplicationLogSnapshots? snapshots = JsonConvert.DeserializeObject<ReplicationLogSnapshots>(s);
                if (snapshots == null)
                    throw new NotImplementedException();

                SnapshotInfo? snapshotInfo = snapshots.Snapshots?.FirstOrDefault();
                if (snapshotInfo == null)
                {
                    throw new NoSnapshotAvailableException("No snapshots found");
                }

                snapshotBlob = snapshotInfo.SnapshotBlob;
            }

            // process snapshot
            // fetch the snapshot from the remote blob store
            HttpRequestMessage request = BuildHttpRequest(HttpMethod.Get, $"api/v1/blobs/{ns}/{snapshotBlob}");
            HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken);
            Stream blobStream = await response.Content.ReadAsStreamAsync(cancellationToken);

            ReplicationLogSnapshot snapshot = await ReplicationLogSnapshot.DeserializeSnapshot(blobStream);

            if (!snapshot.LastEvent.HasValue)
                throw new Exception("No last event found");

            Guid snapshotEvent = snapshot.LastEvent.Value;
            string snapshotBucket = snapshot.LastBucket!;
            int countOfObjectsReplicated = 0;
            int countOfObjectsCurrentlyReplicating = 0;

            // if MaxParallelReplications is set to not limit we use the default behavior of ParallelForEachAsync which is to limit based on CPUs 
            int maxParallelism = _replicatorSettings.MaxParallelReplications != -1 ? _replicatorSettings.MaxParallelReplications : 0;
            await snapshot.LiveObjects.ParallelForEachAsync(async snapshotLiveObject =>
            {
                Interlocked.Increment(ref countOfObjectsCurrentlyReplicating);
                LogReplicationHeartbeat(countOfObjectsCurrentlyReplicating);

                Info.CountOfRunningReplications = countOfObjectsCurrentlyReplicating;
                Info.LastRun = DateTime.Now;

                await ReplicateOp(ns, snapshotLiveObject.Blob, cancellationToken);
                // TODO: Avoid adding to the replication log as we will get into infinite recursion if we do with the remote site
                // we could add to the replication log only if the op was missing, but that could cause issue for a 3rd site replicating from us
                // and the 3rd site is the whole reason we even add this to replication log in the first place
                //await AddToReplicationLog(ns, snapshotLiveObject.Bucket, snapshotLiveObject.Key, snapshotLiveObject.Blob);
                
                Interlocked.Increment(ref countOfObjectsReplicated);
                Interlocked.Decrement(ref countOfObjectsCurrentlyReplicating);
            }, maxParallelism, cancellationToken);

            // snapshot processed, we proceed to reading the incremental state
            return (snapshotBucket, snapshotEvent, countOfObjectsReplicated);
        }

        private HttpRequestMessage BuildHttpRequest(HttpMethod httpMethod, string uri)
        {
            string? token = _serviceCredentials.GetToken();
            HttpRequestMessage request = new HttpRequestMessage(httpMethod, uri);
            if (!string.IsNullOrEmpty(token))
                request.Headers.Add("Authorization", "Bearer " + token);
            return request;
        }

        private async Task<int> ReplicateIncrementally(NamespaceId ns, string? lastBucket, Guid? lastEvent, CancellationToken replicationToken)
        {
            int countOfReplicationsDone = 0;
            _logger.Information("{Name} Looking for new transaction. Previous state: {@State}", _name, _refsState);

            SortedSet<long> replicationTasks = new();

            // if MaxParallelReplications is set to not limit we use the default behavior of ParallelForEachAsync which is to limit based on CPUs 
            int maxParallelism = _replicatorSettings.MaxParallelReplications != -1 ? _replicatorSettings.MaxParallelReplications : 0;

            await GetRefEvents(ns, lastBucket, lastEvent, replicationToken).ParallelForEachAsync(async (ReplicationLogEvent @event) =>
            {
                _logger.Information("{Name} New transaction to replicate found. New op: {@Op} . Count of running replications: {CurrentReplications}", _name, @event, replicationTasks.Count);
                
                Info.CountOfRunningReplications = replicationTasks.Count;
                LogReplicationHeartbeat(replicationTasks.Count);
                long currentOffset = Interlocked.Increment(ref countOfReplicationsDone);

                try
                {
                    string eventBucket = @event.TimeBucket;
                    Guid eventId = @event.EventId;

                    lock (replicationTasks)
                    {
                        replicationTasks.Add(currentOffset);
                    }

                    await ReplicateOp(@event.Namespace, @event.Blob, replicationToken);
                    // TODO: Avoid adding to the replication log as we will get into infinite recursion if we do with the remote site
                    // we could add to the replication log only if the op was missing, but that could cause issue for a 3rd site replicating from us
                    // and the 3rd site is the whole reason we even add this to replication log in the first place
                    // await AddToReplicationLog(@event.Namespace, @event.Bucket, @event.Key, @event.Blob);

                    try
                    {
                        bool wasOldest;
                        lock (replicationTasks)
                        {
                            wasOldest = currentOffset <= replicationTasks.First();
                        }

                        // only update the state when we have replicated everything up that point
                        if (wasOldest)
                        {
                            // we have replicated everything up to a point and can persist this in the state
                            _refsState.LastBucket = eventBucket;
                            _refsState.LastEvent = eventId;
                            SaveState(_stateFile, _refsState);

                            _logger.Information("{Name} replicated all events up to {Time} . Bucket: {EventBucket} Id: {EventId}", _name, @event.Timestamp, eventBucket, eventId);
                        }

                        Info.LastRun = DateTime.Now;
                        LogReplicationHeartbeat(replicationTasks.Count);
                    }
                    finally
                    {
                        lock (replicationTasks)
                        {
                            replicationTasks.Remove(currentOffset);
                        }
                    }
                }
                catch (BlobNotFoundException)
                {
                    _logger.Warning("{Name} Failed to replicate {@Op} in {Namespace} because blob was not present in remote store. Skipping.", _name, @event, Info.NamespaceToReplicate);
                }
            } , maxParallelism, breakLoopOnException: true, cancellationToken: replicationToken);

            return countOfReplicationsDone;
        }

        private async Task AddToReplicationLog(NamespaceId ns, BucketId bucket, KeyId key, BlobIdentifier blob)
        {
            await _replicationLog.InsertAddEvent(ns, bucket, key, blob);
        }

        private async Task ReplicateOp(NamespaceId ns, BlobIdentifier blob, CancellationToken cancellationToken)
        {
            // We could potentially do this, but that could be dangerous if missing child references
            // check if this blob exists locally before replicating, if it does we assume we have all of its references already
            //if (await _blobStore.Exists(ns, blob))
            //    return currentOffset;

            HttpRequestMessage referencesRequest = BuildHttpRequest(HttpMethod.Get, $"api/v1/objects/{ns}/{blob}/references");
            HttpResponseMessage referencesResponse = await _httpClient.SendAsync(referencesRequest, cancellationToken);
            string body = await referencesResponse.Content.ReadAsStringAsync(cancellationToken);
            referencesResponse.EnsureSuccessStatusCode();

            ResolvedReferencesResult? refs = JsonConvert.DeserializeObject<ResolvedReferencesResult>(body);
            if (refs == null)
                throw new Exception($"Unable to resolve references for object {blob} in namespace {ns}");

            BlobIdentifier[] potentialBlobs = new BlobIdentifier[refs.References.Length + 1];
            Array.Copy(refs.References, potentialBlobs, refs.References.Length);
            potentialBlobs[^1] = blob;

            BlobIdentifier[] missingBlobs = await _blobStore.FilterOutKnownBlobs(ns, potentialBlobs);
            Task[] blobReplicationTasks = new Task[missingBlobs.Length];
            for (int i = 0; i < missingBlobs.Length; i++)
            {
                BlobIdentifier blobToReplicate = missingBlobs[i];
                blobReplicationTasks[i] = Task.Run(async () =>
                {
                    
                    HttpRequestMessage blobRequest = BuildHttpRequest(HttpMethod.Get, $"api/v1/blobs/{ns}/{blobToReplicate}");
                    HttpResponseMessage blobResponse = await _httpClient.SendAsync(blobRequest, cancellationToken);
                    byte[] blobContents = await blobResponse.Content.ReadAsByteArrayAsync(cancellationToken);

                    BlobIdentifier calculatedBlob = BlobIdentifier.FromBlob(blobContents);
                    if (!blobToReplicate.Equals(calculatedBlob))
                    {
                        _logger.Warning("Mismatching blob when replicating {Blob}. Determined Hash was {Hash} size was {Size}", blobToReplicate, calculatedBlob, blobContents.LongLength);
                        // TODO: attempt to replicate again
                        return;
                    }

                    await _blobStore.PutObject(ns, blobContents, calculatedBlob);
                });
            }

            await Task.WhenAll(blobReplicationTasks);
        }

        private async IAsyncEnumerable<ReplicationLogEvent> GetRefEvents(NamespaceId ns, string? lastBucket, Guid? lastEvent, [EnumeratorCancellation] CancellationToken cancellationToken)
        {
            bool hasRunOnce = false;
            ReplicationLogEvents logEvents;
            do
            {
                if (cancellationToken.IsCancellationRequested)
                    yield break;

                if (hasRunOnce && (lastBucket == null || lastEvent == null))
                {
                    throw new Exception($"Failed to find state to resume from after first page of ref events, lastBucket: {lastBucket} lastEvent: {lastEvent}");
                }
                StringBuilder url = new StringBuilder($"api/v1/replication-log/incremental/{ns}");
                // its okay for last bucket and last event to be null incase we have never run before, but after the first iteration we need them to keep track of where we were
                if (lastBucket != null)
                    url.Append($"?lastBucket={lastBucket}");
                if (lastEvent != null)
                    url.Append($"&lastEvent={lastEvent}");

                hasRunOnce = true;
                HttpRequestMessage request = BuildHttpRequest(HttpMethod.Get, url.ToString());
                HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken);
                string body = await response.Content.ReadAsStringAsync(cancellationToken);
                if (response.StatusCode == HttpStatusCode.BadRequest)
                {
                    ProblemDetails? problemDetails = JsonConvert.DeserializeObject<ProblemDetails>(body);
                    if (problemDetails == null)
                        throw new Exception("Unknown bad request body when reading incremental replication log. Body: {body}");

                    if (problemDetails.Type == ProblemTypes.UseSnapshot)
                    {
                        BlobIdentifier snapshotBlob = new BlobIdentifier(problemDetails.Extensions["SnapshotId"]!.ToString()!);
                        throw new UseSnapshotException(snapshotBlob);
                    }
                }

                response.EnsureSuccessStatusCode();
                ReplicationLogEvents? e = JsonConvert.DeserializeObject<ReplicationLogEvents>(body);
                if (e == null)
                    throw new Exception($"Unknown error when deserializing replication log events {ns} {lastBucket} {lastEvent}");

                logEvents = e;
                foreach (ReplicationLogEvent logEvent in logEvents.Events)
                {
                    yield return logEvent;

                    lastBucket = logEvent.TimeBucket;
                    lastEvent = logEvent.EventId;
                }

            } while (logEvents.Events.Any());
        }

        private void LogReplicationHeartbeat(int countOfCurrentReplications)
        {
            
            // log message used to generate metric for how many replications are currently running
            _logger.Information("{Name} replication has run . Count of running replications: {CurrentReplications}", _name, countOfCurrentReplications);

            // log message used to verify replicators are actually running
            _logger.Information("{Name} starting replication. Last transaction was {TransactionId} {Generation}", _name, State.ReplicatorOffset.GetValueOrDefault(0L), State.ReplicatingGeneration.GetValueOrDefault(Guid.Empty) );
        }

        public void SetReplicationOffset(long state)
        {
            throw new NotImplementedException();
        }

        public async Task StopReplicating()
        {
            _replicationTokenSource.Cancel(true);
            await _replicationFinishedEvent.WaitAsync();
        }

        public IReplicator.ReplicatorState State
        {
            get { return _refsState; }
        }

        public IReplicator.ReplicatorInfo Info { get; private set; }

        public Task DeleteState()
        {
            _refsState = new RefsState();
            SaveState(_stateFile, _refsState);

            return Task.CompletedTask;
        }

        public void SetRefState(string? lastBucket, Guid? lastEvent)
        {
            _refsState.LastBucket = lastBucket;
            _refsState.LastEvent = lastEvent;
            SaveState(_stateFile, _refsState);
        }
    }

    internal class UseSnapshotException : Exception
    {
        public BlobIdentifier SnapshotBlob { get; }

        public UseSnapshotException(BlobIdentifier snapshotBlob)
        {
            SnapshotBlob = snapshotBlob;
        }

    }

    internal class NoSnapshotAvailableException : Exception
    {
        public NoSnapshotAvailableException(string message) : base(message)
        {
        }
    }

    public class RefsState : IReplicator.ReplicatorState
    {
        public RefsState()
        {
            ReplicatingGeneration = null;
            ReplicatorOffset = 0;

            LastEvent = null;
            LastBucket = null;
        }

        public Guid? LastEvent { get; set; }
        public string? LastBucket { get; set; }
    }
}
