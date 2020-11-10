# Copyright Epic Games, Inc. All Rights Reserved.

from switchboard import message_protocol
from switchboard.switchboard_logging import LOGGER

from PySide2 import QtCore, QtGui
from PySide2.QtCore import Qt, QTimer, QObject, QAbstractTableModel, QModelIndex
from PySide2.QtGui import QColor

from collections import OrderedDict
import time, json, traceback

class nDisplayMonitor(QAbstractTableModel):
    ''' This will monitor the status of the nDisplay nodes, in particular regarding sync.
    It polls the listener at the specified rate and the UI should update with this info.
    '''

    ColorWarning = QColor(0xcc, 0x6a, 0x1b)
    ColorNormal  = QColor(0x3d, 0x3d, 0x3d)

    def __init__(self, parent):
        QAbstractTableModel.__init__(self, parent)

        self.polling_period_ms = 1000
        self.devicedatas = OrderedDict() # ordered so that we can map row indices to devices

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.poll_sync_status)

        self.colnames = [
            'Node', 
            'Host', 
            'Connected',
            'Driver',
            'FlipMode', 
            'Gpus', 
            'Displays', 
            'Fps', 
            'HouseSync',
            'SyncSource',
            'Mosaics',
            'Taskbar',
            'ExeFlags',
        ]

    def color_for_column(self, colname, value, data):
        ''' Returns the background color for the given cell
        '''

        if data['Connected'].lower() == 'no':
            if colname == 'Connected':
                return self.ColorWarning
            return self.ColorNormal

        if colname == 'FlipMode':
            good_string = 'Hardware Composed: Independent Flip'
            return self.ColorNormal if good_string in value else self.ColorWarning

        if colname == 'Gpus':
            return self.ColorNormal if 'Synced' in value and 'Free' not in value else self.ColorWarning

        if colname == 'ExeFlags':
            good_string = 'DISABLEDXMAXIMIZEDWINDOWEDMODE'
            return self.ColorNormal if good_string in value else self.ColorWarning
        
        if colname == 'Displays':
            is_normal = ('Slave' in value or 'Master' in value) and 'Unsynced' not in value
            return self.ColorNormal if is_normal else self.ColorWarning

        return self.ColorNormal

    def reset_device_data(self, device, data):
        ''' Sets device data to unconnected state
        '''

        for colname in self.colnames:
            data[colname] = 'n/a'

        data['Host'] = str(device.ip_address)
        data['Node'] = device.name
        data['Connected'] = 'yes' if device.unreal_client.is_connected else 'no'

        # extra data not in columns
        data['TimeLastFlipGlitch'] = time.time()

    def added_device(self, device):
        ''' Called by the plugin when a new nDisplay device has been added.
        '''

        data = {}

        self.reset_device_data(device, data)
        self.devicedatas[device.device_hash] = {'device':device, 'data':data, 'time_last_update':0, 'stale':True }

        # notify the UI of the change
        self.layoutChanged.emit()

        # start/continue polling since there is at least one device
        if not self.timer.isActive():
            self.timer.start(self.polling_period_ms)

    def removed_device(self, device):
        ''' Called by the plugin when an nDisplay device has been removed.
        '''

        self.devicedatas.pop(device.device_hash)

        # notify the UI of the change
        self.layoutChanged.emit()

        # turn off the timer if there are no devices left
        if not self.devicedatas:
            self.timer.stop()

    def handle_stale_device(self, devicedata, deviceIdx):
        '''Detects if the device is stale, and resets the data if so as to not mislead the user
        '''

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
        ''' Detects if device connection changed and notifies the UI if a disconnection happens.
        '''

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
        ''' Polls sync status for all nDisplay devices
        '''

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
                program_id = device.programs_ids_with_name('unreal')[-1]
            except IndexError:
                program_id = '00000000-0000-0000-0000-000000000000'

            _, msg = message_protocol.create_get_sync_status_message(program_id)

            # send get sync status message
            device.unreal_client.send_message(msg)

    def devicedata_from_device(self, device):
        '''Retrieves the devicedata and index for given device
        '''
        for deviceIdx, hash_devicedata in enumerate(self.devicedatas.items()):
            device_hash, devicedata = hash_devicedata[0], hash_devicedata[1]
            if device_hash == device.device_hash:
                return (deviceIdx, devicedata)

        raise KeyError

    def populate_sync_data(self, data, message):
        ''' Populates model data with message contents, which comes from 'get sync data' command.
        '''

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

        data['Gpus'] = '\n'.join(Gpus)

        # Build Displays, informing which Display in each Sync group are in sync.

        Displays = []

        bpc_strings = {1:6, 2:8, 3:10, 4:12, 5:16}

        for syncTopo in syncTopos:
            display_sync_states = [f"{syncDisplay['syncState']}({bpc_strings.get(syncDisplay['bpc'], '??')}bpc)" for syncDisplay in syncTopo['syncDisplays']]
            Displays.append(', '.join(display_sync_states))

        data['Displays'] = '\n'.join(Displays)

        # Build Fps
        refreshRates = [f"{syncTopo['syncStatusParams']['refreshRate']*1e-4:.3f}" for syncTopo in syncTopos]
        data['Fps'] = '\n'.join(refreshRates)

        # Build House Sync
        house_fpss = [syncTopo['syncStatusParams']['houseSyncIncoming']*1e-4 for syncTopo in syncTopos]
        house_syncs = [syncTopo['syncStatusParams']['bHouseSync'] for syncTopo in syncTopos]
        house_sync_fpss = map(lambda x: f"{x[1]:.3f}" if x[0] else 'no', zip(house_syncs, house_fpss))
        data['HouseSync'] = '\n'.join(house_sync_fpss)

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

        data['SyncSource'] = '\n'.join(sync_slaves)

        #
        # Mosaic Topology
        #

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

        # Build FlipMode.
        #
        flip_history = syncStatus['flipModeHistory']

        if len(flip_history) > 0:
            data['FlipMode'] = flip_history[-1]

        # Detect FlipMode glitches
        if len(set(flip_history)) > 1:
            data['FlipMode'] = 'GLITCH!'
            data['TimeLastFlipGlitch'] = time.time()

        # Write time since last glitch
        if data['FlipMode'] != 'n/a':
            time_since_flip_glitch = time.time() - data['TimeLastFlipGlitch']

            # Let the user know for 1 minute that there was a glitch in the flip mode
            if time_since_flip_glitch < 1*60:
                data['FlipMode'] = data['FlipMode'].split('\n')[0] + '\n' + str(int(time_since_flip_glitch))

        # Show Exe flags (like Disable Fullscreen Optimization)
        data['ExeFlags'] = '\n'.join([layer for layer in syncStatus['programLayers'][1:]])

        # Driver version
        driver = syncStatus['driverVersion']
        data['Driver'] = f'{int(driver/100)}.{driver % 100}'

        # Taskbar visibility
        data['Taskbar'] = syncStatus['taskbar']

    def on_get_sync_status(self, device, message):
        ''' Called when the listener has sent a message with the sync status
        '''

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

        data = devicedata['data']

        try:
            self.populate_sync_data(data, message)
        except KeyError:
            LOGGER.error(f"Error parsing 'get sync status' message and populating model data\n\n=== Traceback BEGIN ===\n{traceback.format_exc()}=== Traceback END ===\n")
            return

        row = deviceIdx + 1
        self.dataChanged.emit(self.createIndex(row, 1), self.createIndex(row, len(self.colnames)))

    #~ QAbstractTableModel interface begin

    def rowCount(self, parent=QModelIndex()):
        return len(self.devicedatas)

    def columnCount(self, parent=QModelIndex()):
        return len(self.colnames)

    def headerData(self, section, orientation, role):

        if role != Qt.DisplayRole:
            return None

        if orientation == Qt.Horizontal:
            return self.colnames[section]
        else:
            return "{}".format(section)

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
            return Qt.AlignRight

        return None

    #~ QAbstractTableModel interface end