// Copyright Epic Games, Inc. All Rights Reserved.

using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Horde.Storage.Implementation;
using Microsoft.Extensions.Options;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using Moq.Contrib.HttpClient;

namespace Horde.Storage.FunctionalTests.Status
{
    [TestClass]
    public class PeerStatusTests
    {
        [TestMethod]
        public async Task GetPeerConnection()
        {
            ClusterSettings settings = new ClusterSettings
            {
                Peers = new PeerSettings[]
                {
                    new PeerSettings()
                    {
                        Name = "siteA",
                        FullName = "siteA",
                        Endpoints = new PeerEndpoints[]
                        {
                            new PeerEndpoints()
                            {
                                Url = "http://siteA.com/internal",
                                IsInternal = true
                            },
                            new PeerEndpoints()
                            {
                                Url = "http://siteA.com/public"
                            },
                        }.ToList()
                    },
                    new PeerSettings()
                    {
                        Name = "siteB",
                        FullName = "siteB",
                        Endpoints = new PeerEndpoints[]
                        {
                            new PeerEndpoints()
                            {
                                Url = "http://siteB.com/internal",
                                IsInternal = true
                            },
                            new PeerEndpoints()
                            {
                                Url = "http://siteB.com/public"
                            },
                        }.ToList()
                    },

                }.ToList()
            };
            IOptionsMonitor<ClusterSettings> settingsMock = Mock.Of<IOptionsMonitor<ClusterSettings>>(_ => _.CurrentValue == settings);

            Mock<HttpMessageHandler> handler = new Mock<HttpMessageHandler>();
            string endpoint = "/health/live";
            // this emulates response times from calling the different endpoints
            handler.SetupRequest("http://sitea.com/public" + endpoint).ReturnsResponse(HttpStatusCode.OK).Callback(() => Task.Delay(220).Wait()).Verifiable();
            handler.SetupRequest("http://sitea.com/internal" + endpoint).ReturnsResponse(HttpStatusCode.OK).Callback(() => Task.Delay(200).Wait()).Verifiable();
            handler.SetupRequest("http://siteb.com/public" + endpoint).ReturnsResponse(HttpStatusCode.OK).Callback(() => Task.Delay(420).Wait()).Verifiable();
            handler.SetupRequest("http://siteb.com/internal" + endpoint).ReturnsResponse(HttpStatusCode.OK).Callback(() => Task.Delay(400).Wait()).Verifiable();

            IHttpClientFactory httpClientFactory = handler.CreateClientFactory();
            PeerStatusService statusService = new PeerStatusService(settingsMock, httpClientFactory);

            await statusService.UpdatePeerStatus(CancellationToken.None);

            handler.Verify();

            // as we are actually measuring the time it takes to call our mocked handler the latency expected is not going to be exact so we allow some delta
            IPeerStatusService.PeerStatus siteAPeerStatus = statusService.GetPeerStatus("siteA");
            Assert.AreEqual(220, siteAPeerStatus.Latency, 50);
            Assert.IsTrue(siteAPeerStatus.Reachable);

            IPeerStatusService.PeerStatus siteBPeerStatus = statusService.GetPeerStatus("siteB");
            Assert.AreEqual(420, siteBPeerStatus.Latency, 50);
            Assert.IsTrue(siteBPeerStatus.Reachable);
        }

        [TestMethod]
        public async Task PeerUnreachable()
        {
            ClusterSettings settings = new ClusterSettings
            {
                Peers = new PeerSettings[]
                {
                    new PeerSettings()
                    {
                        Name = "siteA",
                        FullName = "siteA",
                        Endpoints = new PeerEndpoints[]
                        {
                            new PeerEndpoints()
                            {
                                Url = "http://siteA.com/internal",
                                IsInternal = true
                            },
                            new PeerEndpoints()
                            {
                                Url = "http://siteA.com/public"
                            },
                        }.ToList()
                    },
                    new PeerSettings()
                    {
                        Name = "siteB",
                        FullName = "siteB",
                        Endpoints = new PeerEndpoints[]
                        {
                            new PeerEndpoints()
                            {
                                Url = "http://siteB.com/internal",
                                IsInternal = true
                            },
                            new PeerEndpoints()
                            {
                                Url = "http://siteB.com/public"
                            },
                        }.ToList()
                    },
                }.ToList()
            };
            IOptionsMonitor<ClusterSettings> settingsMock = Mock.Of<IOptionsMonitor<ClusterSettings>>(_ => _.CurrentValue == settings);

            Mock<HttpMessageHandler> handler = new Mock<HttpMessageHandler>();
            string endpoint = "/health/live";
            handler.SetupRequest("http://sitea.com/public" + endpoint).ReturnsResponse(HttpStatusCode.OK).Callback(() => Task.Delay(220).Wait()).Verifiable();
            handler.SetupRequest("http://sitea.com/internal" + endpoint).Throws<SocketException>().Verifiable();

            // site b is not reachable at all
            handler.SetupRequest("http://siteb.com/public" + endpoint).Throws<SocketException>().Verifiable();
            handler.SetupRequest("http://siteb.com/internal" + endpoint).Throws<SocketException>().Verifiable();

            IHttpClientFactory httpClientFactory = handler.CreateClientFactory();
            PeerStatusService statusService = new PeerStatusService(settingsMock, httpClientFactory);

            await statusService.UpdatePeerStatus(CancellationToken.None);

            handler.Verify();

            // as we are actually measuring the time it takes to call our mocked handler the latency expected is not going to be exact so we allow some delta
            IPeerStatusService.PeerStatus siteAPeerStatus = statusService.GetPeerStatus("siteA");
            
            // verify that we ignore the failing internal endpoint
            Assert.AreEqual(220, siteAPeerStatus.Latency, 50);
            Assert.IsTrue(siteAPeerStatus.Reachable);

            IPeerStatusService.PeerStatus siteBPeerStatus = statusService.GetPeerStatus("siteB");
            Assert.AreEqual(int.MaxValue, siteBPeerStatus.Latency);
            Assert.IsFalse(siteBPeerStatus.Reachable);
        }
    }
}
