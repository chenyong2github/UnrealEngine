# Copyright Epic Games, Inc. All Rights Reserved.

import fnmatch
import json
import os
import pathlib
import socket
import sys
import typing

from PySide2 import QtCore
from PySide2 import QtGui

from switchboard.switchboard_logging import LOGGER

ROOT_CONFIGS_PATH = pathlib.Path(__file__).parent.with_name('configs')
CONFIG_SUFFIX = '.json'

USER_SETTINGS_FILE_PATH = ROOT_CONFIGS_PATH.joinpath('user_settings.json')

DEFAULT_MAP_TEXT = '-- Default Map --'


class Setting(QtCore.QObject):
    signal_setting_changed = QtCore.Signal(object, object)
    signal_setting_overriden = QtCore.Signal(str, object, object)

    """
    Allows Device to return paramters that are meant to be set in the settings menu

    Args:
        attr_name             (str): Internal name.
        nice_name             (str): Display name.
        value                      : The initial value of this setting.
        possible_values            : Possible values for this Setting. Useful with e.g. combo boxes.
        placholder_text       (str): Placeholder for this setting's value in the UI (if applicable)
        tool_tip              (str): Tooltip to show in the UI for this setting.
        show_ui              (bool): Determines if this Setting will be shown inthe Settings UI.
        filtervalueset_fn (function): This function will validate and modify the settings value being set. None is allowed.
    """
    def __init__(
        self,
        attr_name,
        nice_name,
        value,
        possible_values=[],
        placholder_text=None,
        tool_tip=None,
        show_ui=True,
        filtervalueset_fn=None
    ):
        super().__init__()

        self.filtervalueset_fn = filtervalueset_fn
        self.attr_name = attr_name
        self.nice_name = nice_name

        if self.filtervalueset_fn:
            value = self.filtervalueset_fn(value)

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
        if self.filtervalueset_fn:
            new_value = self.filtervalueset_fn(new_value)

        if self._value == new_value:
            return

        old_value = self._value
        self._value = new_value

        self.signal_setting_changed.emit(old_value, self._value)

    def override_value(self, device_name, override):
        if self.filtervalueset_fn:
            override = self.filtervalueset_fn(override)

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


class ConfigPathError(Exception):
    '''
    Base exception type for config path related errors.
    '''
    pass


class ConfigPathEmptyError(ConfigPathError):
    '''
    Exception type raised when an empty or all whitespace string is used as a
    config path.
    '''
    pass


class ConfigPathLocationError(ConfigPathError):
    '''
    Exception type raised when a config path is located outside of the root
    configs directory.
    '''
    pass


class ConfigPathIsUserSettingsError(ConfigPathError):
    '''
    Exception type raised when the user settings file path is used as a config
    path.
    '''
    pass


def get_absolute_config_path(
        config_path: typing.Union[str, pathlib.Path]) -> pathlib.Path:
    '''
    Returns the given string or path object as an absolute config path.

    The string/path is validated to ensure that:
      - It is not empty, or all whitespace
      - It ends with the config path suffix
      - If it is already absolute, that it is underneath the root configs path
      - It is not the same path as the user settings file path
    '''
    if isinstance(config_path, str):
        config_path = config_path.strip()
        if not config_path:
            raise ConfigPathEmptyError('Config path cannot be empty')

        config_path = pathlib.Path(config_path)

    # Manually add the suffix instead of using pathlib.Path.with_suffix().
    # For strings like "foo.bar", with_suffix() will first remove ".bar"
    # before adding the suffix, which we don't want it to do.
    if not config_path.name.endswith(CONFIG_SUFFIX):
        config_path = config_path.with_name(
            f'{config_path.name}{CONFIG_SUFFIX}')

    if config_path.is_absolute():
        # Paths that are already absolute must have the root configs path as a
        # parent path.
        # Python 3.9 introduced pathlib.Path.is_relative_to(), which would read
        # a bit nicer here.
        if ROOT_CONFIGS_PATH not in config_path.parents:
            raise ConfigPathLocationError(
                f'Config path "{config_path}" is not underneath the root '
                f'configs path "{ROOT_CONFIGS_PATH}"')
    else:
        # Relative paths can simply be made absolute.
        config_path = ROOT_CONFIGS_PATH.joinpath(config_path)

    if config_path.resolve() == USER_SETTINGS_FILE_PATH:
        raise ConfigPathIsUserSettingsError(
            'Config path cannot be the same as the user settings file '
            f'path "{USER_SETTINGS_FILE_PATH}"')

    return config_path


def get_relative_config_path(
        config_path: typing.Union[str, pathlib.Path]) -> pathlib.Path:
    '''
    Returns the given string or path object as a config path relative to the
    root configs path.

    An absolute path is generated first to perform all of the same validation
    as get_absolute_config_path() before the relative path is computed and
    returned.
    '''
    config_path = get_absolute_config_path(config_path)
    return config_path.relative_to(ROOT_CONFIGS_PATH)


class ConfigPathValidator(QtGui.QValidator):
    '''
    Validator to determine whether the input is an acceptable config file
    path.

    If the input is not acceptable, the state is returned as Intermediate
    rather than Invalid so as not to interfere with the user typing in the
    text field.
    '''

    def validate(self, input, pos):
        try:
            get_absolute_config_path(input)
        except Exception:
            return QtGui.QValidator.Intermediate

        return QtGui.QValidator.Acceptable


class Config(object):

    DEFAULT_CONFIG_PATH = ROOT_CONFIGS_PATH.joinpath(f'Default{CONFIG_SUFFIX}')

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

    def __init__(self, file_path: typing.Union[str, pathlib.Path]):
        self.init_with_file_path(file_path)

    @staticmethod
    def clean_p4_path(path):
        ''' Clean p4 path. e.g. strip and remove trailing '/'
        '''
        if not path:
            return ''

        path = path.strip()

        while len(path) and path[-1] == '/':
            path = path[:-1]

        return path

    def init_new_config(self, file_path: typing.Union[str, pathlib.Path],
                        uproject, engine_dir, p4_settings):
        ''' Initialize new configuration
        '''

        self.file_path = get_absolute_config_path(file_path)
        self.PROJECT_NAME = self.file_path.stem
        self.UPROJECT_PATH = Setting("uproject", "uProject Path", uproject, tool_tip="Path to uProject")
        self.SWITCHBOARD_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../'))
        self.ENGINE_DIR = Setting("engine_dir", "Engine Directory", engine_dir, tool_tip="Path to UE4 engine directory")
        self.BUILD_ENGINE = Setting("build_engine", "Build Engine", False, tool_tip="Is Engine built from source?")
        self.MAPS_PATH = Setting("maps_path", "Map Path", "", tool_tip="Relative path from Content folder that contains maps to launch into.")
        self.MAPS_FILTER = Setting("maps_filter", "Map Filter", "*.umap", tool_tip="Walk every file in the Map Path and run a fnmatch to filter the file names")
        self.P4_ENABLED = Setting("p4_enabled", "Perforce Enabled", p4_settings['p4_enabled'], tool_tip="Toggle Perforce support for the entire application")
        self.SOURCE_CONTROL_WORKSPACE = Setting("source_control_workspace", "Workspace Name", p4_settings['p4_workspace_name'], tool_tip="SourceControl Workspace/Branch")

        self.P4_PROJECT_PATH = Setting(
            attr_name="p4_sync_path",
            nice_name="Perforce Project Path",
            value=p4_settings['p4_project_path'],
            filtervalueset_fn=Config.clean_p4_path,
        )

        self.P4_ENGINE_PATH = Setting(
            attr_name="p4_engine_path",
            nice_name="Perforce Engine Path",
            value=p4_settings['p4_engine_path'],
            filtervalueset_fn=Config.clean_p4_path,
        )

        self.CURRENT_LEVEL = DEFAULT_MAP_TEXT

        self.OSC_SERVER_PORT = Setting("osc_server_port", "OSC Server Port", 6000)
        self.OSC_CLIENT_PORT = Setting("osc_client_port", "OSC Client Port", 8000)

        # MU Settings
        self.MULTIUSER_SERVER_EXE = 'UnrealMultiUserServer'
        self.MUSERVER_COMMAND_LINE_ARGUMENTS = ""
        self.MUSERVER_SERVER_NAME = f'{self.PROJECT_NAME}_MU_Server'
        self.MUSERVER_AUTO_LAUNCH = True
        self.MUSERVER_AUTO_JOIN = False
        self.MUSERVER_CLEAN_HISTORY = True
        self.MUSERVER_AUTO_BUILD = True

        self.LISTENER_EXE = 'SwitchboardListener'

        self._device_data_from_config = {}
        self._plugin_data_from_config = {}
        self._plugin_settings = {}
        self._device_settings = {}

        LOGGER.info(f"Creating new config saved in {self.file_path}")
        self.save()

        SETTINGS.CONFIG = self.file_path
        SETTINGS.save()

    def init_with_file_path(self, file_path: typing.Union[str, pathlib.Path]):
        if file_path:
            try:
                self.file_path = get_absolute_config_path(file_path)

                # Read the json config file
                with open(self.file_path) as f:
                    LOGGER.debug(f'Loading Config {self.file_path}')
                    data = json.load(f)
            except (ConfigPathError, FileNotFoundError) as e:
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

        self.P4_PROJECT_PATH = Setting(
            "p4_sync_path",
            "Perforce Project Path",
            data.get("p4_sync_path", ''),
            placholder_text="//UE4/Project",
            filtervalueset_fn=Config.clean_p4_path,
        )

        self.P4_ENGINE_PATH = Setting(
            "p4_engine_path",
            "Perforce Engine Path",
            data.get("p4_engine_path", ''),
            placholder_text="//UE4/Project/Engine",
            filtervalueset_fn=Config.clean_p4_path,
        )

        project_settings.extend([self.P4_ENABLED, self.SOURCE_CONTROL_WORKSPACE, self.P4_PROJECT_PATH, self.P4_ENGINE_PATH])

        # EXE names
        self.MULTIUSER_SERVER_EXE = data.get('multiuser_exe', 'UnrealMultiUserServer')
        self.LISTENER_EXE = data.get('listener_exe', 'SwitchboardListener')

        # MU Settings
        self.MUSERVER_COMMAND_LINE_ARGUMENTS = data.get('muserver_command_line_arguments', '')
        self.MUSERVER_SERVER_NAME = data.get('muserver_server_name', f'{self.PROJECT_NAME}_MU_Server')
        self.MUSERVER_AUTO_LAUNCH = data.get('muserver_auto_launch', True)
        self.MUSERVER_AUTO_JOIN = data.get('muserver_auto_join', False)
        self.MUSERVER_CLEAN_HISTORY = data.get('muserver_clean_history', True)
        self.MUSERVER_AUTO_BUILD = data.get('muserver_auto_build', True)

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

    def replace(self, new_config_path: typing.Union[str, pathlib.Path]):
        """
        Move the file.

        If a file already exists at the new path, it will be overwritten.
        """
        new_config_path = get_absolute_config_path(new_config_path)

        if self.file_path:
            new_config_path.parent.mkdir(parents=True, exist_ok=True)
            self.file_path.replace(new_config_path)

        self.file_path = new_config_path
        self.save()

    def save(self):
        if not self.file_path or not self.saving_allowed:
            return

        data = {}

        # General settings
        data['project_name'] = self.PROJECT_NAME
        data['uproject'] = self.UPROJECT_PATH.get_value()
        data['engine_dir'] = self.ENGINE_DIR.get_value()
        data['build_engine'] = self.BUILD_ENGINE.get_value()
        data["maps_path"] = self.MAPS_PATH.get_value()
        data["maps_filter"] = self.MAPS_FILTER.get_value()
        data["listener_exe"] = self.LISTENER_EXE

        # OSC settings
        data["osc_server_port"] = self.OSC_SERVER_PORT.get_value()
        data["osc_client_port"] = self.OSC_CLIENT_PORT.get_value()

        # Source Control Settings
        data["p4_enabled"] = self.P4_ENABLED.get_value()
        data["p4_sync_path"] = self.P4_PROJECT_PATH.get_value()
        data["p4_engine_path"] = self.P4_ENGINE_PATH.get_value()
        data["source_control_workspace"] = self.SOURCE_CONTROL_WORKSPACE.get_value()

        # MU Settings
        data["multiuser_exe"] = self.MULTIUSER_SERVER_EXE
        data["muserver_command_line_arguments"] = self.MUSERVER_COMMAND_LINE_ARGUMENTS
        data["muserver_server_name"] = self.MUSERVER_SERVER_NAME
        data["muserver_auto_launch"] = self.MUSERVER_AUTO_LAUNCH
        data["muserver_auto_join"] = self.MUSERVER_AUTO_JOIN
        data["muserver_clean_history"] = self.MUSERVER_CLEAN_HISTORY
        data["muserver_auto_build"] = self.MUSERVER_AUTO_BUILD

        # Current Level
        data["current_level"] = self.CURRENT_LEVEL

        # Devices
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
        self.file_path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.file_path, 'w') as f:
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
        maps_path = os.path.normpath(os.path.join(os.path.dirname(self.UPROJECT_PATH.get_value().replace('"','')), 'Content', self.MAPS_PATH.get_value()))

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
        return self.engine_exe_path(self.ENGINE_DIR.get_value(), self.MULTIUSER_SERVER_EXE)

    def listener_path(self):
        return self.engine_exe_path(self.ENGINE_DIR.get_value(), self.LISTENER_EXE)

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
    def engine_exe_path(engine_dir: str, exe_basename: str):
        ''' Returns platform-dependent path to the specified engine executable. '''
        exe_name = exe_basename
        platform_bin_subdir = ''

        if sys.platform.startswith('win'):
            platform_bin_subdir = 'Win64'
            platform_bin_path = os.path.normpath(os.path.join(engine_dir, 'Binaries', platform_bin_subdir))
            given_path = os.path.join(platform_bin_path, exe_basename)
            if os.path.exists(given_path):
                return given_path

            # Use %PATHEXT% to resolve executable extension ambiguity.
            pathexts = os.environ.get('PATHEXT', '.COM;.EXE;.BAT;.CMD').split(';')
            for ext in pathexts:
                testpath = os.path.join(platform_bin_path, f'{exe_basename}{ext}')
                if os.path.isfile(testpath):
                    return testpath

            # Fallback despite non-existence.
            return given_path
        else:
            if sys.platform.startswith('linux'):
                platform_bin_subdir = 'Linux'
            elif sys.platform.startswith('darwin'):
                platform_bin_subdir = 'Mac'

            return os.path.normpath(os.path.join(engine_dir, 'Binaries', platform_bin_subdir, exe_name))


class UserSettings(object):

    def __init__(self):
        try:
            with open(USER_SETTINGS_FILE_PATH) as f:
                LOGGER.debug(f'Loading Settings {USER_SETTINGS_FILE_PATH}')
                data = json.load(f)
        except FileNotFoundError:
            # Create a default user_settings
            data = {}
            LOGGER.debug('Creating default user settings')

        self.CONFIG = data.get('config')
        if self.CONFIG:
            try:
                self.CONFIG = get_absolute_config_path(self.CONFIG)
            except ConfigPathError as e:
                LOGGER.error(e)
                self.CONFIG = None

        if not self.CONFIG:
            config_paths = list_config_paths()
            self.CONFIG = config_paths[0] if config_paths else None

        # IP Address of the machine running Switchboard
        self.IP_ADDRESS = data.get('ip_address', socket.gethostbyname(socket.gethostname()))

        self.TRANSPORT_PATH = data.get('transport_path', '')

        # UI Settings
        self.MUSERVER_SESSION_NAME = data.get('muserver_session_name', 'MU_Session')
        self.CURRENT_SEQUENCE = data.get('current_sequence', 'Default')
        self.CURRENT_SLATE = data.get('current_slate', 'Scene')
        self.CURRENT_TAKE = data.get('current_take', 1)
        self.CURRENT_LEVEL = data.get('current_level', None)
        self.LAST_BROWSED_PATH = data.get('last_browsed_path', None)

        # Save so any new defaults are written out
        self.save()

    def save(self):
        data = {
            'config': '',
            'ip_address': self.IP_ADDRESS,
            'transport_path': self.TRANSPORT_PATH,
            'muserver_session_name': self.MUSERVER_SESSION_NAME,
            'current_sequence': self.CURRENT_SEQUENCE,
            'current_slate': self.CURRENT_SLATE,
            'current_take': self.CURRENT_TAKE,
            'current_level': self.CURRENT_LEVEL,
            'last_browsed_path': self.LAST_BROWSED_PATH,
        }

        if self.CONFIG:
            try:
                data['config'] = str(get_relative_config_path(self.CONFIG))
            except ConfigPathError as e:
                LOGGER.error(e)

        with open(USER_SETTINGS_FILE_PATH, 'w') as f:
            json.dump(data, f, indent=4)


def list_config_paths() -> typing.List[pathlib.Path]:
    '''
    Returns a list of absolute paths to all config files in the configs dir.
    '''
    ROOT_CONFIGS_PATH.mkdir(parents=True, exist_ok=True)

    # Find all JSON files in the config dir recursively, but exclude the user
    # settings file.
    config_paths = [path for path in ROOT_CONFIGS_PATH.rglob(f'*{CONFIG_SUFFIX}')
                    if path != USER_SETTINGS_FILE_PATH]

    return config_paths


# Get the user settings and load their config
SETTINGS = UserSettings()
CONFIG = Config(SETTINGS.CONFIG)
