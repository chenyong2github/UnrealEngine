// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Threading;
using System.Threading.Tasks;
using async_enumerable_dotnet;
using Horde.Storage.Implementation;
using Horde.Storage.Implementation.TransactionLog;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;
using Newtonsoft.Json;

namespace Horde.Storage.Controllers
{
    [ApiController]
    [Route("api/v1/replication-log")]
    public class ReplicationLogController : ControllerBase
    {
        private readonly IServiceProvider _provider;
        private readonly IAuthorizationService _authorizationService;
        private readonly IReplicationLog _replicationLog;
        private readonly IOptionsMonitor<SnapshotSettings> _snapshotSettings;

        public ReplicationLogController(IServiceProvider provider, IAuthorizationService authorizationService, IReplicationLog replicationLog, IOptionsMonitor<SnapshotSettings> snapshotSettings)
        {
            _provider = provider;
            _authorizationService = authorizationService;
            _replicationLog = replicationLog;
            _snapshotSettings = snapshotSettings;
        }


        [HttpGet("snapshots/{ns}")]
        [ProducesDefaultResponseType]
        [ProducesResponseType(type: typeof(ProblemDetails), 400)]
        [Authorize("replication-log.read")]
        public async Task<IActionResult> GetSnapshots(
            [Required] NamespaceId ns
        )
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            return Ok(new ReplicationLogSnapshots(await _replicationLog.GetSnapshots(ns).ToListAsync()));
        }

        
        [HttpPost("snapshots/{ns}/create")]
        [ProducesDefaultResponseType]
        [ProducesResponseType(type: typeof(ProblemDetails), 400)]
        [Authorize("Admin")]
        public async Task<IActionResult> CreateSnapshot(
            [Required] NamespaceId ns
        )
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            ReplicationLogSnapshotBuilder builder = ActivatorUtilities.CreateInstance<ReplicationLogSnapshotBuilder>(_provider);
            BlobIdentifier snapshotBlob = await builder.BuildSnapshot(ns, _snapshotSettings.CurrentValue.SnapshotStorageNamespace, CancellationToken.None);
            return Ok(new { SnapshotBlobId = snapshotBlob });
        }

        [HttpGet("incremental/{ns}")]
        [ProducesDefaultResponseType]
        [ProducesResponseType(type: typeof(ProblemDetails), 400)]
        [Authorize("replication-log.read")]
        public async Task<IActionResult> GetIncrementalEvents(
            [Required] NamespaceId ns,
            [FromQuery] string? lastBucket,
            [FromQuery] Guid? lastEvent,
            [FromQuery] int count = 100
        )
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            if ((lastBucket == null && lastEvent.HasValue) || (lastBucket != null && !lastEvent.HasValue))
            {
                return BadRequest(new ProblemDetails
                {
                    Title = $"Both bucket and event has to be specified, or omit both.",
                });
            }

            try
            {
                IAsyncEnumerable<ReplicationLogEvent> events = _replicationLog.Get(ns, lastBucket, lastEvent);

                List<ReplicationLogEvent> l = await events.Take(count).ToListAsync();
                return Ok(new ReplicationLogEvents(l));
            }
            catch (IncrementalLogNotAvailableException)
            {
                // failed to resume from the incremental log, check for a snapshot instead
                SnapshotInfo? snapshot = await _replicationLog.GetLatestSnapshot(ns);
                if (snapshot != null)
                {
                    // no log file is available
                    return BadRequest(new ProblemDetails
                    {
                        Title = $"Log file is not available, use snapshot {snapshot.SnapshotBlob} instead",
                        Type = ProblemTypes.UseSnapshot,
                        Extensions = { { "SnapshotId", snapshot.SnapshotBlob } }
                    });
                }

                // if no snapshot is available we just give up, they can always reset the replication to the default behavior by not sending in lastBucket and lastEvent

                return BadRequest(new ProblemDetails
                {
                    Title = $"No snapshot or bucket found for namespace \"{ns}\"",
                });
            }
            catch (NamespaceNotFoundException)
            {
                return NotFound(new ProblemDetails
                {
                    Title = $"Namespace {ns} was not found",
                });
            }

        }
    }

    public class ReplicationLogSnapshots
    {
        public ReplicationLogSnapshots()
        {
            Snapshots = new List<SnapshotInfo>();
        }

        [JsonConstructor]
        public ReplicationLogSnapshots(List<SnapshotInfo> snapshots)
        {
            Snapshots = snapshots;
        }

        public List<SnapshotInfo> Snapshots { get; set; }
    }

    public class ReplicationLogEvents
    {
        public ReplicationLogEvents()
        {
            Events = new List<ReplicationLogEvent>();
        }

        [JsonConstructor]
        public ReplicationLogEvents(List<ReplicationLogEvent> events)
        {
            Events = events;
        }

        public List<ReplicationLogEvent> Events { get; set; }
    }
}
