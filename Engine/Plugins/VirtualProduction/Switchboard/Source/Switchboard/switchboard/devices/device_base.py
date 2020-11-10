# Copyright Epic Games, Inc. All Rights Reserved.
from switchboard import config_osc as osc
from switchboard import recording
from switchboard.config import CONFIG, Setting, SETTINGS
from switchboard.switchboard_logging import LOGGER
from switchboard import message_protocol

from PySide2 import QtCore, QtGui
import pythonosc.udp_client

from enum import IntEnum, unique, auto
import os
import random
import socket

@unique
class DeviceStatus(IntEnum):
    DELETE = auto()
    DISCONNECTED = auto()
    CLOSED = auto()
    SYNCING = auto()
    BUILDING = auto()
    OPEN = auto()
    READY = auto()
    RECORDING = auto()


class DeviceQtHandler(QtCore.QObject):
    signal_device_status_changed = QtCore.Signal(object, int)
    signal_device_connect_failed = QtCore.Signal(object)
    signal_device_client_disconnected = QtCore.Signal(object)
    signal_device_project_changelist_changed = QtCore.Signal(object)
    signal_device_engine_changelist_changed = QtCore.Signal(object)
    signal_device_sync_failed = QtCore.Signal(object)
    signal_device_is_recording_device_changed = QtCore.Signal(object, int)
    signal_device_build_update = QtCore.Signal(object, str, str) # device, step, percent
    signal_device_sync_update = QtCore.Signal(object, int) # device, progress


class Device(QtCore.QObject):

    add_device_dialog = None # falls back to the default AddDeviceDialog as specified in device_widget_base

    csettings = {
        'is_recording_device': Setting(
            attr_name="is_recording_device", 
            nice_name='Is Recording Device', 
            value=True, 
            tool_tip=f'Is this device used to record',
        )
    }

    def __init__(self, name, ip_address, **kwargs):
        super().__init__()

        self._name = name  # Assigned name
        self.device_qt_handler = DeviceQtHandler()

        self.setting_ip_address = Setting("ip_address", 'IP address', ip_address)

        # override any setting that was passed via kwargs
        for setting in self.setting_overrides():
            if setting.attr_name in kwargs.keys():
                override = kwargs[setting.attr_name]
                setting.override_value(self.name, override)

        self._project_changelist = None
        self._engine_changelist = None

        self._status = DeviceStatus.DISCONNECTED

        # If the device should autoconnect on startup
        self.auto_connect = True

        self.device_hash = random.getrandbits(64) # TODO: This is not really a hash. Could be changed to real hash based on e.g. ip and name.

        # Lazily create the OSC client as needed
        self.osc_client = None

        self.device_recording = None

        self.widget = None

    def init(self, widget_class, icons):
        self.widget = widget_class(self.name, self.device_hash, self.ip_address, icons)
        self.widget.signal_device_name_changed.connect(self.on_device_name_changed)
        self.widget.signal_ip_address_changed.connect(self.on_ip_address_changed)
        self.setting_ip_address.signal_setting_changed.connect(lambda _, new_ip, widget=self.widget: widget.on_ip_address_changed(new_ip))

        # let the CONFIG class know what settings/properties this device has so it can save the config file on changes.
        CONFIG.register_device_settings(self.device_type, self.name, self.device_settings(), self.setting_overrides())

    def on_device_name_changed(self, new_name):
        if self.name != new_name:
            old_name = self.name
            self.name = new_name
            CONFIG.on_device_name_changed(old_name, new_name)

    def on_ip_address_changed(self, new_ip):
        if self.ip_address != new_ip:
            self.ip_address = new_ip

    @property
    def device_type(self):
        return self.__class__.__name__.split("Device", 1)[1]

    @classmethod
    def load_plugin_icons(cls):
        plugin_name = cls.__name__.split("Device", 1)[1]
        plugin_dir = plugin_name.lower()
        icon_dir = os.path.join(os.path.dirname(__file__), plugin_dir, "icons")
        icons = {}
        for _, _, files in os.walk(icon_dir):
            for fname in files:
                state, _ = os.path.splitext(fname)
                icon_path = os.path.join(icon_dir, fname)
                icon = icons[state] = QtGui.QIcon()
                icon.addFile(icon_path)
        return icons

    @classmethod
    def reset_csettings(cls):
        for csetting in cls.csettings.values():
            csetting.reset()

    @classmethod
    def plugin_settings(cls):
        # settings that are shared by all devices of a plugin
        return list(cls.csettings.values())

    def device_settings(self):
        # settings that are specific to an instance of the device
        return [self.setting_ip_address]

    def setting_overrides(self):
        # all settings that a device may override. these might be project or plugin settings.
        return []

    @property
    def name(self):
        return self._name

    @name.setter
    def name(self, value):
        if self._name == value:
            return
        self._name = value

    @property
    def category_name(self):
        return self.device_type

    @property
    def is_recording_device(self):
        return Device.csettings['is_recording_device'].get_value(self.name)

    @is_recording_device.setter
    def is_recording_device(self, value):
        Device.csettings['is_recording_device'].update_value(value)
        self.device_qt_handler.signal_device_is_recording_device_changed.emit(self, value)

    @property
    def ip_address(self):
        return self.setting_ip_address.get_value()

    @ip_address.setter
    def ip_address(self, value):
        self.setting_ip_address.update_value(value)
        # todo-dara: probably better to have the osc client connect to a change of the ip address.
        self.setup_osc_client(CONFIG.OSC_CLIENT_PORT.get_value())

    @property
    def status(self):
        return self._status

    @status.setter
    def status(self, value):
        previous_status = self._status
        self._status = value
        self.device_qt_handler.signal_device_status_changed.emit(self, previous_status)

    @property
    def project_changelist(self):
        return self._project_changelist

    @project_changelist.setter
    def project_changelist(self, value):
        self._project_changelist = value
        self.device_qt_handler.signal_device_project_changelist_changed.emit(self)

    @property
    def engine_changelist(self):
        return self._engine_changelist

    @engine_changelist.setter
    def engine_changelist(self, value):
        self._engine_changelist = value
        self.device_qt_handler.signal_device_engine_changelist_changed.emit(self)

    def set_slate(self, value):
        pass

    def set_take(self, value):
        pass

    def connect_listener(self):
        # If the device was disconnected, set to closed
        if self.status == DeviceStatus.DISCONNECTED:
            self.status = DeviceStatus.CLOSED

    def disconnect_listener(self):
        self.status = DeviceStatus.DISCONNECTED

    def setup_osc_client(self, osc_port):
        self.osc_client = pythonosc.udp_client.SimpleUDPClient(self.ip_address, osc_port)

    def send_osc_message(self, command, value, log=True):
        if not self.osc_client:
            self.setup_osc_client(CONFIG.OSC_CLIENT_PORT.get_value())

        if log:
            LOGGER.osc(f'OSC Server: Sending {command} {value} to {self.name} ({self.ip_address})')

        try:
            self.osc_client.send_message(command, value)
        except socket.gaierror as e:
            LOGGER.error(f'{self.name}: Incorrect ip address when sending OSC message. {e}')

    def record_start(self, slate, take, description):
        if self.status == DeviceStatus.DISCONNECTED or not self.is_recording_device:
            return

        self.send_osc_message(osc.RECORD_START, [slate, take, description])

    def record_stop(self):
        if self.status != DeviceStatus.RECORDING or not self.is_recording_device:
            return

        self.send_osc_message(osc.RECORD_STOP, 1)

    def record_start_confirm(self, timecode):
        self.device_recording = self.new_device_recording(self.name, self.device_type, timecode)
        self.status = DeviceStatus.RECORDING

    def record_stop_confirm(self, timecode, paths=None):
        if self.device_recording:
            self.device_recording.timecode_out = timecode
            if paths:
                self.device_recording.paths = paths

        self.status = DeviceStatus.READY

    def get_device_recording(self):
        return self.device_recording

    def transport_paths(self, device_recording):
        """
        Return the transport paths for the passed in device_recording
        This is used to create TransportJobs. If the device does not specify which paths
        it should transport, then no jobs will be moved to the transport path
        """
        return device_recording.paths

    def transport_file(self, device_path, output_dir):
        pass

    def process_file(self, device_path, output_path):
        pass

    @staticmethod
    def new_device_recording(device_name, device_type, timecode_in):
        """
        Create a new DeviceRecording
        """
        device_recording = recording.DeviceRecording()
        device_recording.device_name = device_name
        device_recording.device_type = device_type
        device_recording.timecode_in = timecode_in

        return device_recording

    @classmethod
    def plug_into_ui(cls, menubar, tabs):
        ''' Plugins can take this opportunity to add their own options to the UI
        '''
        pass

    @classmethod
    def added_device(cls, device):
        ''' DeviceManager informing that this plugin device has been created and added.
        '''
        pass

    @classmethod
    def removed_device(cls, device):
        ''' DeviceManager informing that this plugin device has been removed.
        '''
        pass
