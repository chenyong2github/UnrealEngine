// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Dasync.Collections;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class FileSystemStore : IBlobStore, IBlobCleanup 
    {
        private readonly ILogger _logger = Log.ForContext<FileSystemStore>();
        private IOptionsMonitor<FilesystemSettings> _settings;

        internal const int DefaultBufferSize = 4096;

        public FileSystemStore(IOptionsMonitor<FilesystemSettings> settings)
        {
            _settings = settings;
        }

        public static FileInfo GetFilesystemPath(string rootDir, NamespaceId ns, BlobIdentifier blob)
        {
            const int CountOfCharactersPerDirectory = 2;
            string objectName = blob.ToString();
            string firstPart = objectName.Substring(0, CountOfCharactersPerDirectory);
            string secondPart = objectName.Substring(CountOfCharactersPerDirectory, CountOfCharactersPerDirectory);
            string fileName = objectName;

            return new FileInfo(Path.Combine(rootDir, ns.ToString(), firstPart, secondPart, fileName));
        }

        public FileInfo GetFilesystemPath(NamespaceId ns, BlobIdentifier objectName)
        {
            return GetFilesystemPath(_settings.CurrentValue.RootDir, ns, objectName);
        }

        public DirectoryInfo GetFilesystemPath(NamespaceId ns)
        {
            return new DirectoryInfo(Path.Combine(_settings.CurrentValue.RootDir, ns.ToString()));
        }

        public async Task<BlobIdentifier> PutObject(NamespaceId ns, ReadOnlyMemory<byte> content, BlobIdentifier blobIdentifier)
        {
            FileInfo filePath = GetFilesystemPath(ns, blobIdentifier);
            filePath.Directory?.Create();

            await using FileStream fs = new FileStream(filePath.FullName, FileMode.Create, FileAccess.Write, FileShare.Read, DefaultBufferSize, FileOptions.Asynchronous | FileOptions.SequentialScan);
            CancellationToken cancellationToken = default(CancellationToken);
            await fs.WriteAsync(content, cancellationToken);
            await fs.FlushAsync(cancellationToken);

            UpdateLastWriteTime(filePath.FullName, DateTime.UnixEpoch);
            return blobIdentifier;
        }


        public async Task<BlobIdentifier> PutObject(NamespaceId ns, Stream content, BlobIdentifier blobIdentifier)
        {
            FileInfo filePath = GetFilesystemPath(ns, blobIdentifier);
            filePath.Directory?.Create();

            await using FileStream fs = new FileStream(filePath.FullName, FileMode.Create, FileAccess.Write, FileShare.Read, DefaultBufferSize, FileOptions.Asynchronous | FileOptions.SequentialScan);
            CancellationToken cancellationToken = default(CancellationToken);
            await content.CopyToAsync(fs, cancellationToken);
            await fs.FlushAsync(cancellationToken);

            UpdateLastWriteTime(filePath.FullName, DateTime.UnixEpoch);
            return blobIdentifier;
        }

        public async Task<BlobIdentifier> PutObject(NamespaceId ns, byte[] content, BlobIdentifier blobIdentifier)
        {
            FileInfo filePath = GetFilesystemPath(ns, blobIdentifier);
            filePath.Directory?.Create();

            await using FileStream fs = new FileStream(filePath.FullName, FileMode.Create, FileAccess.Write, FileShare.Read, DefaultBufferSize, FileOptions.Asynchronous | FileOptions.SequentialScan);
            CancellationToken cancellationToken = default(CancellationToken);
            await fs.WriteAsync(content, cancellationToken);
            await fs.FlushAsync(cancellationToken);

            UpdateLastWriteTime(filePath.FullName, DateTime.UnixEpoch);
            return blobIdentifier;
        }

        public Task<BlobContents> GetObject(NamespaceId ns, BlobIdentifier blob)
        {
            FileInfo filePath = GetFilesystemPath(ns, blob);

            if (!filePath.Exists)
                throw new BlobNotFoundException(ns, blob);

            FileStream fs = new FileStream(filePath.FullName, FileMode.Open, FileAccess.Read, FileShare.Read, DefaultBufferSize, FileOptions.Asynchronous | FileOptions.SequentialScan);
            
            UpdateLastWriteTime(filePath.FullName, DateTime.UtcNow);
            return Task.FromResult(new BlobContents(fs, fs.Length));
        }

        /// <summary>
        /// Update the last modified/write time that is used for determining when file was last accessed
        /// 
        /// Using access time is tricky as many file systems disable that for performance reasons.
        /// A new blob written to disk should be set with the oldest possible write time.
        /// This will ensure sorting of least recently accessed files during garbage collection works as intended.
        /// The write time update will happen async without any waiting to prevent blocking the critical path
        /// as it's best-effort only.
        /// 
        /// See <seealso cref="GetLeastRecentlyAccessedObjects" />.
        /// </summary>
        /// <param name="filePath"></param>
        /// <param name="lastAccessed">Time the file was last accessed</param>
        private void UpdateLastWriteTime(string filePath, DateTime lastAccessed)
        {
            Task.Run(() =>
            {
                try
                {
                    File.SetLastWriteTimeUtc(filePath, lastAccessed);
                }
                catch (FileNotFoundException)
                {
                    // it is okay if the file does not exist anymore, that just means it got gced
                }
            }).ContinueWith(task =>
            {
                if (task.Exception != null)
                {
                    _logger.Error(task.Exception, "Exception when updating write time for {FilePath}", filePath);
                }
            });
        }

        public Task<bool> Exists(NamespaceId ns, BlobIdentifier blob)
        {
            FileInfo filePath = GetFilesystemPath(ns, blob);

            return Task.FromResult(filePath.Exists);
        }

        public Task DeleteObject(NamespaceId ns, BlobIdentifier objectName)
        {
            FileInfo filePath = GetFilesystemPath(ns, objectName);
            filePath.Delete();
            return Task.CompletedTask;
        }

        public Task DeleteNamespace(NamespaceId ns)
        {
            DirectoryInfo namespaceDirectory = GetFilesystemPath(ns);
            if (namespaceDirectory.Exists)
                namespaceDirectory.Delete(true);

            return Task.CompletedTask;
        }

        public IAsyncEnumerable<BlobIdentifier> ListOldObjects(NamespaceId ns, DateTime cutoff)
        {
            return DoListOldObjects(ns, cutoff).ToAsyncEnumerable();
        }

        private IEnumerable<BlobIdentifier> DoListOldObjects(NamespaceId ns, DateTime cutoff)
        {
            DirectoryInfo di = new DirectoryInfo(Path.Combine(_settings.CurrentValue.RootDir, ns.ToString()));
            if (!di.Exists)
            {
                yield break;
            }

            foreach (FileSystemInfo? file in di.EnumerateFileSystemInfos("*", SearchOption.AllDirectories))
            {
                if (file.Attributes.HasFlag(FileAttributes.Directory))
                {
                    continue;
                }

                if (file.LastWriteTime > cutoff)
                {
                    _logger.Information("Ignoring {Blob} as it is new. Last modified: {LastModified} Cutoff: {CutOff}", file.Name, file.LastWriteTime, cutoff);
                    continue;
                }
                _logger.Information("Old {Blob} found. Last modified: {LastModified} Cutoff: {CutOff}", file.Name, file.LastWriteTime, cutoff);

                yield return new BlobIdentifier(file.Name);
            }
        }
        
        public async Task<List<RemovedBlobs>> Cleanup(CancellationToken cancellationToken)
        {
            return await CleanupInternal(cancellationToken);
        }

        /// <summary>
        /// Clean the store from expired files
        /// 
        /// Uses the configuration to remove the least recently accessed (modified) blobs
        /// </summary>
        /// <param name="cancellationToken">Cancellation token</param>
        /// <param name="batchSize">Number of files to scan for clean up. A higher number is recommended since blob store can contain many small blobs</param>
        /// <returns></returns>
        public async Task<List<RemovedBlobs>> CleanupInternal(CancellationToken cancellationToken, int batchSize = 100000)
        {
            long size = await CalculateDiskSpaceUsed();
            ulong maxSizeBytes = _settings.CurrentValue.MaxSizeBytes;
            long triggerSize = (long) (maxSizeBytes * _settings.CurrentValue.TriggerThresholdPercentage);
            long targetSize = (long) (maxSizeBytes * _settings.CurrentValue.TargetThresholdPercentage); // Target to shrink to if triggered
            List<RemovedBlobs> blobsRemoved = new List<RemovedBlobs>(batchSize);

            if (size < triggerSize)
            {
                return blobsRemoved;
            }

            // Perform a maximum of 5 clean up runs
            for (int i = 0; i < 5; i++)
            {
                size = await CalculateDiskSpaceUsed();
                if (size <= targetSize)
                {
                    return blobsRemoved;
                }
                
                FileInfo[] fileInfos = await GetLeastRecentlyAccessedObjects(maxResults: batchSize);
                if (fileInfos.Length == 0)
                {
                    return blobsRemoved;
                }
                
                long totalBytesDeleted = 0;
                foreach (FileInfo fi in fileInfos)
                {
                    try
                    {
                        totalBytesDeleted += fi.Length;
                        fi.Delete();
                        blobsRemoved.Add(new RemovedBlobs(new BlobIdentifier(fi.Name)));

                        long currentSize = size - totalBytesDeleted;
                        if (currentSize <= targetSize || cancellationToken.IsCancellationRequested)
                        {
                            return blobsRemoved;
                        }
                    }
                    catch (FileNotFoundException)
                    {
                        // if the file was gced while running we can just ignore it
                    }
                }
            }
            
            return blobsRemoved;
        }

        /// <summary>
        /// Get least recently accessed objects
        /// Assumes files on disk have the their last access timestamp up-to-date
        /// </summary>
        /// <param name="ns">Namespace, if set to null all namespaces will be scanned</param>
        /// <param name="maxResults">Max results to return. Note that the entire namespace will be scanned no matter what.</param>
        /// <returns>Enumerable of least recently accessed objects as FileInfos</returns>
        public async Task<FileInfo[]> GetLeastRecentlyAccessedObjects(NamespaceId? ns = null, int maxResults = 10000)
        {
            string path = ns != null ? Path.Combine(_settings.CurrentValue.RootDir, ns.ToString()!) : _settings.CurrentValue.RootDir;
            DirectoryInfo di = new DirectoryInfo(path);
            if (!di.Exists)
            {
                return Array.Empty<FileInfo>();
            }
            
            return await Task.Run(() => di.EnumerateFiles("*", SearchOption.AllDirectories).OrderBy(x => x.LastWriteTime).Take(maxResults).ToArray());
        }
        
        /// <summary>
        /// Calculate the total size of blobs on disk for given namespace
        /// </summary>
        /// <param name="ns">Namespace, if set to null the total size of all namespaces will be returned</param>
        /// <returns>Total size of blobs in bytes</returns>
        public async Task<long> CalculateDiskSpaceUsed(NamespaceId? ns = null)
        {
            string path = ns != null ? Path.Combine(_settings.CurrentValue.RootDir, ns.ToString()!) : _settings.CurrentValue.RootDir;
            DirectoryInfo di = new DirectoryInfo(path);
            if (!di.Exists)
            {
                return 0;
            }
            
            return await Task.Run(() => di.EnumerateFiles("*", SearchOption.AllDirectories).Sum(x =>
            {
                try
                {
                    return x.Length;
                }
                catch (FileNotFoundException)
                {
                    // if the file has been gced we just ignore it
                    return 0;
                }
            }));
        }
        
        public  IAsyncEnumerable<NamespaceId> ListNamespaces()
        {
            DirectoryInfo di = new DirectoryInfo(_settings.CurrentValue.RootDir);
            return di.GetDirectories().Select(x => new NamespaceId(x.Name)).ToAsyncEnumerable();
        }
    }
}
