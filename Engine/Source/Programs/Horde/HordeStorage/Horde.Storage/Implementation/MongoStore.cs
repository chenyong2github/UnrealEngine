// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Security.Authentication;
using System.Threading.Tasks;
using Microsoft.Extensions.Options;
using MongoDB.Bson.Serialization;
using MongoDB.Driver;

namespace Horde.Storage.Implementation;

public class MongoStore
{
    protected readonly MongoClient _client;

    public MongoStore(IOptionsMonitor<MongoSettings> settings)
    {
        MongoClientSettings mongoClientSettings = MongoClientSettings.FromUrl(
            new MongoUrl(settings.CurrentValue.ConnectionString)
        );
        if (settings.CurrentValue.RequireTls12)
        {
            mongoClientSettings.SslSettings = new SslSettings {EnabledSslProtocols = SslProtocols.Tls12};
        }

        _client = new MongoClient(mongoClientSettings);
    }

    protected async Task CreateCollectionIfNotExists<T>()
    {
        string collectionName = GetCollectionName<T>();

        try
        {
            await _client.GetDatabase(GetDatabaseName()).CreateCollectionAsync(collectionName);
        }
        catch (MongoCommandException e)
        {
            if (e.CodeName != "NamespaceExists")
                throw ;
        }
    }

    private string GetCollectionName<T>()
    {
        object[] attr = typeof(T).GetCustomAttributes(typeof(MongoCollectionNameAttribute), true);
        foreach (MongoCollectionNameAttribute o in attr)
        {
            return o.CollectionName;
        }

        throw new ArgumentException($"No MongoCollectionNameAttribute found on type {nameof(T)}");
    }

    protected IMongoIndexManager<T> AddIndexFor<T>()
    {
        return GetCollection<T>().Indexes;
    }

    protected IMongoCollection<T> GetCollection<T>()
    {
        string collectionName = GetCollectionName<T>();
        string dbName = GetDatabaseName();
        return _client.GetDatabase(dbName).GetCollection<T>(collectionName);
    }

    private string GetDatabaseName()
    {
        // TODO: Database per namespace seems a bit much?
        //string cleanedNs = ns.ToString().Replace(".", "_");
        //string dbName = $"HordeStorage_{cleanedNs}";
        string dbName = $"HordeStorage";
        return dbName;
    }
}

public class MongoCollectionNameAttribute : Attribute
{
    public string CollectionName { get; }

    public MongoCollectionNameAttribute(string collectionName)
    {
        CollectionName = collectionName;
    }
}
