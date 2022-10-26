// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Claims;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Horde.Build.Acls;
using Horde.Build.Server;
using Horde.Build.Storage.Backends;
using Horde.Build.Utilities;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Storage
{
	using BackendId = StringId<IStorageBackend>;

	/// <summary>
	/// Exception thrown by the <see cref="StorageService"/>
	/// </summary>
	public sealed class StorageException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public StorageException(string message, Exception? inner = null)
			: base(message, inner)
		{
		}
	}

	/// <summary>
	/// Interface for storage clients which includes a backend implementation. Some functionality is exposed through the backend which is not part of the regular storage API (eg. enumerating).
	/// </summary>
	public interface IStorageClientImpl : IStorageClient
	{
		/// <summary>
		/// Configuration for this namespace
		/// </summary>
		NamespaceConfig Config { get; }

		/// <summary>
		/// The storage backend
		/// </summary>
		IStorageBackend Backend { get; }

		/// <summary>
		/// The ref collection
		/// </summary>
		IRefCollection Refs { get; }

		/// <summary>
		/// Whether the backend supports redirects
		/// </summary>
		bool SupportsRedirects { get; }

		/// <summary>
		/// Gets a redirect for a read request
		/// </summary>
		/// <param name="locator">Locator for the blob</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Path to upload the data to</returns>
		ValueTask<Uri?> GetReadRedirectAsync(BlobLocator locator, CancellationToken cancellationToken = default);

		/// <summary>
		/// Gets a redirect for a write request
		/// </summary>
		/// <param name="prefix">Prefix for the new blob locator</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Locator and path to upload the data to</returns>
		ValueTask<(BlobLocator, Uri)?> GetWriteRedirectAsync(Utf8String prefix = default, CancellationToken cancellationToken = default);
	}

	/// <summary>
	/// Functionality related to the storage service
	/// </summary>
	public sealed class StorageService : IDisposable
	{
		sealed class StorageClient : StorageClientBase, IStorageClientImpl, IDisposable
		{
			public NamespaceConfig Config { get; }

			public IStorageBackend Backend { get; }

			NamespaceId NamespaceId => Config.Id;

			public IRefCollection Refs { get; }

			public bool SupportsRedirects { get; }

			public StorageClient(NamespaceConfig config, IStorageBackend backend, IRefCollection refs, IMemoryCache cache, ILogger logger)
				: base(cache, logger)
			{
				Config = config;
				Backend = backend;
				Refs = refs;
				SupportsRedirects = backend is IStorageBackendWithRedirects;
			}

			public void Dispose()
			{
				if (Backend is IDisposable disposable)
				{
					disposable.Dispose();
				}
			}

			static string GetBlobPath(BlobId blobId) => $"{blobId}.blob";

			#region Blobs

			/// <inheritdoc/>
			public override async Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default)
			{
				string path = GetBlobPath(locator.BlobId);

				Stream? stream = await Backend.TryReadAsync(path, cancellationToken);
				if (stream == null)
				{
					throw new StorageException($"Unable to read data from {path}");
				}

				return stream;
			}

			/// <inheritdoc/>
			public ValueTask<Uri?> GetReadRedirectAsync(BlobLocator locator, CancellationToken cancellationToken = default)
			{
				IStorageBackendWithRedirects? redirectBackend = Backend as IStorageBackendWithRedirects;
				if (redirectBackend == null)
				{
					return new ValueTask<Uri?>((Uri?)null);
				}
				return redirectBackend.GetReadRedirectAsync(GetBlobPath(locator.BlobId), cancellationToken);
			}

			/// <inheritdoc/>
			public override async Task<Stream> ReadBlobRangeAsync(BlobLocator locator, int offset, int length, CancellationToken cancellationToken = default)
			{
				string path = GetBlobPath(locator.BlobId);

				Stream? stream = await Backend.TryReadAsync(path, offset, length, cancellationToken);
				if (stream == null)
				{
					throw new StorageException($"Unable to read data from {path}");
				}

				return stream;
			}

			/// <inheritdoc/>
			public override async Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default)
			{
				BlobId id = BlobId.CreateNew(prefix);

				string path = GetBlobPath(id);
				await Backend.WriteAsync(path, stream, cancellationToken);

				return new BlobLocator(HostId.Empty, id);
			}

			/// <inheritdoc/>
			public async ValueTask<(BlobLocator, Uri)?> GetWriteRedirectAsync(Utf8String prefix = default, CancellationToken cancellationToken = default)
			{
				IStorageBackendWithRedirects? redirectBackend = Backend as IStorageBackendWithRedirects;
				if (redirectBackend == null)
				{
					return null;
				}

				BlobId id = BlobId.CreateNew(prefix);
				string path = GetBlobPath(id);

				Uri? url = await redirectBackend.GetWriteRedirectAsync(path, cancellationToken);
				if (url == null)
				{
					return null;
				}

				return (new BlobLocator(HostId.Empty, id), url);
			}

			#endregion

			#region Refs

			/// <inheritdoc/>
			public override Task<NodeLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) => Refs.TryReadRefTargetAsync(NamespaceId, name, cacheTime, cancellationToken);

			/// <inheritdoc/>
			public override Task WriteRefTargetAsync(RefName name, NodeLocator target, RefOptions? options = null, CancellationToken cancellationToken = default) => Refs.WriteRefTargetAsync(NamespaceId, name, target, options, cancellationToken);

			/// <inheritdoc/>
			public override Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default) => Refs.DeleteRefAsync(NamespaceId, name, cancellationToken);

			#endregion
		}

		class NamespaceInfo : IDisposable
		{
			public NamespaceConfig Config { get; }
			public IStorageClientImpl Client { get; }
			public Acl? Acl { get; }

			public NamespaceInfo(NamespaceConfig config, IStorageClientImpl client)
			{
				Config = config;
				Client = client;
				Acl = Acl.Merge(null, config.Acl);
			}

			public void Dispose() => (Client as IDisposable)?.Dispose();
		}

		class State : IDisposable
		{
			public StorageConfig Config { get; }
			public Dictionary<NamespaceId, NamespaceInfo> Namespaces { get; } = new Dictionary<NamespaceId, NamespaceInfo>();

			public State(StorageConfig config)
			{
				Config = config;
			}

			public void Dispose()
			{
				foreach (NamespaceInfo namespaceInfo in Namespaces.Values)
				{
					namespaceInfo.Dispose();
				}
			}
		}

		readonly GlobalsService _globalsService;
		readonly AclService _aclService;
		readonly IRefCollection _refCollection;
		readonly IMemoryCache _cache;
		readonly IServiceProvider _serviceProvider;
		readonly ILogger _logger;

		State? _lastState;
		string? _lastConfigRevision;

		readonly AsyncCachedValue<State> _cachedState;

		static readonly BackendConfig s_defaultBackendConfig = new BackendConfig();

		/// <summary>
		/// Constructor
		/// </summary>
		public StorageService(GlobalsService globalsService, AclService aclService, IRefCollection refCollection, IMemoryCache cache, IServiceProvider serviceProvider, ILogger<StorageService> logger)
		{
			_globalsService = globalsService;
			_aclService = aclService;
			_refCollection = refCollection;
			_cache = cache;
			_serviceProvider = serviceProvider;
			_cachedState = new AsyncCachedValue<State>(() => GetNextState(), TimeSpan.FromMinutes(1.0));
			_logger = logger;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			if (_lastState != null)
			{
				_lastState.Dispose();
				_lastState = null;
			}
		}

		async ValueTask<NamespaceInfo> GetNamespaceInfoAsync(NamespaceId namespaceId, CancellationToken cancellationToken)
		{
			State state = await _cachedState.GetAsync(cancellationToken);
			if (!state.Namespaces.TryGetValue(namespaceId, out NamespaceInfo? namespaceInfo))
			{
				throw new StorageException($"No namespace '{namespaceId}' is configured.");
			}
			return namespaceInfo;
		}

		/// <summary>
		/// Finds the configuration for all current namespaces
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>List of namespace configurations</returns>
		public async Task<List<NamespaceConfig>> GetNamespacesAsync(CancellationToken cancellationToken)
		{
			State state = await _cachedState.GetAsync(cancellationToken);
			return state.Namespaces.Select(x => x.Value.Config).ToList();
		}

		/// <summary>
		/// Gets a storage client for the given namespace
		/// </summary>
		/// <param name="namespaceId">Namespace identifier</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async ValueTask<IStorageClientImpl> GetClientAsync(NamespaceId namespaceId, CancellationToken cancellationToken)
		{
			NamespaceInfo namespaceInfo = await GetNamespaceInfoAsync(namespaceId, cancellationToken);
			return namespaceInfo.Client;
		}

		/// <summary>
		/// Authorize operations for the given store
		/// </summary>
		/// <param name="namespaceId"></param>
		/// <param name="user"></param>
		/// <param name="action"></param>
		/// <param name="cache">Global permissions cache</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async Task<bool> AuthorizeAsync(NamespaceId namespaceId, ClaimsPrincipal user, AclAction action, GlobalPermissionsCache? cache, CancellationToken cancellationToken)
		{
			NamespaceInfo namespaceInfo = await GetNamespaceInfoAsync(namespaceId, cancellationToken);

			bool? result = namespaceInfo.Acl?.Authorize(action, user);
			if (result == null)
			{
				return await _aclService.AuthorizeAsync(action, user, cache);
			}
			else
			{
				return result.Value;
			}
		}

		async Task<State> GetNextState()
		{
			IGlobals globals = await _globalsService.GetAsync();
			if (_lastState == null || !String.Equals(_lastConfigRevision, globals.ConfigRevision, StringComparison.Ordinal))
			{
				StorageConfig storageConfig = globals.Config.Storage;

				// Create a lookup for backend config objects by their id
				Dictionary<BackendId, BackendConfig> backendIdToConfig = new Dictionary<BackendId, BackendConfig>();
				foreach (BackendConfig backendConfig in storageConfig.Backends)
				{
					backendIdToConfig.Add(backendConfig.Id, backendConfig);
				}

				// Create a lookup of namespace id to config, to ensure there are no duplicates
				Dictionary<NamespaceId, NamespaceConfig> namespaceIdToConfig = new Dictionary<NamespaceId, NamespaceConfig>();
				foreach (NamespaceConfig namespaceConfig in storageConfig.Namespaces)
				{
					namespaceIdToConfig.Add(namespaceConfig.Id, namespaceConfig);
				}

				// Configure the new clients
				State nextState = new State(storageConfig);
				try
				{
					Dictionary<BackendId, BackendConfig> mergedIdToConfig = new Dictionary<BackendId, BackendConfig>();
					foreach (NamespaceConfig namespaceConfig in storageConfig.Namespaces)
					{
						if (namespaceConfig.Backend.IsEmpty)
						{
							throw new StorageException($"No backend configured for namespace {namespaceConfig.Id}");
						}

						BackendConfig backendConfig = GetBackendConfig(namespaceConfig.Backend, backendIdToConfig, mergedIdToConfig);
						IStorageBackend backend = CreateStorageBackend(backendConfig);

#pragma warning disable CA2000 // Dispose objects before losing scope
						StorageClient client = new StorageClient(namespaceConfig, backend, _refCollection, _cache, _logger);
						nextState.Namespaces.Add(namespaceConfig.Id, new NamespaceInfo(namespaceConfig, client));
#pragma warning restore CA2000 // Dispose objects before losing scope
					}
				}
				catch
				{
					nextState.Dispose();
					throw;
				}

				_lastState?.Dispose();
				_lastState = nextState;

				_lastConfigRevision = globals.ConfigRevision;
			}
			return _lastState;
		}

		/// <summary>
		/// Creates a storage backend with the given configuration
		/// </summary>
		/// <param name="config">Configuration for the backend</param>
		/// <returns>New storage backend instance</returns>
		IStorageBackend CreateStorageBackend(BackendConfig config)
		{
			switch (config.Type ?? StorageBackendType.FileSystem)
			{
				case StorageBackendType.FileSystem:
					return new FileSystemStorageBackend(config);
				case StorageBackendType.Aws:
					return new AwsStorageBackend(_serviceProvider.GetRequiredService<IConfiguration>(), config, _serviceProvider.GetRequiredService<ILogger<AwsStorageBackend>>());
				case StorageBackendType.Memory:
					return new MemoryStorageBackend();
				default:
					throw new NotImplementedException();
			}
		}

		/// <summary>
		/// Gets the configuration for a particular backend
		/// </summary>
		/// <param name="backendId">Identifier for the backend</param>
		/// <param name="baseIdToConfig">Lookup for all backend configuration data</param>
		/// <param name="mergedIdToConfig">Lookup for computed hierarchical config objects</param>
		/// <returns>Merged config for the given backend</returns>
		BackendConfig GetBackendConfig(BackendId backendId, Dictionary<BackendId, BackendConfig> baseIdToConfig, Dictionary<BackendId, BackendConfig> mergedIdToConfig)
		{
			BackendConfig? config;
			if (mergedIdToConfig.TryGetValue(backendId, out config))
			{
				if (config == null)
				{
					throw new StorageException($"Configuration for storage backend '{backendId}' is recursive.");
				}
			}
			else
			{
				mergedIdToConfig.Add(backendId, null!);

				if (!baseIdToConfig.TryGetValue(backendId, out config))
				{
					throw new StorageException($"Unable to find storage backend '{backendId}'"); 
				}

				if (config.Base != BackendId.Empty)
				{
					BackendConfig baseConfig = GetBackendConfig(config.Base, baseIdToConfig, mergedIdToConfig);
					MergeConfigs(baseConfig, config, s_defaultBackendConfig);
				}

				mergedIdToConfig[backendId] = config;
			}
			return config;
		}

		/// <summary>
		/// Allows one configuration object to override another
		/// </summary>
		static void MergeConfigs<T>(T sourceObject, T targetObject, T defaultObject)
		{
			PropertyInfo[] properties = typeof(T).GetProperties(BindingFlags.Public | BindingFlags.Instance);
			foreach (PropertyInfo property in properties)
			{
				object? targetValue = property.GetValue(targetObject);
				object? defaultValue = property.GetValue(defaultObject);

				if (ValueEquals(targetValue, defaultValue))
				{
					object? sourceValue = property.GetValue(sourceObject);
					property.SetValue(targetObject, sourceValue);
				}
			}
		}

		/// <summary>
		/// Tests two objects for value equality
		/// </summary>
		static bool ValueEquals(object? source, object? target)
		{
			if (ReferenceEquals(source, null) || ReferenceEquals(target, null))
			{
				return ReferenceEquals(source, target);
			}

			if (source is IEnumerable _)
			{
				IEnumerator sourceEnumerator = ((IEnumerable)source).GetEnumerator();
				IEnumerator targetEnumerator = ((IEnumerable)target).GetEnumerator();

				for (; ; )
				{
					if (!sourceEnumerator.MoveNext())
					{
						return !targetEnumerator.MoveNext();
					}
					if (!targetEnumerator.MoveNext() || !ValueEquals(sourceEnumerator.Current, targetEnumerator.Current))
					{
						return false;
					}
				}
			}

			return source.Equals(target);
		}
	}
}
