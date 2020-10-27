# Copyright Epic Games, Inc. All Rights Reserved.
import getpass
import os
import os.path
import json
import socket
from .switchboard_logging import LOGGER
import switchboard.switchboard_utils as sb_utils
import threading
#from . import p4_utils
import shutil
import fnmatch

from PySide2 import QtCore

CONFIG_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', "configs"))
DEFAULT_MAP_TEXT = '-- Default Map --'

class Setting(QtCore.QObject):
    signal_setting_changed = QtCore.Signal(object, object)
    signal_setting_overriden = QtCore.Signal(str, object, object)

    """
    Allows Device to return paramters that are meant to be set in the settings menu
    """
    def __init__(self, attr_name, nice_name, value, possible_values=[], placholder_text=None, tool_tip=None, show_ui=True):
        super().__init__()

        self.attr_name = attr_name
        self.nice_name = nice_name
        self._original_value = self._value = value
        self.possible_values = possible_values
        # todo-dara: overrides are identified by device name right now. this should be changed to the hash instead.
        # that way we could avoid having to patch the overrides and settings in CONFIG when a device is renamed.
        self._overrides = {}
        self.placholder_text = placholder_text
        self.tool_tip = tool_tip
        self.show_ui = show_ui

    def is_overriden(self, device_name):
        try:
            return self._overrides[device_name] != self._value
        except KeyError:
            return False

    def remove_override(self, device_name):
        self._overrides.pop(device_name, None)

    def update_value(self, new_value):
        if self._value == new_value:
            return

        old_value = self._value
        self._value = new_value

        self.signal_setting_changed.emit(old_value, self._value)

    def override_value(self, device_name, override):
        if device_name in self._overrides and self._overrides[device_name] == override:
            return
        self._overrides[device_name] = override
        self.signal_setting_overriden.emit(device_name, self._value, override)

    def get_value(self, device_name=None):
        try:
            return self._overrides[device_name]
        except KeyError:
            return self._value

    def on_device_name_changed(self, old_name, new_name):
        if old_name in self._overrides.keys():
            self._overrides[new_name] = self._overrides.pop(old_name)

    def reset(self):
        self._value = self._original_value
        self._overrides = {}


# Store engine path, uproject path

class Config(object):

    saving_allowed = True
    saving_allowed_fifo = []

    def push_saving_allowed(self, value):
        ''' Sets a new state of saving allowed, but pushes current to the stack
        '''
        self.saving_allowed_fifo.append(self.saving_allowed)
        self.saving_allowed = value

    def pop_saving_allowed(self):
        ''' Restores saving_allowed flag from the stack
        '''
        self.saving_allowed = self.saving_allowed_fifo.pop()

    def __init__(self, file_name):
        self.init_with_file_name(file_name)

    def init_new_config(self, project_name, uproject, engine_dir, p4_settings):
        self.PROJECT_NAME = project_name
        self.UPROJECT_PATH = Setting("uproject", "uProject Path", uproject, tool_tip="Path to uProject")
        self.SWITCHBOARD_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../'))
        self.ENGINE_DIR = Setting("engine_dir", "Engine Directory", engine_dir, tool_tip="Path to UE4 engine directory")
        self.BUILD_ENGINE = Setting("build_engine", "Build Engine", False, tool_tip="Is Engine built from source?")
        self.MAPS_PATH = Setting("maps_path", "Map Path", "", tool_tip="Relative path from Content folder that contains maps to launch into.")
        self.MAPS_FILTER = Setting("maps_filter", "Map Filter", "*.umap", tool_tip="Walk every file in the Map Path and run a fnmatch to filter the file names")
        self.P4_ENABLED = Setting("p4_enabled", "Perforce Enabled", p4_settings['p4_enabled'], tool_tip="Toggle Perforce support for the entire application")
        self.SOURCE_CONTROL_WORKSPACE = Setting("source_control_workspace", "Workspace Name", p4_settings['p4_workspace_name'], tool_tip="SourceControl Workspace/Branch")
        self.P4_PATH = Setting("p4_sync_path", "Perforce Project Path", p4_settings['p4_project_path'])
        self.CURRENT_LEVEL = DEFAULT_MAP_TEXT

        self.OSC_SERVER_PORT = Setting("osc_server_port", "OSC Server Port", 6000)
        self.OSC_CLIENT_PORT = Setting("osc_client_port", "OSC Client Port", 8000)

        # MU Settings
        self.MULTIUSER_SERVER_EXE = 'UnrealMultiUserServer.exe'
        self.MUSERVER_COMMAND_LINE_ARGUMENTS = ""
        self.MUSERVER_SERVER_NAME = f'{self.PROJECT_NAME}_MU_Server'
        self.MUSERVER_AUTO_LAUNCH = True
        self.MUSERVER_AUTO_JOIN = True
        self.MUSERVER_CLEAN_HISTORY = True

        self._device_data_from_config = {}
        self._plugin_data_from_config = {}
        self._plugin_settings = {}
        self._device_settings = {}
        
        self.file_path = os.path.normpath(os.path.join(CONFIG_DIR, self.name_to_config_file_name(project_name, unique=True)))
        file_name = os.path.basename(self.file_path)
        SETTINGS.CONFIG = file_name
        LOGGER.info(f"Creating new config saved in {SETTINGS.CONFIG}")
        SETTINGS.save()
        CONFIG.save()

    def init_with_file_name(self, file_name):

        if file_name:
            self.file_path = os.path.normpath(os.path.join(CONFIG_DIR, file_name))

            # Read the json config file
            try:
                with open(self.file_path) as f:
                    LOGGER.debug(f'Loading Config {self.file_path}')
                    data = json.load(f)
            except FileNotFoundError as e:
                LOGGER.error(f'Config: {e}')
                self.file_path = None
                data = {}
        else:
            self.file_path = None
            data = {}

        project_settings = []

        self.PROJECT_NAME = data.get('project_name', 'Default')
        self.UPROJECT_PATH = Setting("uproject", "uProject Path", data.get('uproject', ''), tool_tip="Path to uProject")
        project_settings.append(self.UPROJECT_PATH)

        # Directory Paths
        self.SWITCHBOARD_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../'))
        self.ENGINE_DIR = Setting("engine_dir", "Engine Directory", data.get('engine_dir', ''), tool_tip="Path to UE4 engine directory")
        project_settings.append(self.ENGINE_DIR)
        self.BUILD_ENGINE = Setting("build_engine", "Build Engine", data.get('build_engine', False), tool_tip="Is Engine built from source?")
        project_settings.append(self.BUILD_ENGINE)
        self.MAPS_PATH = Setting("maps_path", "Map Path", data.get('maps_path', ''), placholder_text="Maps", tool_tip="Relative path from Content folder that contains maps to launch into.")
        project_settings.append(self.MAPS_PATH)
        self.MAPS_FILTER = Setting("maps_filter", "Map Filter", data.get('maps_filter', '*.umap'), placholder_text="*.umap", tool_tip="Walk every file in the Map Path and run a fnmatch to filter the file names")
        project_settings.append(self.MAPS_FILTER)

        # OSC settings
        self.OSC_SERVER_PORT = Setting("osc_server_port", "OSC Server Port", data.get('osc_server_port', 6000))
        self.OSC_CLIENT_PORT = Setting("osc_client_port", "OSC Client Port", data.get('osc_client_port', 8000))
        project_settings.extend([self.OSC_SERVER_PORT, self.OSC_CLIENT_PORT])

        # Perforce settings
        self.P4_ENABLED = Setting("p4_enabled", "Perforce Enabled", data.get("p4_enabled", False), tool_tip="Toggle Perforce support for the entire application")
        self.SOURCE_CONTROL_WORKSPACE = Setting("source_control_workspace", "Workspace Name", data.get("source_control_workspace"), tool_tip="SourceControl Workspace/Branch")
        self.P4_PATH = Setting("p4_sync_path", "Perforce Project Path", data.get("p4_sync_path", ''), placholder_text="//UE4/Project")
        project_settings.extend([self.P4_ENABLED, self.SOURCE_CONTROL_WORKSPACE, self.P4_PATH])

        # EXE names
        self.MULTIUSER_SERVER_EXE = data.get('multiuser_exe', 'UnrealMultiUserServer.exe')

        # MU Settings
        self.MUSERVER_COMMAND_LINE_ARGUMENTS = data.get('muserver_command_line_arguments', '')
        self.MUSERVER_SERVER_NAME = data.get('muserver_server_name', f'{self.PROJECT_NAME}_MU_Server')
        self.MUSERVER_AUTO_LAUNCH = data.get('muserver_auto_launch', True)
        self.MUSERVER_AUTO_JOIN = data.get('muserver_auto_join', True)
        self.MUSERVER_CLEAN_HISTORY = data.get('muserver_clean_history', True)

        # MISC SETTINGS
        self.CURRENT_LEVEL = data.get('current_level', DEFAULT_MAP_TEXT)

        # automatically save whenever a project setting is changed or overriden by a device
        for setting in project_settings:
            setting.signal_setting_changed.connect(lambda: self.save())
            setting.signal_setting_overriden.connect(self.on_device_override_changed)

        # Devices
        self._device_data_from_config = {}
        self._plugin_data_from_config = {}
        self._device_settings = {}
        self._plugin_settings = {}

        # Convert devices data from dict to list so they can be directly fed into the kwargs
        for device_type, devices in data.get('devices', {}).items():
            for device_name, data in devices.items():
                if device_name == "settings":
                    self._plugin_data_from_config[device_type] = data
                else:
                    ip_address = data["ip_address"]
                    device_data = {"name": device_name, "ip_address": ip_address}
                    device_data["kwargs"] = {k: v for (k,v) in data.items() if k != "ip_address"}
                    self._device_data_from_config.setdefault(device_type, []).append(device_data)

    def load_plugin_settings(self, device_type, settings):
        ''' Updates plugin settings values with those read from the config file.
        '''

        loaded_settings = self._plugin_data_from_config.get(device_type, [])

        if loaded_settings:
            for setting in settings:
                if setting.attr_name in loaded_settings:
                    setting.update_value(loaded_settings[setting.attr_name])
            del self._plugin_data_from_config[device_type]

    def register_plugin_settings(self, device_type, settings):

        self._plugin_settings[device_type] = settings

        for setting in settings:
            setting.signal_setting_changed.connect(lambda: self.save())
            setting.signal_setting_overriden.connect(self.on_device_override_changed)

    def register_device_settings(self, device_type, device_name, settings, overrides):
        self._device_settings[(device_type, device_name)] = (settings, overrides)

        for setting in settings:
            setting.signal_setting_changed.connect(lambda: self.save())

    def on_device_override_changed(self, device_name, old_value, override):
        # only do a save operation when the device is known (has called register_device_settings)
        # otherwise it is still loading and we want to avoid saving during device loading to avoid errors in the cfg file.
        known_devices = [name for (_, name) in self._device_settings.keys()]
        if device_name in known_devices:
            self.save()

    def is_writable(self):
        if not self.file_path:
            return False

        if os.path.exists(self.file_path) and os.path.isfile(self.file_path):
            return os.access(self.file_path, os.W_OK)

        return False

    def rename(self, new_config_name):
        """
        Move the file
        """
        new_file_path = os.path.normpath(os.path.join(CONFIG_DIR, new_config_name))

        if self.file_path:
            shutil.move(self.file_path, new_file_path)

        self.file_path = new_file_path
        self.save()

    def save(self):

        if not self.saving_allowed:
            return

        data = {}

        # General settings
        #
        data['project_name'] = self.PROJECT_NAME
        data['uproject'] = self.UPROJECT_PATH.get_value()
        data['engine_dir'] = self.ENGINE_DIR.get_value()
        data['build_engine'] = self.BUILD_ENGINE.get_value()
        data["maps_path"] = self.MAPS_PATH.get_value()
        data["maps_filter"] = self.MAPS_FILTER.get_value()
        
		# OSC settings
		#
        data["osc_server_port"] = self.OSC_SERVER_PORT.get_value()
        data["osc_client_port"] = self.OSC_CLIENT_PORT.get_value()

        # Source Control Settings
        #
        data["p4_enabled"] = self.P4_ENABLED.get_value()
        data["p4_sync_path"] = self.P4_PATH.get_value()
        data["source_control_workspace"] = self.SOURCE_CONTROL_WORKSPACE.get_value()
        
        # MU Settings
        #
        data["multiuser_exe"] = self.MULTIUSER_SERVER_EXE
        data["muserver_command_line_arguments"] = self.MUSERVER_COMMAND_LINE_ARGUMENTS
        data["muserver_server_name"] = self.MUSERVER_SERVER_NAME
        data["muserver_auto_launch"] = self.MUSERVER_AUTO_LAUNCH
        data["muserver_auto_join"] = self.MUSERVER_AUTO_JOIN
        data["muserver_clean_history"] = self.MUSERVER_CLEAN_HISTORY

        # Current Level
        #
        data["current_level"] = self.CURRENT_LEVEL

        # Devices
        #
        data["devices"] = {}

        # Plugin settings
        for device_type, plugin_settings in self._plugin_settings.items():

            if not plugin_settings:
                continue

            settings = {}

            for setting in plugin_settings:
                settings[setting.attr_name] = setting.get_value()

            data["devices"][device_type] = {
                "settings" : settings,
            }

        # Device settings
        for (device_type, device_name), (settings, overrides) in self._device_settings.items():

            if not device_type in data["devices"].keys():
                data["devices"][device_type] = {}

            serialized_settings = {}

            for setting in settings:
                serialized_settings[setting.attr_name] = setting.get_value()

            for setting in overrides:
                if setting.is_overriden(device_name):
                    serialized_settings[setting.attr_name] = setting.get_value(device_name)

            data["devices"][device_type][device_name] = serialized_settings

        # Save to file
        #
        with open(f'{self.file_path}', 'w') as f:
            json.dump(data, f, indent=4)
            LOGGER.debug(f'Config File: {self.file_path} updated')

    def on_device_name_changed(self, old_name, new_name):
        old_key = None

        # update the entry in device_settings as they are identified by name
        for (device_type, device_name), (_, overrides) in self._device_settings.items():
            if device_name == old_name:
                old_key = (device_type, old_name)
                # we also need to patch the overrides for the same reason
                for setting in overrides:
                    setting.on_device_name_changed(old_name, new_name)
                break

        new_key = (old_key[0], new_name)
        self._device_settings[new_key] = self._device_settings.pop(old_key)

        self.save()

    def on_device_removed(self, _, device_type, device_name, update_config):

        if not update_config:
            return

        del self._device_settings[(device_type, device_name)]
        self.save()

    def maps(self):
        maps_path = os.path.normpath(os.path.join(os.path.dirname(self.UPROJECT_PATH.get_value()), 'Content', self.MAPS_PATH.get_value()))

        maps = []
        for _, _, files in os.walk(maps_path):
            for name in files:
                if not fnmatch.fnmatch(name, self.MAPS_FILTER.get_value()):
                    continue

                rootname, _ = os.path.splitext(name)
                if rootname not in maps:
                    maps.append(rootname)

        maps.sort()
        return maps

    def multiuser_server_path(self):
        return os.path.normpath(os.path.join(self.ENGINE_DIR.get_value(), 'Binaries/Win64', self.MULTIUSER_SERVER_EXE))

    # todo-dara: find a way to do this directly in the LiveLinkFace plugin code
    def unreal_device_ip_addresses(self):
        unreal_ips = []
        for (device_type, device_name), (settings, overrides) in self._device_settings.items():
            if device_type == "Unreal":
                for setting in settings:
                    if setting.attr_name == "ip_address":
                        unreal_ips.append(setting.get_value(device_name))
        return unreal_ips

    @staticmethod
    def engine_path(engine_dir, engine_exe):
        return os.path.normpath(os.path.join(engine_dir, 'Binaries/Win64', engine_exe))

    @staticmethod
    def name_to_config_file_name(name, unique=False):
        """
        Given a name like My_Project return config_My_Project.json
        """
        if not unique:
            return f'config_{name}.json'

        i = 1
        file_name = f'config_{name}.json'
        file_path = os.path.normpath(os.path.join(CONFIG_DIR, file_name))
        while os.path.isfile(file_path):
            file_name = f'config_{name}_{i}.json'
            file_path = os.path.normpath(os.path.join(CONFIG_DIR, file_name))
            i+=1

        return file_name

    @staticmethod
    def config_file_name_to_name(file_name):
        """
        Given a file name like config_My_Project.json return My_Project
        """
        name = sb_utils.remove_prefix(file_name, 'config_')
        return os.path.splitext(name)[0]


class UserSettings(object):
    def __init__(self, file_name='user_settings.json'):
        self.file_path = os.path.normpath(os.path.join(CONFIG_DIR, file_name))

        try:
            with open(self.file_path) as f:
                LOGGER.debug(f'Loading Settings {self.file_path}')
                data = json.load(f)
        # Create a default user_settings
        except FileNotFoundError:
            data = {}
            LOGGER.debug(f'Creating default user settings')

        config_files = list_config_files()
        if config_files:
            config_files = config_files[0]
        else:
            config_files = ''

        self.CONFIG = data.get('config', config_files)
        # IP Address of the machine running Switchboard
        self.IP_ADDRESS = data.get('ip_address', socket.gethostbyname(socket.gethostname()))

        self.TRANSPORT_PATH = data.get('transport_path', '')

        # UI Settings
        self.MUSERVER_SESSION_NAME = data.get('muserver_session_name', f'MU_Session')
        self.CURRENT_SEQUENCE = data.get('current_sequence', 'Default')
        self.CURRENT_SLATE = data.get('current_slate', 'Scene')
        self.CURRENT_TAKE = data.get('current_take', 1)
        self.CURRENT_LEVEL = data.get('current_level', None)
        self.LAST_BROWSED_PATH = data.get('last_browsed_path', None)

        # Save so any new defaults are written out
        self.save()

    def save(self):
        data = {}
        data['config'] = self.CONFIG
        data['ip_address'] = self.IP_ADDRESS
        data['transport_path'] = self.TRANSPORT_PATH
        data["muserver_session_name"] = self.MUSERVER_SESSION_NAME
        data["current_sequence"] = self.CURRENT_SEQUENCE
        data["current_slate"] = self.CURRENT_SLATE
        data["current_take"] = self.CURRENT_TAKE
        data["current_level"] = self.CURRENT_LEVEL
        data["last_browsed_path"] = self.LAST_BROWSED_PATH

        with open(f'{self.file_path}', 'w') as f:
            json.dump(data, f, indent=4)


# Return a path to the user_settings.json file
def user_settings_file():
    return os.path.join(CONFIG_DIR, 'user_settings.json')


# Return all the config files in config_dir()
def list_config_files():
    os.makedirs(CONFIG_DIR, exist_ok=True)
    return [x for x in os.listdir(CONFIG_DIR) if x.endswith('.json') and x.startswith('config_')]


# Get the user settings and load their config
SETTINGS = UserSettings()
CONFIG = Config(SETTINGS.CONFIG)
