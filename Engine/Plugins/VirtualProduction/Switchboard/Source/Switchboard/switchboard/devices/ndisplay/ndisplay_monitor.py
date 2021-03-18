# Copyright Epic Games, Inc. All Rights Reserved.

from switchboard import message_protocol
from switchboard.switchboard_logging import LOGGER

from PySide2 import QtCore, QtGui
from PySide2.QtCore import Signal, Qt, QTimer, QObject, QAbstractTableModel, QModelIndex
from PySide2.QtGui import QColor

from collections import OrderedDict
from itertools import count
import json, time, traceback

class nDisplayMonitor(QAbstractTableModel):
    ''' This will monitor the status of the nDisplay nodes, in particular regarding sync.
    It polls the listener at the specified rate and the UI should update with this info.
    '''

    ColorWarning = QColor(0x70, 0x40, 0x00)
    ColorNormal  = QColor(0x3d, 0x3d, 0x3d)
    CoreOverloadThresh = 90 # percent utilization
    DataMissingStr = 'n/a' # Sometimes treated specially from listener (PresentMode); also used for display.

    console_exec_issued = Signal()

    def __init__(self, parent):
        QAbstractTableModel.__init__(self, parent)

        self.polling_period_ms = 1000
        self.devicedatas = OrderedDict() # ordered so that we can map row indices to devices

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.poll_sync_status)

        headerdata = [
            ('Node'           , 'The cluster name of this device'), 
            ('Host'           , 'The URL of the remote PC'), 
            ('Connected'      , 'If we are connected to the listener of this device'),
            ('Driver'         , 'GPU driver version'),
            ('PresentMode'    , 'Current presentation mode. Only available once the render node process is running. Expects "Hardware Composed: Independent Flip"'), 
            ('Gpus'           , 'Informs if GPUs are synced.'), 
            ('Displays'       , 'Detected displays and whether they are in sync or not'), 
            ('Fps'            , 'Sync Frame Rate'), 
            ('HouseSync'      , 'Presence of an external sync signal connected to the remote Quadro Sync card'),
            ('SyncSource'     , 'The source of the GPU sync signal'),
            ('Mosaics'        , 'Display grids and their resolutions'),
            ('Taskbar'        , 'Whether the taskbar is set to auto hide or always on top. It is recommended to be consistent across the cluster'),
            ('InFocus'        , 'Whether nDisplay instance window is in Focus. It is recommended to be in focus.'),
            ('ExeFlags'       , 'It is recommended to disable fullscreen opimizations on the unreal executable. Only available once the render node process is running. Expects "DISABLEDXMAXIMIZEDWINDOWEDMODE"'),
            ('OsVer'          , 'Operating system version'),
            ('CpuUtilization' , f"CPU utilization average. The number of overloaded cores (> {self.CoreOverloadThresh}% load) will be displayed in parentheses."),
            ('MemUtilization' , 'Physical memory, utilized / total.'),
            ('GpuUtilization' , 'GPU utilization. The GPU clock speed is displayed in parentheses.'),
            ('GpuTemperature' , 'GPU temperature in degrees celsius. (Max across all sensors.)'),
        ]

        self.colnames = [hd[0] for hd in headerdata]
        self.tooltips = [hd[1] for hd in headerdata]

    def color_for_column(self, colname, value, data):
        ''' Returns the background color for the given cell '''
        if data['Connected'].lower() == 'no':
            if colname == 'Connected':
                return self.ColorWarning
            return self.ColorNormal

        if colname == 'PresentMode':
            good_string = 'Hardware Composed: Independent Flip'
            return self.ColorNormal if good_string in value else self.ColorWarning

        if colname == 'Gpus':
            return self.ColorNormal if 'Synced' in value and 'Free' not in value else self.ColorWarning

        if colname == 'InFocus':
            return self.ColorNormal if 'yes' in value else self.ColorWarning

        if colname == 'ExeFlags':
            good_string = 'DISABLEDXMAXIMIZEDWINDOWEDMODE'
            return self.ColorNormal if good_string in value else self.ColorWarning
        
        if colname == 'Displays':
            is_normal = ('Slave' in value or 'Master' in value) and 'Unsynced' not in value
            return self.ColorNormal if is_normal else self.ColorWarning

        if colname == 'CpuUtilization':
            no_overload = '(' not in value # "(# cores > threshold%)"
            return self.ColorNormal if no_overload else self.ColorWarning

        return self.ColorNormal

    def friendly_osver(self, device):
        ''' Returns a display-friendly string for the device OS version '''
        if device.os_version_label.startswith('Windows'):
            try:
                # Destructuring according to FWindowsPlatformMisc::GetOSVersion, ex: "10.0.19041.1.256.64bit"
                [major, minor, build, product_type, suite_mask, arch] = device.os_version_number.split('.')

                if major == '10' and minor == '0':
                    # FWindowsPlatformMisc::GetOSVersions (with an s) returns a label that includes this
                    # "release ID"/"SDK version", but the mechanism for retrieving it seems fragile.
                    # Based on https://docs.microsoft.com/en-us/windows/release-information/
                    build_to_sdk_version = {
                        '19042': '20H2',
                        '19041': '2004',
                        '18363': '1909',
                        '17763': '1809',
                        '17134': '1803',
                        '14393': '1607',
                        '10240': '1507',
                    }

                    if build in build_to_sdk_version:
                        return f"Windows 10, version {build_to_sdk_version[build]}"

            except ValueError:
                # Mismatched count of splits vs. destructure, fall through to default
                pass

        # Default / fallback
        friendly = device.os_version_label

        if device.os_version_label_sub != '':
            friendly += " " + device.os_version_label_sub

        if device.os_version_number != '':
            friendly += " " + device.os_version_number

        if friendly == '':
            friendly = self.DataMissingStr

        return friendly


    def reset_device_data(self, device, data):
        ''' Sets device data to unconnected state '''
        for colname in self.colnames:
            data[colname] = self.DataMissingStr

        data['Host'] = str(device.ip_address)
        data['Node'] = device.name
        data['Connected'] = 'yes' if device.unreal_client.is_connected else 'no'

        # extra data not in columns
        data['TimeLastFlipGlitch'] = time.time()

    def added_device(self, device):
        ''' Called by the plugin when a new nDisplay device has been added. '''
        data = {}

        self.reset_device_data(device, data)
        self.devicedatas[device.device_hash] = {'device':device, 'data':data, 'time_last_update':0, 'stale':True }

        # notify the UI of the change
        self.layoutChanged.emit()

        # start/continue polling since there is at least one device
        if not self.timer.isActive():
            self.timer.start(self.polling_period_ms)

    def removed_device(self, device):
        ''' Called by the plugin when an nDisplay device has been removed. '''
        self.devicedatas.pop(device.device_hash)

        # notify the UI of the change
        self.layoutChanged.emit()

        # turn off the timer if there are no devices left
        if not self.devicedatas:
            self.timer.stop()

    def handle_stale_device(self, devicedata, deviceIdx):
        ''' Detects if the device is stale, and resets the data if so as to not mislead the user '''
        # if already flagged as stale, no need to do anything
        if devicedata['stale']:
            return

        # check if it has been too long
        time_elapsed_since_last_update = time.time() - devicedata['time_last_update']

        timeout_factor = 4

        if time_elapsed_since_last_update < timeout_factor * self.polling_period_ms * 1e-3:
            return

        # if we're here, it has been too long since the last update
        self.reset_device_data(devicedata['device'], devicedata['data']) 
        devicedata['stale'] = True

        # notify the UI
        row = deviceIdx + 1
        self.dataChanged.emit(self.createIndex(row,1), self.createIndex(row,len(self.colnames))) # [Qt.EditRole]

    def handle_connection_change(self, devicedata, deviceIdx):
        ''' Detects if device connection changed and notifies the UI if a disconnection happens. '''
        device = devicedata['device']
        data = devicedata['data']

        is_connected = device.unreal_client.is_connected
        was_connected = True if data['Connected'] == 'yes' else False

        data['Connected'] = 'yes' if is_connected else 'no'

        if was_connected != is_connected:
            if not is_connected:
                self.reset_device_data(device, data)

            row = deviceIdx + 1
            self.dataChanged.emit(self.createIndex(row,1), self.createIndex(row,len(self.colnames)))

    def poll_sync_status(self):
        ''' Polls sync status for all nDisplay devices '''
        for deviceIdx, devicedata in enumerate(self.devicedatas.values()):
            device = devicedata['device']

            # detect connection changes (a disconnection invalidates data)
            self.handle_connection_change(devicedata, deviceIdx)

            # detect stale devices
            self.handle_stale_device(devicedata, deviceIdx)

            # no point in continuing of not connected to listener
            if not device.unreal_client.is_connected:
                continue

            # create message
            try:
                program_id = device.program_start_queue.running_puuids_named('unreal')[-1]
            except IndexError:
                program_id = '00000000-0000-0000-0000-000000000000'

            _, msg = message_protocol.create_get_sync_status_message(program_id)

            # send get sync status message
            device.unreal_client.send_message(msg)

    def devicedata_from_device(self, device):
        ''' Retrieves the devicedata and index for given device '''
        for deviceIdx, hash_devicedata in enumerate(self.devicedatas.items()):
            device_hash, devicedata = hash_devicedata[0], hash_devicedata[1]
            if device_hash == device.device_hash:
                return (deviceIdx, devicedata)

        raise KeyError

    def populate_sync_data(self, devicedata, message):
        ''' Populates model data with message contents, which comes from 'get sync data' command. '''
        data = devicedata['data']
        device = devicedata['device']

        #
        # Sync Topology
        #
        syncStatus = message['syncStatus']
        syncTopos = syncStatus['syncTopos']

        # Build Gpus, informing which Gpus in each Sync group are in sync
        Gpus = []

        for syncTopo in syncTopos:
            gpu_sync_oks = [gpu['bIsSynced'] for gpu in syncTopo['syncGpus']]
            gpu_sync_yesno = map(lambda x: "Synced" if x else 'Free', gpu_sync_oks)
            Gpus.append('%s' % (', '.join(gpu_sync_yesno)))

        data['Gpus'] = '\n'.join(Gpus) if len(Gpus) > 0 else self.DataMissingStr

        # Build Displays, informing which Display in each Sync group are in sync.
        Displays = []

        bpc_strings = {1:6, 2:8, 3:10, 4:12, 5:16}

        for syncTopo in syncTopos:
            display_sync_states = [f"{syncDisplay['syncState']}({bpc_strings.get(syncDisplay['bpc'], '??')}bpc)" for syncDisplay in syncTopo['syncDisplays']]
            Displays.append(', '.join(display_sync_states))

        data['Displays'] = '\n'.join(Displays) if len(Displays) > 0 else self.DataMissingStr

        # Build Fps
        refreshRates = [f"{syncTopo['syncStatusParams']['refreshRate']*1e-4:.3f}" for syncTopo in syncTopos]
        data['Fps'] = '\n'.join(refreshRates) if len(refreshRates) > 0 else self.DataMissingStr

        # Build House Sync
        house_fpss = [syncTopo['syncStatusParams']['houseSyncIncoming']*1e-4 for syncTopo in syncTopos]
        house_syncs = [syncTopo['syncStatusParams']['bHouseSync'] for syncTopo in syncTopos]
        house_sync_fpss = list(map(lambda x: f"{x[1]:.3f}" if x[0] else 'no', zip(house_syncs, house_fpss)))
        data['HouseSync'] = '\n'.join(house_sync_fpss) if len(house_sync_fpss) > 0 else self.DataMissingStr

        # Build Sync Source
        source_str = {0:'Vsync', 1:'House'}
        sync_sources = [syncTopo['syncControlParams']['source'] for syncTopo in syncTopos]
        sync_sources = [source_str.get(sync_source, 'Unknown') for sync_source in sync_sources]
        bInternalSlaves = [syncTopo['syncStatusParams']['bInternalSlave'] for syncTopo in syncTopos]

        sync_slaves = []

        for i in range(len(sync_sources)):
            if bInternalSlaves[i] and sync_sources[i] == 'Vsync':
                sync_slaves.append('Vsync(daisy)')
            else:
                sync_slaves.append(sync_sources[i])

        data['SyncSource'] = '\n'.join(sync_slaves) if len(sync_slaves) > 0 else self.DataMissingStr

        # Mosaic Topology
        mosaicTopos = syncStatus['mosaicTopos']

        mosaicTopoLines = []

        for mosaicTopo in mosaicTopos:
            displaySettings = mosaicTopo['displaySettings']
            width_per_display = displaySettings['width']
            height_per_display = displaySettings['height']

            width = mosaicTopo['columns'] * width_per_display
            height = mosaicTopo['rows'] * height_per_display

            # Ignoring displaySettings['freq'] because it seems to be fixed and ignores sync frequency.
            line = f"{width}x{height} {displaySettings['bpp']}bpp"
            mosaicTopoLines.append(line)

        data['Mosaics'] = '\n'.join(mosaicTopoLines)

        # Build PresentMode.
        flip_history = syncStatus['flipModeHistory']

        if len(flip_history) > 0:
            data['PresentMode'] = flip_history[-1]

        # Detect PresentMode glitches
        if len(set(flip_history)) > 1:
            data['PresentMode'] = 'GLITCH!'
            data['TimeLastFlipGlitch'] = time.time()

        # Write time since last glitch
        if data['PresentMode'] != self.DataMissingStr:
            time_since_flip_glitch = time.time() - data['TimeLastFlipGlitch']

            # Let the user know for 1 minute that there was a glitch in the flip mode
            if time_since_flip_glitch < 1*60:
                data['PresentMode'] = data['PresentMode'].split('\n')[0] + '\n' + str(int(time_since_flip_glitch))

        # Window in focus or not
        data['InFocus'] = 'no'
        for prog in device.program_start_queue.running_programs_named('unreal'):
            if prog.pid and prog.pid == syncStatus['pidInFocus']:
                data['InFocus'] = 'yes'
                break

        # Show Exe flags (like Disable Fullscreen Optimization)
        data['ExeFlags'] = '\n'.join([layer for layer in syncStatus['programLayers'][1:]])

        # Driver version
        try:
            driver = syncStatus['driverVersion']
            data['Driver'] = f'{int(driver/100)}.{driver % 100}'
        except (KeyError, TypeError):
            data['Driver'] = self.DataMissingStr

        # Taskbar visibility
        data['Taskbar'] = syncStatus.get('taskbar', self.DataMissingStr)

        # Operating system version
        data['OsVer'] = self.friendly_osver(device)

        # CPU utilization
        try:
            num_cores = len(syncStatus['cpuUtilization'])
            num_overloaded_cores = 0
            cpu_load_avg = 0.0
            for core_load in syncStatus['cpuUtilization']:
                cpu_load_avg += float(core_load) * (1.0 / num_cores)
                if core_load > self.CoreOverloadThresh:
                    num_overloaded_cores += 1

            data['CpuUtilization'] = f"{cpu_load_avg:.0f}%"
            if num_overloaded_cores > 0:
                data['CpuUtilization'] += f" ({num_overloaded_cores} cores > {self.CoreOverloadThresh}%)"
        except (KeyError, ValueError):
            data['CpuUtilization'] = self.DataMissingStr

        # Memory utilization
        try:
            gb = 1024 * 1024 * 1024
            mem_utilized = device.total_phys_mem - syncStatus.get('availablePhysicalMemory', 0)
            data['MemUtilization'] = f"{mem_utilized / gb:.1f} / {device.total_phys_mem / gb:.0f} GB"
        except TypeError:
            data['MemUtilization'] = self.DataMissingStr

        # GPU utilization + clocks
        try:
            gpu_stats = list(map(lambda x: f"#{x[0]}: {x[1]:.0f}% ({x[2] / 1000:.0f} MHz)", zip(count(), syncStatus['gpuUtilization'], syncStatus['gpuCoreClocksKhz'])))
            data['GpuUtilization'] = '\n'.join(gpu_stats) if len(gpu_stats) > 0 else self.DataMissingStr
        except (KeyError, TypeError):
            data['GpuUtilization'] = self.DataMissingStr

        # GPU temperature
        try:
            temps = [t if t != -2147483648 else self.DataMissingStr for t in syncStatus['gpuTemperature']]
            data['GpuTemperature'] = '\n'.join(map(lambda x: f"#{x[0]}: {x[1]}° C", zip(count(), temps))) if len(temps) > 0 else self.DataMissingStr
        except (KeyError, TypeError):
            data['GpuTemperature'] = self.DataMissingStr


    def on_get_sync_status(self, device, message):
        ''' Called when the listener has sent a message with the sync status '''
        # check ack
        try:
            if message['bAck'] == False:
                return
        except KeyError:
            LOGGER.error(f"Error parsing 'get sync status' looking for 'bAck' flag")
            return

        # ok, we expect this to be a valid message, let's parse and update the model.
        deviceIdx, devicedata = self.devicedata_from_device(device)
        devicedata['time_last_update'] = time.time()
        devicedata['stale'] = False

        try:
            self.populate_sync_data(devicedata=devicedata, message=message)
        except (KeyError, ValueError):
            LOGGER.error(f"Error parsing 'get sync status' message and populating model data\n\n=== Traceback BEGIN ===\n{traceback.format_exc()}=== Traceback END ===\n")
            return

        row = deviceIdx + 1
        self.dataChanged.emit(self.createIndex(row, 1), self.createIndex(row, len(self.colnames)))


    def do_console_exec(self, exec_str, executor=''):
        ''' Issues a console exec to the cluster '''
        devices = [devicedata['device'] for devicedata in self.devicedatas.values()]
        if len(devices):
            try:
                devices[0].__class__.console_exec_cluster(devices, exec_str, executor)
                self.console_exec_issued.emit()
            except:
                LOGGER.warning("Could not issue console exec")


    #~ QAbstractTableModel interface begin

    def rowCount(self, parent=QModelIndex()):
        return len(self.devicedatas)

    def columnCount(self, parent=QModelIndex()):
        return len(self.colnames)

    def headerData(self, section, orientation, role):
        if role == Qt.DisplayRole:
            if orientation == Qt.Horizontal:
                return self.colnames[section]
            else:
                return "{}".format(section)

        if role == Qt.ToolTipRole:
            return self.tooltips[section]

        return None

    def data(self, index, role=Qt.DisplayRole):
        column = index.column()
        row = index.row()

        # get column name
        colname = self.colnames[column]

        # grab device data from ordered dict
        _, devicedata = list(self.devicedatas.items())[row] # returns key, value. Where key is the device_hash.
        data = devicedata['data']
        value = data[colname]

        if role == Qt.DisplayRole:
            return value

        elif role == Qt.BackgroundRole:
            return self.color_for_column(colname=colname, value=value, data=data)

        elif role == Qt.TextAlignmentRole:
            if colname in ('CpuUtilization', 'GpuUtilization'):
                return Qt.AlignLeft
            return Qt.AlignRight

        return None

    @QtCore.Slot()
    def btnFixExeFlags_clicked(self):
        ''' Tries to force the correct UE4Editor.exe flags '''
        for devicedata in self.devicedatas.values():
            device = devicedata['device']
            data = devicedata['data']

            good_string = 'DISABLEDXMAXIMIZEDWINDOWEDMODE'

            if good_string not in data['ExeFlags']:
                device.fix_exe_flags()

    @QtCore.Slot()
    def btnSoftKill_clicked(self):
        ''' Kills the cluster by sending a message to the master. '''
        devices = [devicedata['device'] for devicedata in self.devicedatas.values()]
        if len(devices):
            try:
                devices[0].__class__.soft_kill_cluster(devices)
            except:
                LOGGER.warning("Could not soft kill cluster")

    #~ QAbstractTableModel interface end