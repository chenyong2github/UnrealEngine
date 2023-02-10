// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace UnrealGameSync
{
	/// <summary>
	/// Encapsulates the state of cross-process workspace lock
	/// </summary>
	public class WorkspaceLock : IDisposable
	{
		const string Prefix = @"Global\ugs-workspace";

		readonly Mutex _mutex;

		readonly object _lockObject = new object();
		readonly string _objectName;
		int _acquireCount;

		bool _locked;
		EventWaitHandle? _lockedEvent;

		Thread? _monitorThread;
		readonly ManualResetEvent _cancelMonitorEvent = new ManualResetEvent(false);

		/// <summary>
		/// Callback for the lock state changing
		/// </summary>
		public event Action<bool>? OnChange;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="rootDir">Root directory for the workspace</param>
		public WorkspaceLock(DirectoryReference rootDir)
		{
			using (MD5 md5 = MD5.Create())
			{
				byte[] idBytes = Encoding.UTF8.GetBytes(rootDir.FullName.ToUpperInvariant());
				_objectName = StringUtils.FormatHexString(md5.ComputeHash(idBytes));
			}

			_mutex = new Mutex(false, $"{Prefix}.{_objectName}.mutex");

			_monitorThread = new Thread(MonitorThread);
			_monitorThread.IsBackground = true;
			_monitorThread.Start();
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			while (_acquireCount > 0)
			{
				Release();
			}

			if (_monitorThread != null)
			{
				_cancelMonitorEvent.Set();

				_monitorThread.Join();
				_monitorThread = null;

				_cancelMonitorEvent.Dispose();
			}

			_mutex.Dispose();
		}

		/// <summary>
		/// Determines if the lock is held by any
		/// </summary>
		/// <returns>True if the lock is held by any process</returns>
		public bool IsLocked() => _locked;

		/// <summary>
		/// Determines if the lock is held by another process
		/// </summary>
		/// <returns>True if the lock is held by another process</returns>
		public bool IsLockedByOtherProcess() => _acquireCount == 0 && IsLocked();

		/// <summary>
		/// Attempt to acquire the mutext
		/// </summary>
		/// <returns></returns>
		public bool TryAcquire()
		{
			lock (_lockObject)
			{
				try
				{
					if (!_mutex.WaitOne(0))
					{
						return false;
					}
				}
				catch (AbandonedMutexException)
				{
				}

				if (++_acquireCount == 1)
				{
					_lockedEvent = CreateLockedEvent();
					_lockedEvent.Set();
				}

				return true;
			}
		}

		/// <summary>
		/// Release the current mutext
		/// </summary>
		public void Release()
		{
			lock (_lockObject)
			{
				if (_acquireCount > 0)
				{
					_mutex.ReleaseMutex();
					if (--_acquireCount == 0 && _lockedEvent != null)
					{
						_lockedEvent.Reset();
						_lockedEvent.Dispose();
						_lockedEvent = null;
					}
				}
			}
		}

		void MonitorThread()
		{
			_locked = IsLocked();
			for (; ;)
			{
				if (_locked)
				{
					try
					{
						int idx = WaitHandle.WaitAny(new WaitHandle[] { _mutex, _cancelMonitorEvent });
						if (idx == 1)
						{
							break;
						}
					}
					catch (AbandonedMutexException)
					{
					}

					_mutex.ReleaseMutex();
				}
				else
				{
					using EventWaitHandle lockedEvent = CreateLockedEvent();

					int idx = WaitHandle.WaitAny(new WaitHandle[] { lockedEvent, _cancelMonitorEvent });
					if (idx == 1)
					{
						break;
					}
				}

				_locked ^= true;
				OnChange?.Invoke(!_locked);
			}
		}

		EventWaitHandle CreateLockedEvent() => new EventWaitHandle(false, EventResetMode.ManualReset, $"{Prefix}.{_objectName}.locked");
	}
}
