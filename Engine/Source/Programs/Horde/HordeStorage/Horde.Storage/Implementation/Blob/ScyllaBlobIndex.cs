// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Cassandra;
using Cassandra.Mapping;
using Datadog.Trace;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;

namespace Horde.Storage.Implementation.Blob;

public class ScyllaBlobIndex : IBlobIndex
{
    private readonly IOptionsMonitor<JupiterSettings> _jupiterSettings;
    private readonly ISession _session;
    private readonly Mapper _mapper;

    public ScyllaBlobIndex(IScyllaSessionManager scyllaSessionManager, IOptionsMonitor<JupiterSettings> jupiterSettings)
    {
        _jupiterSettings = jupiterSettings;
        _session = scyllaSessionManager.GetSessionForReplicatedKeyspace();
        _mapper = new Mapper(_session);

        _session.Execute(new SimpleStatement(@"CREATE TABLE IF NOT EXISTS blob_index (
            namespace text,
            blob_id frozen<blob_identifier>,
            regions set<text>,
            PRIMARY KEY ((namespace, blob_id))
        );"
        ));
    }

    public async Task AddBlobToIndex(NamespaceId ns, BlobIdentifier id)
    {
        using Scope _ = Tracer.Instance.StartActive("scylla.insert_blob_index");

        await _mapper.UpdateAsync<ScyllaBlobIndexTable>("SET regions = regions + ? WHERE namespace = ? AND blob_id = ?", new string[] {_jupiterSettings.CurrentValue.CurrentSite}, ns.ToString(), new ScyllaBlobIdentifier(id));
    }

    public async Task<IBlobIndex.BlobInfo?> GetBlobInfo(NamespaceId ns, BlobIdentifier id)
    {
        using Scope _ = Tracer.Instance.StartActive("scylla.fetch_blob_index");

        ScyllaBlobIndexTable? blobIndex = await _mapper.FirstOrDefaultAsync<ScyllaBlobIndexTable>("WHERE namespace = ? AND blob_id = ?", ns.ToString(), new ScyllaBlobIdentifier(id));

        if (blobIndex == null)
            return null;

        return new IBlobIndex.BlobInfo()
        {
            Regions = blobIndex.Regions
        };
    }

    public async Task<bool> RemoveBlobFromIndex(NamespaceId ns, BlobIdentifier id)
    {
        // TODO: Should this only remove the current region, and the actual row isnt removed until all regions have been removed? Seems overly complicated
        using Scope _ = Tracer.Instance.StartActive("scylla.remove_from_blob_index");
        await _mapper.DeleteAsync<ScyllaBlobIndexTable>("WHERE namespace = ? AND blob_id = ?", ns.ToString(), new ScyllaBlobIdentifier(id));

        return true;
    }

    public async Task<bool> BlobExistsInRegion(NamespaceId ns, BlobIdentifier blobIdentifier)
    {
        IBlobIndex.BlobInfo? blobInfo = await GetBlobInfo(ns, blobIdentifier);
        return blobInfo?.Regions.Contains(_jupiterSettings.CurrentValue.CurrentSite) ?? false;
    }
}


[Cassandra.Mapping.Attributes.Table("blob_index")]
class ScyllaBlobIndexTable
{
    public ScyllaBlobIndexTable()
    {
        Namespace = null!;
        BlobId = null!;
        Regions = null!;
    }

    public ScyllaBlobIndexTable(string @namespace, BlobIdentifier blobId, HashSet<string> regions)
    {
        Namespace = @namespace;
        BlobId =  new ScyllaBlobIdentifier(blobId);
        Regions = regions;
    }

    [Cassandra.Mapping.Attributes.PartitionKey]
    public string Namespace { get; set; }

    [Cassandra.Mapping.Attributes.PartitionKey]
    [Cassandra.Mapping.Attributes.Column("blob_id")]
    public ScyllaBlobIdentifier BlobId { get;set; }

    [Cassandra.Mapping.Attributes.Column("regions")]
    public HashSet<string> Regions { get; set; }
}
