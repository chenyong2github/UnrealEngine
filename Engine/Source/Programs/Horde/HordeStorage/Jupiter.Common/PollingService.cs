// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Hosting;
using Serilog;

namespace Jupiter
{
    public abstract class PollingService<T> : IHostedService
    {
        private struct ThreadState
        {
            public string ServiceName { get; set; }
            public TimeSpan PollFrequency { get; set; }

            public CancellationToken StopPollingToken { get; set; }
            public T ServiceState { get; set; }
            public PollingService<T> Instance { get; set; }
        }

        private readonly ILogger _logger = Log.ForContext<PollingService<T>>();
        private readonly string _serviceName;
        private readonly TimeSpan _pollFrequency;
        private readonly T _state;
        private readonly Thread _pollingThread = new Thread(OnUpdate);
        private readonly CancellationTokenSource _stopPolling = new CancellationTokenSource();

        public PollingService(string serviceName, TimeSpan pollFrequency, T state)
        {
            _serviceName = serviceName;
            _pollFrequency = pollFrequency;
            _state = state;
        }

        public bool Running
        {
            get { return _pollingThread.IsAlive; }
        }

        public T State
        {
            get { return _state; }
        }

        public virtual bool ShouldStartPolling()
        {
            return true;
        }

        public Task StartAsync(CancellationToken cancellationToken)
        {
            bool shouldPoll = ShouldStartPolling();
            _logger.Information("Polling service {Service} initialized {@State} , will poll: {WillPoll}.", _serviceName, _state, shouldPoll);

            if (shouldPoll)
            {
                _pollingThread.Start(new ThreadState
                {
                    ServiceName = _serviceName, 
                    PollFrequency = _pollFrequency, 
                    ServiceState = _state, 
                    StopPollingToken = _stopPolling.Token,
                    Instance = this,
                });
            }

            return Task.CompletedTask;
        }

        private static void OnUpdate(object? state)
        {
            ThreadState? ts = (ThreadState?)state;
            if (ts == null)
            {
                throw new ArgumentNullException(nameof(ts));
            }
            ThreadState threadState = ts.Value;

            PollingService<T> instance = threadState.Instance;
            string serviceName = threadState.ServiceName;
            CancellationToken stopPollingToken = threadState.StopPollingToken;
            ILogger logger = Log.ForContext<PollingService<T>>();

            while (!stopPollingToken.IsCancellationRequested)
            {
                var startTime = DateTime.Now;
                try
                {
                    bool _ = instance.OnPoll(threadState.ServiceState, stopPollingToken).Result;
                }
                catch (AggregateException e)
                {
                    logger.Error(e, "{Service} Aggregate exception in polling thread", serviceName);
                    foreach (Exception inner in e.InnerExceptions)
                    {
                        logger.Error(inner, "{Service} inner exception in polling thread. Trace: {StackTrace}", serviceName, inner.StackTrace);
                        
                    }
                }
                catch (Exception e)
                {
                    logger.Error(e, "{Service} Exception in polling thread", serviceName);
                }
                finally
                {
                    TimeSpan duration = DateTime.Now - startTime;
                    TimeSpan pollFrequency = threadState.PollFrequency;

                    // if we spent less then the poll frequency we sleep to avoid ping the remote server to much 
                    if (duration < pollFrequency)
                    {
                        TimeSpan remainingDuration = pollFrequency - duration;
                        logger.Information("{Service} ran polled for {Duration} Sleeping for {RemainingDuration} to stay within poll frequency", serviceName, duration, remainingDuration);

                        Thread.Sleep(remainingDuration);
                    }
                }
            }
        }

        public abstract Task<bool> OnPoll(T state, CancellationToken cancellationToken);

        protected virtual Task OnStopping(T state)
        {
            return Task.CompletedTask;
        }

        public async Task StopAsync(CancellationToken cancellationToken)
        {
            _logger.Information("{Service} poll service stopping.", _serviceName);

            await OnStopping(_state);

            _stopPolling.Cancel();
            if (_pollingThread.IsAlive)
            {
                _pollingThread.Join();
            }
        }
    }
}
