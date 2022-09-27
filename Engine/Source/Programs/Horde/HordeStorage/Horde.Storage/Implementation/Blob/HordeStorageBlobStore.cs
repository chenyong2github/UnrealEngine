// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Mime;
using System.Threading.Tasks;
using Dasync.Collections;
using EpicGames.Horde.Storage;
using Horde.Storage.Implementation.LeaderElection;
using Jupiter.Implementation;
using k8s;
using k8s.Models;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class HordeStorageBlobStore : IBlobStore
    {
        private readonly IHordeStorageServiceDiscovery _serviceDiscovery;
        private readonly IHttpClientFactory _httpClientFactory;
        private readonly IServiceCredentials _serviceCredentials;
        private readonly ILogger _logger = Log.ForContext<HordeStorageBlobStore>();
        private readonly ConcurrentDictionary<string, HttpClient> _httpClients = new ConcurrentDictionary<string, HttpClient>();

        public HordeStorageBlobStore(IHordeStorageServiceDiscovery serviceDiscovery, IHttpClientFactory httpClientFactory, IServiceCredentials serviceCredentials)
        {
            _serviceDiscovery = serviceDiscovery;
            _httpClientFactory = httpClientFactory;
            _serviceCredentials = serviceCredentials;
        }

        private async Task<HttpRequestMessage> BuildHttpRequest(HttpMethod method, Uri uri)
        {
            string? token = await _serviceCredentials.GetTokenAsync();
            HttpRequestMessage request = new HttpRequestMessage(method, uri);
            if (!string.IsNullOrEmpty(token))
            {
                request.Headers.Add("Authorization", $"{_serviceCredentials.GetAuthenticationScheme()} {token}");
            }

            return request;
        }

        public async Task<BlobContents> GetObject(NamespaceId ns, BlobIdentifier blob, LastAccessTrackingFlags flags = LastAccessTrackingFlags.DoTracking)
        {
            // TODO: This could be done in parallel
            await foreach (string instance in _serviceDiscovery.FindOtherHordeStorageInstances())
            {
                string filesystemLayerName = nameof(FileSystemStore);
                using HttpRequestMessage getObjectRequest = await BuildHttpRequest(HttpMethod.Get, new Uri($"api/v1/blobs/{ns}/{blob}?storageLayers={filesystemLayerName}", UriKind.Relative));
                getObjectRequest.Headers.Add("Accept", MediaTypeNames.Application.Octet);
                HttpResponseMessage response = await GetHttpClient(instance).SendAsync(getObjectRequest);
                if (response.StatusCode == HttpStatusCode.NotFound)
                {
                    continue;
                }

                response.EnsureSuccessStatusCode();

                long? contentLength = response.Content.Headers.ContentLength;
                if (contentLength == null)
                {
                    _logger.Warning("Content length missing in response from horde storage blob store. This is not supported, ignoring response");
                    continue;
                }
                return new BlobContents(await response.Content.ReadAsStreamAsync(), contentLength.Value);
            }
            
            throw new BlobNotFoundException(ns, blob);
        }

        public async Task<bool> Exists(NamespaceId ns, BlobIdentifier blob, bool forceCheck = false)
        {
            // TODO: This could be done in parallel
            await foreach (string instance in _serviceDiscovery.FindOtherHordeStorageInstances())
            {
                string filesystemLayerName = nameof(FileSystemStore);
                using HttpRequestMessage headObjectRequest = await BuildHttpRequest(HttpMethod.Head, new Uri($"api/v1/blobs/{ns}/{blob}?storageLayers={filesystemLayerName}", UriKind.Relative));
                HttpResponseMessage response = await GetHttpClient(instance).SendAsync(headObjectRequest);
                if (response.StatusCode == HttpStatusCode.NotFound)
                {
                    continue;
                }

                response.EnsureSuccessStatusCode();

                return true;
            }

            return false;
        }

        private HttpClient GetHttpClient(string instance)
        {
            return _httpClients.GetOrAdd(instance, s =>
            {
                HttpClient httpClient = _httpClientFactory.CreateClient(instance);
                httpClient.BaseAddress = new Uri($"http://{instance}");
                return httpClient;
            });
        }

        public Task<BlobIdentifier> PutObject(NamespaceId ns, byte[] blob, BlobIdentifier identifier)
        {
            // not applicable
            return Task.FromResult(identifier);
        }

        public Task<BlobIdentifier> PutObject(NamespaceId ns, ReadOnlyMemory<byte> blob, BlobIdentifier identifier)
        {
            // not applicable
            return Task.FromResult(identifier);
        }

        public Task<BlobIdentifier> PutObject(NamespaceId ns, Stream content, BlobIdentifier identifier)
        {
            // not applicable
            return Task.FromResult(identifier);
        }

        public Task DeleteObject(NamespaceId ns, BlobIdentifier blob)
        {
            // not applicable
            return Task.CompletedTask;
        }

        public Task DeleteNamespace(NamespaceId ns)
        {
            // not applicable
            return Task.CompletedTask;
        }

        public IAsyncEnumerable<(BlobIdentifier, DateTime)> ListObjects(NamespaceId ns)
        {
            // not applicable
            return AsyncEnumerable<(BlobIdentifier, DateTime)>.Empty;
        }
    }

    public interface IHordeStorageServiceDiscovery
    {
        public IAsyncEnumerable<string> FindOtherHordeStorageInstances();
    }

    public class StaticHordeStorageServiceDiscoverySettings
    {
        [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2227:Collection properties should be read only", Justification = "Used by serialization")]
        public List<string> Peers { get; set; } = new List<string>();
    }

    public sealed class StaticHordeStorageServiceDiscovery : IHordeStorageServiceDiscovery
    {
        private readonly IOptionsMonitor<StaticHordeStorageServiceDiscoverySettings> _settings;

        public StaticHordeStorageServiceDiscovery(IOptionsMonitor<StaticHordeStorageServiceDiscoverySettings> settings)
        {
            _settings = settings;
        }

        public async IAsyncEnumerable<string> FindOtherHordeStorageInstances()
        {
            await Task.CompletedTask;
            foreach (string peer in _settings.CurrentValue.Peers)
            {
                yield return peer;
            }
        }
    }

    public sealed class KubernetesHordeStorageServiceDiscovery: IHordeStorageServiceDiscovery, IDisposable
    {
        private readonly IOptionsMonitor<KubernetesLeaderElectionSettings> _leaderSettings;
        private readonly Kubernetes _client;

        public KubernetesHordeStorageServiceDiscovery(IOptionsMonitor<KubernetesLeaderElectionSettings> leaderSettings)
        {
            _leaderSettings = leaderSettings;
            KubernetesClientConfiguration config = KubernetesClientConfiguration.InClusterConfig();
            _client = new Kubernetes(config);
        }

        public async IAsyncEnumerable<string> FindOtherHordeStorageInstances()
        {
            V1PodList podList = await _client.ListNamespacedPodAsync(_leaderSettings.CurrentValue.Namespace, labelSelector: _leaderSettings.CurrentValue.HordeStoragePodLabelSelector);

            foreach (V1Pod pod in podList.Items)
            {
                string ip = pod.Status.PodIP;
                if (string.IsNullOrEmpty(ip))
                {
                    continue;
                }
                // we typically expose the container on port 80
                yield return $"{ip}:80";
            }
        }

        private void Dispose(bool disposing)
        {
            if (disposing)
            {
                _client.Dispose();
            }
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }
    }
}
