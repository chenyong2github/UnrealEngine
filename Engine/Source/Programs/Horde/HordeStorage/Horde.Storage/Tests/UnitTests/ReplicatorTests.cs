// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;
using System.Threading;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System.Threading.Tasks;
using Horde.Storage.Implementation;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;
using Moq;
using RestSharp;

namespace Horde.Storage.UnitTests
{
    [TestClass]
    public class ReplicatorTests
    {
        private readonly DirectoryInfo _tempPath = new DirectoryInfo(Path.GetTempPath());
        private readonly Guid _currentLogGeneration = new Guid();
        private const string CurrentSite = "Test";
        private const string ReplicatorNameV1 = "TestReplicatorV1";

        private readonly NamespaceId NamespaceV1 = new NamespaceId("test-namespace-v1");

        [TestInitialize]
        public void CleanupTempDir()
        {
            {
                string stateFile = Path.Combine(_tempPath.FullName, path2: $"{ReplicatorNameV1}.json");
                if (File.Exists(stateFile))
                {
                    File.Delete(stateFile);
                }
            }
        }

        [TestMethod]
        public async Task ReplicatorAddTransactionReplicatorV1()
        {
            ReplicatorSettings replicatorSettings = new ReplicatorSettings
            {
                NamespaceToReplicate = NamespaceV1, ReplicatorName = ReplicatorNameV1, Version = ReplicatorVersion.V1
            };

            byte[] contents = Encoding.ASCII.GetBytes("test string");
            BlobIdentifier blobToReplication = BlobIdentifier.FromBlob(contents);

            Mock<IBlobStore> blobStoreMock = new Mock<IBlobStore>();
            IServiceCredentials serviceCredentials = Mock.Of<IServiceCredentials>();
            Mock<ITransactionLogWriter> transactionLogWriter = new Mock<ITransactionLogWriter>();
            Mock<IRestClient> remoteClientMock = new Mock<IRestClient> { DefaultValue = DefaultValue.Empty };

            CallistoReader.CallistoGetResponse[] responses = new[]
            {
                new CallistoReader.CallistoGetResponse(
                    generation: _currentLogGeneration,
                    currentOffset: 0,
                    events: new List<TransactionEvent>(new[]
                    {
                        new AddTransactionEvent("", "", new[] {blobToReplication}, identifier:0, nextIdentifier: 100)
                    })
                ),
                new CallistoReader.CallistoGetResponse(
                    generation: _currentLogGeneration,
                    currentOffset: 100,
                    events: new List<TransactionEvent>()
                )
            };

            Mock<IRestResponse<CallistoReader.CallistoGetResponse>>[] mockedCallistoResponse = responses.Select(response => CreateMockResponse(HttpStatusCode.OK, response)).ToArray();
            // mock of callisto calls
            remoteClientMock
                .SetupSequence(x => x.ExecuteGetAsync<CallistoReader.CallistoGetResponse>(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()))
                // first we will query for a callisto event
                .ReturnsAsync(mockedCallistoResponse[0].Object)
                // then it will iterate again and should now reach the stop event
                .ReturnsAsync(mockedCallistoResponse[0].Object)
                .ReturnsAsync(mockedCallistoResponse[1].Object);

            // mock of io calls
            Mock<IRestResponse> mockedIoResponse = CreateMockResponseRaw(HttpStatusCode.OK, contents);
            remoteClientMock
                .Setup(x => x.ExecuteGetAsync(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync(mockedIoResponse.Object);

            ReplicationSettings replicationSettings = new ReplicationSettings {CurrentSite = CurrentSite, StateRoot = _tempPath.FullName};
            IOptionsMonitor<ReplicationSettings> replicationSettingsMonitor = Mock.Of<IOptionsMonitor<ReplicationSettings>>(_ => _.CurrentValue == replicationSettings);

            using IReplicator replicator = new ReplicatorV1(replicatorSettings, replicationSettingsMonitor, blobStoreMock.Object, transactionLogWriter.Object, remoteClientMock.Object);

            Assert.IsNull(replicator.State.ReplicatorOffset,"Expected state to have been reset during test initialize");

            bool ran = await replicator.TriggerNewReplications();
            Assert.IsTrue(ran);

            NamespaceId ns = replicatorSettings.NamespaceToReplicate;

            // we should have checked the local io once for if we had the blob
            blobStoreMock.Verify(blobStore => blobStore.Exists(ns, blobToReplication), Times.Once);

            // we should have attempted to store the replicated object in our callisto
            remoteClientMock.Verify(client => client.ExecuteGetAsync<CallistoReader.CallistoGetResponse>(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()), Times.Exactly(3));
            transactionLogWriter.Verify(writer => writer.Add(ns, It.Is<AddTransactionEvent>(e => e.Blobs[0].Equals(blobToReplication))), Times.Once);

            // as this was a add operation we should have transferred the blob from the remote blob store to the local
            remoteClientMock.Verify(client => client.ExecuteGetAsync(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()), Times.Once);
            blobStoreMock.Verify(blobStore => blobStore.PutObject(ns, It.IsAny<byte[]>(), blobToReplication), Times.Once);

            Assert.AreEqual(_currentLogGeneration, replicator.State.ReplicatingGeneration);

            remoteClientMock.VerifyNoOtherCalls();
        }


        [TestMethod]
        public async Task ReplicatorSkipLogReplicatorV1()
        {
            ReplicatorSettings replicatorSettings = new ReplicatorSettings
            {
                NamespaceToReplicate = NamespaceV1, ReplicatorName = ReplicatorNameV1, Version = ReplicatorVersion.V1
            };

            Mock<IRestClient> remoteClientMock = new Mock<IRestClient> { DefaultValue = DefaultValue.Empty };

            CallistoReader.CallistoGetResponse[] responses = new[]
            {
                new CallistoReader.CallistoGetResponse(
                    generation: _currentLogGeneration,
                    currentOffset: 100000,
                    events: new List<TransactionEvent>()
                ),
            };
            Mock<IRestResponse<CallistoReader.CallistoGetResponse>>[] mockedCallistoResponse = responses.Select(response => CreateMockResponse(HttpStatusCode.OK, response)).ToArray();
            // mock of callisto calls
            remoteClientMock
                .SetupSequence(x => x.ExecuteGetAsync<CallistoReader.CallistoGetResponse>(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync(mockedCallistoResponse[0].Object)
                .ReturnsAsync(mockedCallistoResponse[0].Object);

            Mock<IBlobStore> blobStoreMock = new Mock<IBlobStore>();
            Mock<ITransactionLogWriter> transactionLogWriter = new Mock<ITransactionLogWriter>();
            ReplicationSettings replicationSettings = new ReplicationSettings {CurrentSite = CurrentSite, StateRoot = _tempPath.FullName};
            IOptionsMonitor<ReplicationSettings> replicationSettingsMonitor = Mock.Of<IOptionsMonitor<ReplicationSettings>>(_ => _.CurrentValue == replicationSettings);

            using IReplicator replicator = new ReplicatorV1(replicatorSettings, replicationSettingsMonitor, blobStoreMock.Object, transactionLogWriter.Object, remoteClientMock.Object);

            Assert.IsNull(replicator.State.ReplicatorOffset,"Expected state to have been reset during test initialize");

            bool replicatedEvents = await replicator.TriggerNewReplications();
            Assert.IsFalse(replicatedEvents);

            blobStoreMock.VerifyNoOtherCalls();
            transactionLogWriter.VerifyNoOtherCalls();

            Assert.AreEqual(100000, replicator.State.ReplicatorOffset);
            Assert.AreEqual(_currentLogGeneration, replicator.State.ReplicatingGeneration);

            // we should called the transaction log 2
            remoteClientMock.Verify(client => client.ExecuteGetAsync<CallistoReader.CallistoGetResponse>(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()), Times.Exactly(2));

            remoteClientMock.VerifyNoOtherCalls();
        }
        
        [TestMethod]
        public async Task ReplicatorSendingIncorrectPayloadV1()
        {
            ReplicatorSettings replicatorSettings = new ReplicatorSettings
            {
                NamespaceToReplicate = NamespaceV1, ReplicatorName = ReplicatorNameV1, Version = ReplicatorVersion.V1
            };

            byte[] contents = Encoding.ASCII.GetBytes("test string");
            BlobIdentifier blobToReplication = BlobIdentifier.FromBlob(contents);

            Mock<IBlobStore> blobStoreMock = new Mock<IBlobStore>();
            IServiceCredentials serviceCredentials = Mock.Of<IServiceCredentials>();
            Mock<ITransactionLogWriter> transactionLogWriter = new Mock<ITransactionLogWriter>();
            Mock<IRestClient> remoteClientMock = new Mock<IRestClient> { DefaultValue = DefaultValue.Empty };

            CallistoReader.CallistoGetResponse[] responses = new[]
            {
                new CallistoReader.CallistoGetResponse(
                    generation: _currentLogGeneration,
                    currentOffset: 0,
                    events: new List<TransactionEvent>(new[]
                    {
                        new AddTransactionEvent("", "", new[] {blobToReplication}, identifier:0, nextIdentifier: 100)
                    })
                ),
                new CallistoReader.CallistoGetResponse(
                    generation: _currentLogGeneration,
                    currentOffset: 100,
                    events: new List<TransactionEvent>()
                )
            };

            Mock<IRestResponse<CallistoReader.CallistoGetResponse>>[] mockedCallistoResponse = responses.Select(response => CreateMockResponse(HttpStatusCode.OK, response)).ToArray();
            // mock of callisto calls
            remoteClientMock
                .SetupSequence(x => x.ExecuteGetAsync<CallistoReader.CallistoGetResponse>(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()))
                // first we will query for a callisto event
                .ReturnsAsync(mockedCallistoResponse[0].Object)
                // then it will iterate again and should now reach the stop event
                .ReturnsAsync(mockedCallistoResponse[0].Object)
                .ReturnsAsync(mockedCallistoResponse[1].Object);

            // mock of io calls
            // return a empty array (0 byte response) from the remote IO and on retry returns a valid object
            Mock<IRestResponse> mockedIoResponse = CreateMockResponseRaw(HttpStatusCode.OK, Array.Empty<byte>());
            Mock<IRestResponse> mockedIoResponse2 = CreateMockResponseRaw(HttpStatusCode.OK, contents);
            remoteClientMock
                .SetupSequence(x => x.ExecuteGetAsync(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync(mockedIoResponse.Object)
                .ReturnsAsync(mockedIoResponse2.Object);

            ReplicationSettings replicationSettings = new ReplicationSettings {CurrentSite = CurrentSite, StateRoot = _tempPath.FullName};
            IOptionsMonitor<ReplicationSettings> replicationSettingsMonitor = Mock.Of<IOptionsMonitor<ReplicationSettings>>(_ => _.CurrentValue == replicationSettings);

            using IReplicator replicator = new ReplicatorV1(replicatorSettings, replicationSettingsMonitor, blobStoreMock.Object, transactionLogWriter.Object, remoteClientMock.Object);

            Assert.IsNull(replicator.State.ReplicatorOffset,"Expected state to have been reset during test initialize");

            bool ran = await replicator.TriggerNewReplications();
            Assert.IsTrue(ran);

            NamespaceId ns = replicatorSettings.NamespaceToReplicate;

            // we should have checked the local io once for if we had the blob
            blobStoreMock.Verify(blobStore => blobStore.Exists(ns, blobToReplication), Times.Once);

            // we should have attempted to store the replicated object in our callisto
            remoteClientMock.Verify(client => client.ExecuteGetAsync<CallistoReader.CallistoGetResponse>(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()), Times.Exactly(3));
            transactionLogWriter.Verify(writer => writer.Add(ns, It.Is<AddTransactionEvent>(e => e.Blobs[0].Equals(blobToReplication))), Times.Once);

            // as this was a add operation we should have transferred the blob from the remote blob store to the local
            remoteClientMock.Verify(client => client.ExecuteGetAsync(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()), Times.Exactly(2));
            blobStoreMock.Verify(blobStore => blobStore.PutObject(ns, It.IsAny<byte[]>(), blobToReplication), Times.Once);

            Assert.AreEqual(_currentLogGeneration, replicator.State.ReplicatingGeneration);

            remoteClientMock.VerifyNoOtherCalls();
        }


        [TestMethod]
        public async Task ReplicatorDeleteTransactionV1()
        {
            ReplicatorSettings replicatorSettings = new ReplicatorSettings
            {
                NamespaceToReplicate = NamespaceV1, ReplicatorName = ReplicatorNameV1, Version = ReplicatorVersion.V1
            };

            NamespaceId ns = replicatorSettings.NamespaceToReplicate;

            Mock<IBlobStore> blobStoreMock = new Mock<IBlobStore>();
            IServiceCredentials serviceCredentials = Mock.Of<IServiceCredentials>();
            Mock<ITransactionLogWriter> transactionLogWriter = new Mock<ITransactionLogWriter>();
            Mock<IRestClient> remoteClientMock = new Mock<IRestClient> { DefaultValue = DefaultValue.Empty };

            CallistoReader.CallistoGetResponse[] responses = new[]
            {
                new CallistoReader.CallistoGetResponse(
                    generation: _currentLogGeneration,
                    currentOffset: 0,
                    events: new List<TransactionEvent>(new[]
                    {
                        new RemoveTransactionEvent("", "", 0, 100)
                    })
                ),
                new CallistoReader.CallistoGetResponse(
                    generation: _currentLogGeneration,
                    currentOffset: 100,
                    events: new List<TransactionEvent>()
                )
            };

            Mock<IRestResponse<CallistoReader.CallistoGetResponse>>[] mockedCallistoResponse = responses.Select(response => CreateMockResponse(HttpStatusCode.OK, response)).ToArray();
            // mock of callisto calls
            remoteClientMock
                .SetupSequence(x => x.ExecuteGetAsync<CallistoReader.CallistoGetResponse>(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()))
                // first we will query for a callisto event
                .ReturnsAsync(mockedCallistoResponse[0].Object)
                // then it will iterate again and should now reach the stop event
                .ReturnsAsync(mockedCallistoResponse[0].Object)
                .ReturnsAsync(mockedCallistoResponse[1].Object);

            ReplicationSettings replicationSettings = new ReplicationSettings {CurrentSite = CurrentSite, StateRoot = _tempPath.FullName};
            IOptionsMonitor<ReplicationSettings> replicationSettingsMonitor = Mock.Of<IOptionsMonitor<ReplicationSettings>>(_ => _.CurrentValue == replicationSettings);

            using IReplicator replicator = new ReplicatorV1(replicatorSettings, replicationSettingsMonitor, blobStoreMock.Object, transactionLogWriter.Object, remoteClientMock.Object);

            Assert.IsNull(replicator.State.ReplicatorOffset, "Expected state to have been reset during test initialize");

            bool ran = await replicator.TriggerNewReplications();
            Assert.IsTrue(ran);

            // we should have fetched the replicated object
            remoteClientMock.Verify(client => client.ExecuteGetAsync<CallistoReader.CallistoGetResponse>(It.IsAny<IRestRequest>(), It.IsAny<CancellationToken>()), Times.Exactly(3));
            transactionLogWriter.Verify(writer => writer.Add(ns, It.IsAny<RemoveTransactionEvent>()), Times.Once);
            // other then copying the remove event we should have written nothing else
            transactionLogWriter.VerifyNoOtherCalls();
            // this was a delete so should not have stored anything in the local store
            blobStoreMock.VerifyNoOtherCalls();

            Assert.AreEqual(_currentLogGeneration, replicator.State.ReplicatingGeneration);

            remoteClientMock.VerifyNoOtherCalls();
        }

        private Mock<IRestResponse<T>> CreateMockResponse<T>(HttpStatusCode httpStatusCode, T data)
        {
            Mock<IRestResponse<T>> response = new Mock<IRestResponse<T>>();
            response.Setup(_ => _.StatusCode).Returns(httpStatusCode);
            response.Setup(_ => _.IsSuccessful).Returns(httpStatusCode == HttpStatusCode.OK);
            response.Setup(_ => _.Data).Returns(data);

            return response;
        }
        
        public static Mock<IRestResponse> CreateMockResponseRaw(HttpStatusCode httpStatusCode, byte[] data)
        {
            Mock<IRestResponse> response = new Mock<IRestResponse>();
            response.Setup(_ => _.StatusCode).Returns(httpStatusCode);
            response.Setup(_ => _.IsSuccessful).Returns(httpStatusCode == HttpStatusCode.OK);
            response.Setup(_ => _.RawBytes).Returns(data);

            return response;
        }

    }
}
