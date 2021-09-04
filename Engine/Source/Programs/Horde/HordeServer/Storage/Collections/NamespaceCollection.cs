// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Caching.Memory;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Claims;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Storage
{
	using NamespaceId = StringId<INamespace>;

	class NamespaceCollection : INamespaceCollection, IDisposable
	{
		class Namespace : INamespace
		{
			public NamespaceId Id { get; set; }
			public Acl? Acl { get; set; }
			public bool Deleted { get; set; }
		}

		IMongoCollection<Namespace> Namespaces;
		LazyCache<NamespaceId, INamespace?> NamespaceCache;
		AclService AclService;

		public NamespaceCollection(DatabaseService DatabaseService, AclService AclService)
		{
			this.AclService = AclService;
			this.Namespaces = DatabaseService.GetCollection<Namespace>("Storage.Namespaces");
			this.NamespaceCache = new LazyCache<NamespaceId, INamespace?>(GetAsync, new LazyCacheOptions { });
		}

		public void Dispose()
		{
			NamespaceCache.Dispose();
		}

		public async Task AddOrUpdateAsync(NamespaceId NamespaceId, NamespaceConfig Config)
		{
			Namespace Namespace = new Namespace();
			Namespace.Id = NamespaceId;
			Namespace.Acl = Acl.Merge(null, Config.Acl);
			await Namespaces.ReplaceOneAsync(x => x.Id == Namespace.Id, Namespace, new ReplaceOptions { IsUpsert = true });
		}

		public async Task<List<INamespace>> FindAsync()
		{
			return await Namespaces.Find(x => !x.Deleted).ToListAsync<Namespace, INamespace>();
		}

		public async Task<INamespace?> GetAsync(NamespaceId NamespaceId)
		{
			return await Namespaces.Find(x => x.Id == NamespaceId).FirstOrDefaultAsync();
		}

		public async Task RemoveAsync(NamespaceId NamespaceId)
		{
			await Namespaces.UpdateOneAsync(x => x.Id == NamespaceId, Builders<Namespace>.Update.Set(x => x.Deleted, true));
		}

		public async Task<bool> AuthorizeAsync(NamespaceId NamespaceId, ClaimsPrincipal User, AclAction Action)
		{
			INamespace? Namespace = await NamespaceCache.GetAsync(NamespaceId);
			if (Namespace == null)
			{
				return false;
			}

			bool? Result = Namespace.Acl?.Authorize(Action, User);
			if (Result != null)
			{
				return Result.Value;
			}

			return await AclService.AuthorizeAsync(Action, User);
		}
	}
}
