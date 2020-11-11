# Copyright Epic Games, Inc. All Rights Reserved.

from switchboard import config_osc as osc
from switchboard import message_protocol, p4_utils
from switchboard.config import CONFIG, Setting, SETTINGS, DEFAULT_MAP_TEXT
from switchboard.devices.device_base import Device, DeviceStatus, PluginHeaderWidgets
from switchboard.devices.device_widget_base import DeviceWidget
from switchboard.listener_client import ListenerClient
from switchboard.switchboard_logging import LOGGER

from PySide2 import QtWidgets, QtGui, QtCore

import os, sys, base64, uuid, threading
from pathlib import Path
from collections import OrderedDict

class ProgramStartQueueItem(object):
    ''' Item that holds the information for a program to start in the future
    '''
    def __init__(self, name, puuid_dependency, puuid, msg_to_unreal_client, pid=0, launch_fn=lambda:None):
        self.puuid_dependency = puuid_dependency         # so that it can check wait_for_previous_to_end against the correct program
        self.puuid = puuid                               # id of the program.
        self.msg_to_unreal_client = msg_to_unreal_client # command for listener/unreal_client
        self.name = name
        self.launch_fn = launch_fn
        self.pid = pid

    @classmethod
    def from_listener_process(cls, process):

        return cls(
            puuid_dependency = None,
            puuid = uuid.UUID(process['uuid']),
            msg_to_unreal_client = '',
            name = process['name'],
            pid = process['pid'],
            launch_fn = None,
        )

def use_lock(func):
    ''' Decorator to ensure the decorated function is executed by a single thread at a time
    '''
    def _use_lock(self, *args, **kwargs):
        self.lock.acquire()
        try:
            return func(self, *args, **kwargs)
        finally:
            self.lock.release()
            
    return _use_lock

class ProgramStartQueue(object):
    ''' Queue of programs to launch that may have dependencies
    '''

    def __init__(self):

        self.lock = threading.Lock() # Needed because these functions will be called from listener thread and main thread

        self.queued_programs = [] # ProgramStartQueueItem
        self.starting_programs = OrderedDict() # [puuid] = ProgramStartQueueItem 
        self.running_programs = OrderedDict()  # [puuid] = ProgramStartQueueItem (initialized as returned by listener)

    @use_lock
    def reset(self):
        ''' Clears the list and any internal state
        '''
        self.queued_programs = []
        self.starting_programs = OrderedDict()
        self.running_programs = OrderedDict()

    def _name_from_puuid(self, puuid):
        ''' Returns the name of the specified program id
        '''
        for prog in self.queued_programs:
            if prog.puuid == puuid:
                return prog.name

        for thedict in [self.starting_programs, self.running_programs]:
            try:
                return thedict[puuid].name
            except KeyError:
                pass

        raise KeyError

    @use_lock
    def puuid_from_name(self, name):
        ''' Returns the puuid of the specified program name
        Searches as to return the one most likely to return last - but not guaranteed to do so.
        '''
        for prog in self.queued_programs:
            if prog.name == name:
                return prog.puuid

        for thedict in [self.starting_programs, self.running_programs]:
            for prog in thedict.values():
                if prog.name == name:
                    return prog.puuid

        raise KeyError

    @use_lock
    def on_program_started(self, prog):
        ''' Returns the name of the program that started
        Moves the program from starting_programs to running_programs
        '''
        try:
            self.starting_programs.pop(prog.puuid)
        except KeyError:
            LOGGER.error(f"on_program_started::starting_programs.pop({prog.puuid}) KeyError")

        self.running_programs[prog.puuid] = prog

        return prog.name

    @use_lock
    def on_program_ended(self, puuid, unreal_client):
        ''' Returns the name of the program that ended
        Launches any dependent programs in the queue.
        Removes the program from starting_programs or running_programs.
        '''
        # remove from lists
        #
        prog = None

        if puuid is not None:
            # check if it was waiting to start
            try:
                prog = self.starting_programs.pop(puuid)
            except KeyError:
                pass

            # check if it is already running
            if prog is None:
                try:
                    prog = self.running_programs.pop(puuid)
                except KeyError:
                    pass

            # check if it is queued
            if prog is None:
                programs = [program for program in self.queued_programs if prog.puuid == puuid]

                for program in programs:
                    prog = program
                    self.queued_programs.remove(program)

            if prog is not None:
                LOGGER.debug(f"Ended {prog.name} {prog.puuid}")

        self._launch_dependents(puuid=puuid, unreal_client=unreal_client)

        return prog.name if prog else "unknown"

    def _launch_dependents(self, puuid, unreal_client):
        ''' Launches programs dependend on given puuid
        Do not call externally because it does not use the thread lock.
        '''
        # see if we need to launch any dependencies

        progs_launched = []

        for prog in self.queued_programs:            
            if (prog.puuid_dependency is None) or puuid == prog.puuid_dependency:

                progs_launched.append(prog)
                self.starting_programs[prog.puuid] = prog

                unreal_client.send_message(prog.msg_to_unreal_client)
                prog.launch_fn()

        for prog in progs_launched:
            self.queued_programs.remove(prog)

    @use_lock
    def add(self, prog, unreal_client):
        ''' Adds a new program to be started in the queue
        Must be of type ProgramStartQueueItem.
        It may start launch it right away if it doesn't have any dependencies.
        '''
        assert isinstance(prog, ProgramStartQueueItem)

        # ensure that dependance is still possible, and if it isn't, replace with None
        if prog.puuid_dependency is not None:
            try:
                self._name_from_puuid(prog.puuid_dependency)
            except KeyError:
                LOGGER.debug(f"{prog.name} specified non-existent dependency on puuid {prog.puuid_dependency}")
                prog.puuid_dependency = None

        self.queued_programs.append(prog)

        # This effectively causes a launch if it doesn't have any dependencies
        self._launch_dependents(puuid=None, unreal_client=unreal_client)

    @use_lock
    def running_programs_named(self, name):
        ''' Returns the program ids of running programs named as specified
        '''
        return [prog for puuid, prog in self.running_programs.items() if prog.name == name]

    @use_lock
    def running_puuids_named(self, name):
        ''' Returns the program ids of running programs named as specified
        '''
        return [puuid for puuid, prog in self.running_programs.items() if prog.name == name]

    @use_lock
    def update_running_program(self, prog):
        self.running_programs[prog.puuid] = prog

    @use_lock
    def clear_running_programs(self):
        self.running_programs.clear()

class DeviceUnreal(Device):

    csettings = {
        'buffer_size': Setting(
            attr_name="buffer_size", 
            nice_name="Buffer Size", 
            value=1024, 
            tool_tip="Buffer size used for communication with SwitchboardListener",
        ),
        'command_line_arguments': Setting(
            attr_name="command_line_arguments", 
            nice_name='Command Line Arguments', 
            value="", 
            tool_tip=f'Additional command line arguments for the engine',
        ),
        'exec_cmds': Setting(
            attr_name="exec_cmds", 
            nice_name='ExecCmds', 
            value="", 
            tool_tip=f'ExecCmds to be passed. No need for outer double quotes.',
        ),
        'port': Setting(
            attr_name="port", 
            nice_name="Listener Port", 
            value=2980, 
            tool_tip="Port of SwitchboardListener"
        ),
        'roles_filename': Setting(
            attr_name="roles_filename", 
            nice_name="Roles Filename", 
            value="VPRoles.ini", 
            tool_tip="File that stores VirtualProduction roles. Default: Config/Tags/VPRoles.ini",
        ),
        'stage_session_id': Setting(
            attr_name="stage_session_id", 
            nice_name="Stage Session ID",
            value=0,
            tool_tip="An ID that groups Stage Monitor providers and monitors. Instances with different Session IDs are invisible to each other in Stage Monitor.",
        ),
        'ue4_exe': Setting(
            attr_name="editor_exe", 
            nice_name="UE4 Editor filename", 
            value="UE4Editor.exe",
        ),
        'max_gpu_count': Setting(
            attr_name="max_gpu_count", 
            nice_name="Number of GPUs",
            value=1,
            possible_values=list(range(1, 17)),
            tool_tip="If you have multiple GPUs in the PC, you can specify how many to use.",
        ),
    }

    unreal_started_signal = QtCore.Signal()

    def __init__(self, name, ip_address, **kwargs):
        super().__init__(name, ip_address, **kwargs)

        self.unreal_client = ListenerClient(
            ip_address=self.ip_address, 
            port=DeviceUnreal.csettings['port'].get_value(self.name), 
            buffer_size=DeviceUnreal.csettings['buffer_size'].get_value(self.name)
        )

        roles = kwargs.get("roles", [])

        self.setting_roles = Setting(
            attr_name="roles", 
            nice_name="Roles", 
            value=roles, 
            possible_values=[], 
            tool_tip="List of roles for this device"
        )

        self.setting_ip_address.signal_setting_changed.connect(self.on_setting_ip_address_changed)
        DeviceUnreal.csettings['port'].signal_setting_changed.connect(self.on_setting_port_changed)
        CONFIG.BUILD_ENGINE.signal_setting_changed.connect(self.on_build_engine_changed)

        self.auto_connect = False

        self.inflight_project_cl = None
        self.inflight_engine_cl = None

        self.sync_progress = 0

        # Set a delegate method if the device gets a disconnect signal
        self.unreal_client.disconnect_delegate = self.on_listener_disconnect
        self.unreal_client.receive_file_completed_delegate = self.on_file_received
        self.unreal_client.receive_file_failed_delegate = self.on_file_receive_failed
        self.unreal_client.delegates["state"] = self.on_listener_state
        self.unreal_client.delegates["kill"] = self.on_program_killed
        self.unreal_client.delegates["programstdout"] = self.on_listener_programstdout
        self.unreal_client.delegates["program ended"] = self.on_program_ended
        self.unreal_client.delegates["program started"] = self.on_program_started
        self.unreal_client.delegates["program killed"] = self.on_program_killed
        self.unreal_client.delegates["start"] = self.on_program_started

        self.osc_connection_timer = QtCore.QTimer(self)
        self.osc_connection_timer.timeout.connect(self._try_connect_osc)

        # on_program_started is called from inside the connection thread but QTimer can only be used from the main thread.
        # so we connect to a signal sent on the connection threat and can then start the timer on the main thread.
        self.unreal_started_signal.connect(self.on_unreal_started)

        # keeps track of programs to start
        self.program_start_queue = ProgramStartQueue()

        # determines if it should force clobber in workspace
        self.force_clobber = False

    @classmethod
    def plugin_settings(cls):
        return Device.plugin_settings() + list(DeviceUnreal.csettings.values())

    def setting_overrides(self):
        return super().setting_overrides() + [
            Device.csettings['is_recording_device'],
            DeviceUnreal.csettings['command_line_arguments'],
            DeviceUnreal.csettings['exec_cmds'],
            DeviceUnreal.csettings['max_gpu_count'],
            CONFIG.ENGINE_DIR,
            CONFIG.SOURCE_CONTROL_WORKSPACE,
            CONFIG.UPROJECT_PATH,
        ]

    def device_settings(self):
        return super().device_settings() + [self.setting_roles]

    @classmethod
    def plugin_header_widget_config(cls):
        """ combination of widgets that will be visualized in the plugin header. """
        return super().plugin_header_widget_config() | PluginHeaderWidgets.OPEN_BUTTON | PluginHeaderWidgets.CHANGELIST_LABEL

    def on_setting_ip_address_changed(self, _, new_address):
        LOGGER.info(f"Updating IP address for ListenerClient to {new_address}")
        self.unreal_client.ip_address = new_address

    def on_setting_port_changed(self, _, new_port):
        if not DeviceUnreal.csettings['port'].is_overriden(self.name):
            LOGGER.info(f"Updating Port for ListenerClient to {new_port}")
            self.unreal_client.port = new_port

    def on_build_engine_changed(self, _, build_engine):
        if build_engine:
            self.widget.engine_changelist_label.show()
            if self.status != DeviceStatus.DISCONNECTED:
                self._request_engine_changelist_number()
        else:
            self.widget.engine_changelist_label.hide()

    def set_slate(self, value):
        if not self.is_recording_device:
            return

        self.send_osc_message(osc.SLATE, value)

    def set_take(self, value):
        if not self.is_recording_device:
            return

        self.send_osc_message(osc.TAKE, value)

    def record_stop(self):
        if not self.is_recording_device:
            return

        self.send_osc_message(osc.RECORD_STOP, 1)

    def _request_roles_file(self):
        uproject_path = CONFIG.UPROJECT_PATH.get_value(self.name).replace('"', '')
        roles_filename = DeviceUnreal.csettings["roles_filename"].get_value(self.name)
        roles_file_path = os.path.join(os.path.dirname(uproject_path), "Config", "Tags", roles_filename)
        _, msg = message_protocol.create_copy_file_from_listener_message(roles_file_path)
        self.unreal_client.send_message(msg)

    def _request_project_changelist_number(self):

        if not CONFIG.P4_ENABLED.get_value():
            return

        client_name = CONFIG.SOURCE_CONTROL_WORKSPACE.get_value(self.name)

        if not client_name:
            LOGGER.warning(f"{self.name}: Missing workspace name to query the project changelist")
            return

        p4_path = CONFIG.P4_PROJECT_PATH.get_value()

        if not p4_path:
            LOGGER.warning(f"{self.name}: Missing p4 path to query the project changelist")
            return

        formatstring = "%change%"
        args = f'-F "{formatstring}" -c {client_name} cstat {p4_path}/...#have'

        program_name = "cstat_project"

        puuid, msg = message_protocol.create_start_process_message(
            prog_path="p4", 
            prog_args=args, 
            prog_name=program_name, 
            caller=self.name,
            update_clients_with_stdout=False,
        )

        self.program_start_queue.add(
            ProgramStartQueueItem(
                name = program_name,
                puuid_dependency = None,
                puuid = puuid,
                msg_to_unreal_client = msg,
            ),
            unreal_client = self.unreal_client,
        )

    def _request_engine_changelist_number(self):

        if not CONFIG.P4_ENABLED.get_value():
            return

        client_name = CONFIG.SOURCE_CONTROL_WORKSPACE.get_value(self.name)

        if not client_name:
            LOGGER.warning(f"{self.name}: Missing workspace name to query the engine changelist")
            return

        p4_path = CONFIG.P4_ENGINE_PATH.get_value()

        if not p4_path:
            LOGGER.warning(f'{self.name}: Missing p4 path to query the engine changelist')
            return

        formatstring = "%change%"
        args = f'-F "{formatstring}" -c {client_name} cstat {p4_path}/...#have'

        program_name = "cstat_engine"

        puuid, msg = message_protocol.create_start_process_message(
            prog_path="p4", 
            prog_args=args, 
            prog_name=program_name, 
            caller=self.name,
            update_clients_with_stdout=False,
        )

        self.program_start_queue.add(
            ProgramStartQueueItem(
                name = program_name,
                puuid_dependency = None,
                puuid = puuid,
                msg_to_unreal_client = msg,
            ),
            unreal_client = self.unreal_client,
        )

    @QtCore.Slot()
    def connect_listener(self):
        ''' Connects to the listener
        '''
        # Ensure this code is run from the main thread
        if threading.current_thread().name != 'MainThread':
            QtCore.QMetaObject.invokeMethod(self, 'connect_listener', QtCore.Qt.QueuedConnection)
            return

        is_connected = self.unreal_client.connect()

        if not is_connected:
            self.device_qt_handler.signal_device_connect_failed.emit(self)
            return

        super().connect_listener()

    @QtCore.Slot()
    def disconnect_listener(self):
        ''' Disconnects from the listener
        '''
        # Ensure this code is run from the main thread
        if threading.current_thread().name != 'MainThread':
            QtCore.QMetaObject.invokeMethod(self, 'disconnect_listener', QtCore.Qt.QueuedConnection)
            return

        super().disconnect_listener()
        self.unreal_client.disconnect()

    def sync(self, engine_cl, project_cl):
        if not engine_cl and not project_cl:
            LOGGER.warning(f"Neither project nor engine changelist is selected. There is nothing to sync!")
            return

        if engine_cl:
            self._sync_engine(engine_cl)
            self.status = DeviceStatus.SYNCING

        if project_cl:
            self._sync_project(project_cl)
            if self.status != DeviceStatus.SYNCING:
                self.status = DeviceStatus.SYNCING

    def _sync_engine(self, engine_cl):

        program_name = "sync_engine"

        # check if it is already on its way:
        try:
            existing_puuid = self.program_start_queue.puuid_from_name(program_name)
            LOGGER.info(f"{self.name}: Already syncing engine with puuid {existing_puuid}")
            return
        except KeyError:
            pass

        project_name = os.path.basename(os.path.dirname(CONFIG.UPROJECT_PATH.get_value(self.name)))
        LOGGER.info(f"{self.name}: Queuing {program_name} for project {project_name} to revision {engine_cl}")

        self.inflight_engine_cl = engine_cl

        # when no uproject is specified to the UAT SyncProject script, it will go into EngineOnly mode and only sync the engine
        # as well as all loose files one directoy up from the engine directory.
        sync_tool = f'{os.path.normpath(os.path.join(CONFIG.ENGINE_DIR.get_value(self.name), "Build", "BatchFiles", "RunUAT.bat"))}'
        sync_args = f'-P4 SyncProject -cl={engine_cl} -threads=8 -generate'

        # check if project sync is already happening:
        try:
            sync_project_puuid = self.program_start_queue.puuid_from_name('sync_project')
        except KeyError:
            sync_project_puuid = None

        puuid, msg = message_protocol.create_start_process_message(
            prog_path=sync_tool, 
            prog_args=sync_args, 
            prog_name=program_name, 
            caller=self.name,
            update_clients_with_stdout=True,
        )

        self.program_start_queue.add(
            ProgramStartQueueItem(
                name = program_name,
                puuid_dependency = sync_project_puuid,
                puuid = puuid,
                msg_to_unreal_client = msg,
                launch_fn= lambda : LOGGER.info(f"{self.name}: Sending engine sync command: {sync_tool} {sync_args}")
            ),
            unreal_client = self.unreal_client,
        )


    def _sync_project(self, project_cl):

        sync_project_prog_name = "sync_project"

        # check if it is already on its way:
        try:
            existing_puuid = self.program_start_queue.puuid_from_name(sync_project_prog_name)
            LOGGER.info(f"{self.name}: Already syncing with puuid {existing_puuid}")
            return
        except KeyError:
            pass

        project_name = os.path.basename(os.path.dirname(CONFIG.UPROJECT_PATH.get_value(self.name)))
        LOGGER.info(f"{self.name}: Queueing {sync_project_prog_name} {project_name} to revision {project_cl}")
        self.inflight_project_cl = project_cl

        sync_tool = ""
        sync_args = ""

        # todo: UAT requires help finding the correct workspace.
        # the only way this can be done is by setting the environment variable uebp_CLIENT to whatever we have in CONFIG.SOURCE_CONTROL_WORKSPACE.
        # however that needs to happen on the listener machines, so we would need a way to (platform-independently) set env variables when
        # running commands and extend the protocol to include setting env vars as well.
        # alternatively we could try running commands through cmd /V /C "set uebp_CLIENT=CONFIG.SOURCE_CONTROL_WORKSPACE&& EXE ARGS"
        # but that would only work for Windows and thus require us to know what the listener machine's OS is.
        engine_under_source_control = CONFIG.BUILD_ENGINE.get_value()

        workspace = CONFIG.SOURCE_CONTROL_WORKSPACE.get_value(self.name)

        if engine_under_source_control:
            sync_tool = f'{os.path.normpath(os.path.join(CONFIG.ENGINE_DIR.get_value(self.name), "Build", "BatchFiles", "RunUAT.bat"))}'
            sync_args = f'-P4 SyncProject -project="{CONFIG.UPROJECT_PATH.get_value(self.name)}" -projectonly -cl={project_cl} -threads=8 -generate'
        else:
            # for installed/vanilla engine we directly call p4 to sync the project itself.
            p4_path = CONFIG.P4_PROJECT_PATH.get_value()
            sync_tool = "p4"
            sync_args = f"-c{workspace} sync {p4_path}/...@{project_cl}"

        # check if engine sync is already happening:
        try:
            sync_engine_puuid = self.program_start_queue.puuid_from_name('sync_engine')
        except KeyError:
            sync_engine_puuid = None

        # find out if it is noclobber
        #
        program_name = "isclobber"

        puuid_isclobber, msg = message_protocol.create_start_process_message(
            prog_path="p4", 
            prog_args=f"client -o {workspace}",
            prog_name=program_name, 
            caller=self.name,
            update_clients_with_stdout=False,
        )

        self.program_start_queue.add(
            ProgramStartQueueItem(
                name = program_name,
                puuid_dependency = sync_engine_puuid,
                puuid = puuid_isclobber,
                msg_to_unreal_client = msg,
                launch_fn= lambda : LOGGER.info(f"{self.name}: Sending project sync command: {sync_tool} {sync_args}"),
            ),
            unreal_client = self.unreal_client,
        )

        # allow clobbering
        #
        if self.force_clobber and sys.platform.startswith('win'):

            program_name = "clobber"

            puuid_clobber, msg = message_protocol.create_start_process_message(
                prog_path="powershell", 
                prog_args=f"p4 client -o {workspace} | %{{$_ -replace 'noclobber', 'clobber'}} | p4 client -i",
                prog_name=program_name, 
                caller=self.name,
                update_clients_with_stdout=True,
            )

            self.program_start_queue.add(
                ProgramStartQueueItem(
                    name = program_name,
                    puuid_dependency = puuid_isclobber,
                    puuid = puuid_clobber,
                    msg_to_unreal_client = msg,
                ),
                unreal_client = self.unreal_client,
            )
        else:
            # TODO: Support other operating systems
            puuid_clobber = puuid_isclobber # transfer puuid dependency to clobber puuid

        # start sync
        #

        puuid_sync, msg = message_protocol.create_start_process_message(
            prog_path=sync_tool, 
            prog_args=sync_args, 
            prog_name=sync_project_prog_name, 
            caller=self.name,
            update_clients_with_stdout=True,
        )

        self.program_start_queue.add(
            ProgramStartQueueItem(
                name = sync_project_prog_name,
                puuid_dependency = puuid_clobber,
                puuid = puuid_sync,
                msg_to_unreal_client = msg,
            ),
            unreal_client = self.unreal_client,
        )

        # disallow clobbering
        
        if self.force_clobber and sys.platform.startswith('win'):

            program_name = "noclobber"

            puuid, msg = message_protocol.create_start_process_message(
                prog_path="powershell", 
                prog_args=f"p4 client -o {workspace} | %{{$_ -replace ' clobber', ' noclobber'}} | p4 client -i",
                prog_name=program_name, 
                caller=self.name,
                update_clients_with_stdout=True,
            )

            self.program_start_queue.add(
                ProgramStartQueueItem(
                    name = program_name,
                    puuid_dependency = puuid_sync,
                    puuid = puuid,
                    msg_to_unreal_client = msg,
                ),
                unreal_client = self.unreal_client,
            )
        else:
            # TODO: Support other operating systems
            pass

    def build(self):

        program_name = "build"

        # check if it is already on its way:
        try:
            existing_puuid = self.program_start_queue.puuid_from_name(program_name)
            LOGGER.info(f"{self.name}: Already building with puuid {existing_puuid}")
            return
        except KeyError:
            pass

        # check for any sync dependencies
        #
        sync_puuid = None
        sync_name = ''
        for sync_program_name in ['sync_engine', 'sync_project']: # order matters, as we sync the engine before the project.
            try:
                sync_puuid = self.program_start_queue.puuid_from_name(sync_program_name)
                sync_name = sync_program_name
            except KeyError:
                pass

        if sync_puuid:
            LOGGER.debug(f"{self.name} Queued build after {sync_name} with puuid {sync_puuid}")

        # Generate build command
        #
        engine_path = CONFIG.ENGINE_DIR.get_value(self.name)
        build_tool = os.path.join(engine_path, "Binaries", "DotNET", "UnrealBuildTool")
        build_args = f'Win64 Development -project="{CONFIG.UPROJECT_PATH.get_value(self.name)}" -TargetType=Editor -Progress -NoHotReloadFromIDE' 

        puuid, msg = message_protocol.create_start_process_message(
            prog_path=build_tool, 
            prog_args=build_args, 
            prog_name=program_name, 
            caller=self.name,
            update_clients_with_stdout=True,
        )

        def launch_fn():
            LOGGER.info(f"{self.name}: Sending build command: {build_tool} {build_args}")
            self.status = DeviceStatus.BUILDING

        # Queue the build command
        #
        self.program_start_queue.add(
            ProgramStartQueueItem(
                name = program_name,
                puuid_dependency = sync_puuid,
                puuid = puuid,
                msg_to_unreal_client = msg,
                launch_fn=launch_fn,
            ),
            unreal_client = self.unreal_client,
        )

    def close(self, force=False):

        # This call only refers to "unreal" programs.

        unreal_puuids = self.program_start_queue.running_puuids_named('unreal')

        for unreal_puuid in unreal_puuids:
            _, msg = message_protocol.create_kill_process_message(unreal_puuid)
            self.unreal_client.send_message(msg)

        if not len(unreal_puuids):
            self.status = DeviceStatus.CLOSED

    def force_focus(self):
        ''' Asks the listener to force focus on the 'unreal' window
        '''
        unreals = self.program_start_queue.running_programs_named('unreal')

        if not len(unreals):
            return

        pid = unreals[0].pid

        if not pid:
            return
        
        _, msg = message_protocol.create_forcefocus_message(pid)
        self.unreal_client.send_message(msg)
    
    def fix_exe_flags(self):
        ''' Tries to force the correct UE4Editor.exe flags
        '''
        unreals = self.program_start_queue.running_programs_named('unreal')

        if not len(unreals):
            return

        puuid = unreals[0].puuid

        if not puuid:
            return

        _, msg = message_protocol.create_fixExeFlags_message(puuid)
        self.unreal_client.send_message(msg)

    def generate_unreal_exe_path(self):
        return CONFIG.engine_path(CONFIG.ENGINE_DIR.get_value(self.name), DeviceUnreal.csettings["ue4_exe"].get_value())

    def get_vproles(self):
        ''' Gets selected vp roles that are also present in the ini file
        Also returns any selected vp roles that are not in the ini file.
        '''

        vproles = self.setting_roles.get_value()
        missing_roles = [role for role in vproles if role not in self.setting_roles.possible_values]
        vproles = [role for role in vproles if role not in missing_roles]

        return vproles, missing_roles

    def generate_unreal_command_line_args(self, map_name):

        command_line_args = f'{DeviceUnreal.csettings["command_line_arguments"].get_value(self.name)}'
        if CONFIG.MUSERVER_AUTO_JOIN:
            command_line_args += f' -CONCERTRETRYAUTOCONNECTONERROR -CONCERTAUTOCONNECT -CONCERTSERVER={CONFIG.MUSERVER_SERVER_NAME} -CONCERTSESSION={SETTINGS.MUSERVER_SESSION_NAME} -CONCERTDISPLAYNAME={self.name}'
        
        exec_cmds = f'{DeviceUnreal.csettings["exec_cmds"].get_value(self.name)}'.strip()
        if len(exec_cmds):
            command_line_args += f' -ExecCmds="{exec_cmds}" '

        selected_roles = self.setting_roles.get_value()
        unsupported_roles = [role for role in selected_roles if role not in self.setting_roles.possible_values]
        supported_roles = [role for role in selected_roles if role not in unsupported_roles]

        if supported_roles:
            command_line_args += ' -VPRole=' + '|'.join(supported_roles)
        if unsupported_roles:
            LOGGER.error(f"{self.name}: Omitted unsupported roles: {'|'.join(unsupported_roles)}")

        # Session ID
        #
        session_id = DeviceUnreal.csettings["stage_session_id"].get_value()
        if session_id > 0:
            command_line_args += f" -StageSessionId={session_id}"

        command_line_args += f" -StageFriendlyName={self.name}"

        # Max GPU Count (mGPU)
        #
        max_gpu_count = DeviceUnreal.csettings["max_gpu_count"].get_value(self.name)
        try:
            if int(max_gpu_count) > 1:
                command_line_args += f" -MaxGPUCount={max_gpu_count} "
        except ValueError:
            LOGGER.warning(f"Invalid Number of GPUs '{max_gpu_count}'")

        args = f'"{CONFIG.UPROJECT_PATH.get_value(self.name)}" {map_name} {command_line_args}'
        return args

    def generate_unreal_command_line(self, map_name):
        return self.generate_unreal_exe_path(), self.generate_unreal_command_line_args(map_name)

    def launch(self, map_name, program_name="unreal"):

        if map_name == DEFAULT_MAP_TEXT:
            map_name = ''

        engine_path, args = self.generate_unreal_command_line(map_name)
        LOGGER.info(f"Launching UE4: {engine_path} {args}")

        puuid, msg = message_protocol.create_start_process_message(
            prog_path=engine_path, 
            prog_args=args, 
            prog_name=program_name, 
            caller=self.name, 
            update_clients_with_stdout=False,
            force_window_focus = True,
        )

        self.program_start_queue.add(
            ProgramStartQueueItem(
                name = program_name,
                puuid_dependency = None,
                puuid = puuid,
                msg_to_unreal_client = msg,
            ),
            unreal_client = self.unreal_client,
        )

    def _try_connect_osc(self):
        if self.status == DeviceStatus.OPEN:
            self.send_osc_message(osc.OSC_ADD_SEND_TARGET, [SETTINGS.IP_ADDRESS, CONFIG.OSC_SERVER_PORT.get_value()])
        else:
            self.osc_connection_timer.stop()

    def on_listener_disconnect(self, unexpected=False, exception=None):

        self.program_start_queue.reset()

        if unexpected:
            self.device_qt_handler.signal_device_client_disconnected.emit(self)

    def do_program_running_update(self, prog):

        if prog.name == "unreal":
            self.status = DeviceStatus.OPEN
            self.unreal_started_signal.emit()
        elif prog.name == "build":
            self.status = DeviceStatus.BUILDING
        elif 'sync' in prog.name:
            self.sync_progress = 0
            self.status = DeviceStatus.SYNCING

        self.program_start_queue.update_running_program(prog=prog)

    def on_program_started(self, message):
        ''' Handler of the "start program" command
        '''
        # check if the operation failed
        if not message['bAck']:
            # when we send the start program command, the listener will use the message uuid as program uuid
            puuid = uuid.UUID(message['puuid'])

            # Tell the queue that the program ended (even before it started)
            program_name = self.program_start_queue.on_program_ended(puuid=puuid, unreal_client=self.unreal_client)

            # log this
            LOGGER.error(f"Could not start {program_name}: {message['error']}")

            if program_name in ["sync", "build"]:
                self.status = DeviceStatus.CLOSED
                self.project_changelist = self.project_changelist # force to show existing project_changelist to hide building/syncing
            elif program_name == 'unreal':
                self.status = DeviceStatus.CLOSED

            return

        # grab the process data
        try:
            process = message['process']
        except KeyError:
            LOGGER.warning(f"{self.name} Received 'on_program_started' but no 'process' in the message")
            return

        if process['caller'] != self.name:
            return

        prog = ProgramStartQueueItem.from_listener_process(process)

        LOGGER.info(f"{self.name}: {prog.name} with id {prog.puuid} was successfully started")

        # Tell the queue that the program started
        self.program_start_queue.on_program_started(prog=prog)

        # Perform any necessary updates to the device
        self.do_program_running_update(prog=prog)

    def on_unreal_started(self):

        if self.is_recording_device:
            sleep_time_in_ms = 1000
            self.osc_connection_timer.start(sleep_time_in_ms)

    def on_program_ended(self, message):

        try:
            process = message['process']
        except KeyError:
            LOGGER.warning(f"Received 'on_program_ended' but no 'process' in the message")
            return

        if process['caller'] != self.name:
            return

        puuid = uuid.UUID(process['uuid'])
        returncode = message['returncode']
        output = message['output']

        LOGGER.info(f"{self.name}: Program with id {puuid} exited with returncode {returncode}")

        try:
            program_name = self.program_start_queue.on_program_ended(puuid=puuid, unreal_client=self.unreal_client)
        except KeyError:
            LOGGER.error(f"{self.name}: on_program_ended with unknown id {puuid}")
            return

        # check if there are remaining programs named the same but with different ids, which is not normal.
        remaining_homonyms = self.program_start_queue.running_puuids_named(program_name)
        for prog_id in remaining_homonyms:
            LOGGER.warning(f'{self.name}: But ({prog_id}) with the same name "{program_name}" is still in the list, which is unusual')

        if program_name == "unreal" and not len(remaining_homonyms):
            self.status = DeviceStatus.CLOSED

        elif "sync" in program_name:
            
            if "sync_engine" == program_name:

                if returncode == 0:
                    LOGGER.info(f"{self.name}: Engine was synced successfully")
                else:
                    LOGGER.error(f"{self.name}: Engine was not synced successfully!")
                    for line in output.splitlines():
                        LOGGER.error(f"{self.name}: {line}")

                    # flag the inflight cl as invalid
                    self.inflight_engine_cl = None

                    # notify of the failure
                    self.device_qt_handler.signal_device_sync_failed.emit(self)

                # Update CL with the one in flight
                self.engine_changelist = self.inflight_engine_cl
                self.inflight_engine_cl = None

                # refresh project CL
                self.project_changelist = self.project_changelist

            elif "sync_project" == program_name:

                if returncode == 0:
                    LOGGER.info(f"{self.name}: Project was synced successfully")
                else:
                    LOGGER.error(f"{self.name}: Project was not synced successfully!")
                    for line in output.splitlines():
                        LOGGER.error(f"{self.name}: {line}")

                    # flag the inflight cl as invalid
                    self.inflight_project_cl = None

                    # notify of the failure
                    self.device_qt_handler.signal_device_sync_failed.emit(self)

                # Update CL with the one in flight
                self.project_changelist = self.inflight_project_cl
                self.inflight_project_cl = None

                # If you build and sync the engine, update its CL
                if CONFIG.BUILD_ENGINE.get_value():
                    self.engine_changelist = self.inflight_engine_cl if self.inflight_engine_cl != None else self.engine_changelist
                    self.inflight_engine_cl = None

            if self.inflight_project_cl == None and self.inflight_engine_cl == None:
                # everything is done syncing
                self.status = DeviceStatus.CLOSED

        elif program_name == "build":
            if returncode == 0:
                LOGGER.info(f"{self.name}: Project was built successfully!")
            else:
                LOGGER.error(f"{self.name}: Project was not built successfully!")
                for line in output.splitlines():
                    # error lines from UBT usually look like this: $SourceFile: error C4430: missing type specifier - int assumed...
                    if ": error " in line:
                        LOGGER.error(f"{self.name}: {line}")

            self.status = DeviceStatus.CLOSED
            self.project_changelist = self.project_changelist # forces an update to the changelist field (to hide the Building state)
            if CONFIG.BUILD_ENGINE.get_value():
                self.engine_changelist = self.engine_changelist

        elif "cstat" in program_name:

            changelists = [line.strip() for line in output.split()]

            try:
                current_changelist = str(int(changelists[-1]))

                # when not connected to p4, you get a message similar to:
                #  "Perforce client error: Connect to server failed; check $P4PORT. TCP connect to perforce:1666 failed. No such host is known."
                if 'Perforce client error:' in output:
                    raise ValueError

            except (ValueError,IndexError):
                LOGGER.error(f"{self.name}: Could not retrieve changelists for project. Are the Source Control Settings correctly configured?")
                return

            if program_name.endswith("project"):
                project_name = os.path.basename(os.path.dirname(CONFIG.UPROJECT_PATH.get_value(self.name)))
                LOGGER.info(f"{self.name}: Project {project_name} is on revision {current_changelist}")
                self.project_changelist = current_changelist
            elif program_name.endswith("engine"):
                project_name = os.path.basename(os.path.dirname(CONFIG.UPROJECT_PATH.get_value(self.name)))
                LOGGER.info(f"{self.name}: Engine used for project {project_name} is on revision {current_changelist}")
                self.engine_changelist = current_changelist

        elif program_name == 'isclobber':
            for line in output.splitlines():
                if line.startswith('Options:'):
                    self.force_clobber = True if 'noclobber' in line else False
                    break


    def on_program_killed(self, message):
        ''' Handler of killed program. Expect on_program_ended for anything other than a fail.
        '''
        if not message['bAck']:

            # remove from list of puuids (if it exists)
            #
            puuid = uuid.UUID(message['puuid'])
            self.program_start_queue.on_program_ended(puuid=puuid, unreal_client=self.unreal_client)

            LOGGER.error(f"{self.name} Unable to close program with id {str(puuid)}. Error was '{message['error']}''")

    def on_file_received(self, source_path, content):
        if source_path.endswith(DeviceUnreal.csettings["roles_filename"].get_value(self.name)):
            decoded_content = base64.b64decode(content).decode()
            tags = parse_unreal_tag_file(decoded_content.splitlines())
            self.setting_roles.possible_values = tags
            LOGGER.info(f"{self.name}: All possible roles: {tags}")
            unsupported_roles = [role for role in self.setting_roles.get_value() if role not in tags]
            if len(unsupported_roles) > 0:
                LOGGER.error(f"{self.name}: Found unsupported roles: {unsupported_roles}")
                LOGGER.error(f"{self.name}: Please change the roles for this device in the settings or in the unreal project settings!")

    def on_file_receive_failed(self, source_path, error):
        roles = self.setting_roles.get_value()
        if len(roles) > 0:
            LOGGER.error(f"{self.name}: Error receiving role file from listener and device claims to have these roles: {' | '.join(roles)}")
            LOGGER.error(f"Error: {error}")

    def on_listener_programstdout(self, message):
        ''' Handles updates to stdout of programs
        Particularly useful to update build progress
        '''

        process = message['process']

        if process['caller'] != self.name:
            return

        stdout = ''.join([chr(b) for b in message['partialStdout'] if b != ord('\r')])
        lines = list(filter(None, stdout.split('\n')))

        # see if this is an update to the build
        #
        # example lines:
        #
        #    @progress push 5% 
        #    @progress 'Generating code...' 0% 
        #    @progress 'Generating code...' 67% 
        #    @progress 'Generating code...' 100% 
        #    @progress pop 
        #
        if process['name'] == 'build':
            for line in lines:
                if '@progress' in line:
                    stepparts = line.split("'")

                    if len(stepparts) < 2:
                        break

                    step = stepparts[-2].strip()

                    percent = line.split(' ')[-1].strip()
                    
                    if '%' == percent[-1]:
                        self.device_qt_handler.signal_device_build_update.emit(self, step, percent)

            # Remove non-error lines to avoid overloading the UI
            lines = [line for line in lines if 'error' in line.lower()]

        elif 'sync' in process['name']:
            self.sync_progress += len(lines)
            self.device_qt_handler.signal_device_sync_update.emit(self, self.sync_progress)

            # Remove non-error lines to avoid overloading the UI
            lines = [line for line in lines if 'error' in line.lower()]

        for line in lines:
            LOGGER.debug(f"{self.name} {process['name']}: {line}")

    def on_listener_state(self, message):
        ''' Message expected to be received upon connection with the listener.
        It contains the state of the listener. Particularly useful when Switchboard reconnects.
        '''
        
        self.program_start_queue.clear_running_programs()

        try:
            version = message['version']

            major = (version >> 16) & 0xFF
            minor = (version >>  8) & 0xFF
            patch = (version >>  0) & 0xFF

            LOGGER.info(f"{self.name} Connected to listener version {major}.{minor}.{patch}")

            desired_major = 0x01
            min_minor = 0x01

            if not((major == desired_major) and (minor >= min_minor)):
                LOGGER.error(f"This version of the listener is incompatible with Switchboard. "\
                             f"We expected {desired_major}.>={min_minor}.xx. Disconnecting...")
                self.disconnect_listener()

        except KeyError:
            LOGGER.error(f"This unversioned listener is TOO OLD. Disconnecting...")
            self.disconnect_listener()

        # update list of running processes
        #
        for process in message['runningProcesses']:

            prog = ProgramStartQueueItem.from_listener_process(process)

            if process['caller'] == self.name:
                LOGGER.warning(f"{self.name} already running {prog.name} {prog.puuid}")
                self.do_program_running_update(prog=prog)        

        # request roles and changelists
        #
        self._request_roles_file()
        self._request_project_changelist_number()

        if CONFIG.BUILD_ENGINE.get_value():
            self._request_engine_changelist_number()

    def transport_paths(self, device_recording):
        """
        Do not transport UE4 paths as they will be checked into source control
        """
        return []


def parse_unreal_tag_file(file_content):
    tags = []
    for line in file_content:
        if line.startswith("GameplayTagList"):
            tag = line.split("Tag=")[1]
            tag = tag.split(',', 1)[0]
            tag = tag.strip('"')
            tags.append(tag)
    return tags


class DeviceWidgetUnreal(DeviceWidget):
    def __init__(self, name, device_hash, ip_address, icons, parent=None):
        super().__init__(name, device_hash, ip_address, icons, parent=parent)

        CONFIG.P4_ENABLED.signal_setting_changed.connect(lambda _, enabled: self.sync_button.setVisible(enabled))

    def _add_control_buttons(self):
        super()._add_control_buttons()

        changelist_layout = QtWidgets.QVBoxLayout()
        self.engine_changelist_label = QtWidgets.QLabel()
        self.engine_changelist_label.setFont(QtGui.QFont("Roboto", 10))
        self.engine_changelist_label.setToolTip("Current Engine Changelist")
        changelist_layout.addWidget(self.engine_changelist_label)

        self.project_changelist_label = QtWidgets.QLabel()
        self.project_changelist_label.setFont(QtGui.QFont("Roboto", 10))
        self.project_changelist_label.setToolTip("Current Project Changelist")
        changelist_layout.addWidget(self.project_changelist_label)

        spacer = QtWidgets.QSpacerItem(0, 20, QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Minimum)

        self.layout.addLayout(changelist_layout)

        self.sync_button = self.add_control_button(':/icons/images/icon_sync.png',
                                                    icon_disabled=':/icons/images/icon_sync_disabled.png',
                                                    icon_hover=':/icons/images/icon_sync_hover.png',
                                                    checkable=False, tool_tip='Sync Changelist')

        self.build_button = self.add_control_button(':/icons/images/icon_build.png',
                                                    icon_disabled=':/icons/images/icon_build_disabled.png',
                                                    icon_hover=':/icons/images/icon_build_hover.png',
                                                    icon_size=QtCore.QSize(21, 21),
                                                    checkable=False, tool_tip='Build Changelist')

        self.layout.addItem(spacer)

        self.open_button = self.add_control_button(':/icons/images/icon_open.png',
                                                    icon_hover=':/icons/images/icon_open_hover.png',
                                                    icon_disabled=':/icons/images/icon_open_disabled.png',
                                                    icon_on=':/icons/images/icon_close.png',
                                                    icon_hover_on=':/icons/images/icon_close_hover.png',
                                                    icon_disabled_on=':/icons/images/icon_close_disabled.png',
                                                    tool_tip='Start Unreal')

        self.connect_button = self.add_control_button(':/icons/images/icon_connect.png',
                                                        icon_hover=':/icons/images/icon_connect_hover.png',
                                                        icon_disabled=':/icons/images/icon_connect_disabled.png',
                                                        icon_on=':/icons/images/icon_connected.png',
                                                        icon_hover_on=':/icons/images/icon_connected_hover.png',
                                                        icon_disabled_on=':/icons/images/icon_connected_disabled.png',
                                                        tool_tip='Connect to listener')

        self.sync_button.clicked.connect(self.sync_button_clicked)
        self.build_button.clicked.connect(self.build_button_clicked)
        self.connect_button.clicked.connect(self.connect_button_clicked)
        self.open_button.clicked.connect(self.open_button_clicked)

        # Disable UI when not connected
        self.open_button.setDisabled(True)
        self.sync_button.setDisabled(True)
        self.build_button.setDisabled(True)

        self.project_changelist_label.hide()
        self.engine_changelist_label.hide()
        self.sync_button.hide()

    def can_sync(self):
        return self.sync_button.isEnabled()

    def can_build(self):
        return self.build_button.isEnabled()

    def _open(self):
        # Make sure the button is in the correct state
        self.open_button.setChecked(True)
        # Emit Signal to Switchboard
        self.signal_device_widget_open.emit(self)

    def _close(self):
        # Make sure the button is in the correct state
        self.open_button.setChecked(False)
        # Emit Signal to Switchboard
        self.signal_device_widget_close.emit(self)

    def _connect(self):
        # Make sure the button is in the correct state
        self.connect_button.setChecked(True)
        self.connect_button.setToolTip("Disconnect from listener")

        self.open_button.setDisabled(False)
        self.sync_button.setDisabled(False)
        self.build_button.setDisabled(False)

        # Emit Signal to Switchboard
        self.signal_device_widget_connect.emit(self)

    def _disconnect(self):
        ''' Called when user disconnects
        '''
        self._update_disconnected_ui()

        # Emit Signal to Switchboard
        self.signal_device_widget_disconnect.emit(self)

    def _update_disconnected_ui(self):
        ''' Updates the UI of the device to reflect disconnected status
        '''
        # Make sure the button is in the correct state
        self.connect_button.setChecked(False)
        self.connect_button.setToolTip("Connect to listener")

        # Don't show the changelist
        self.project_changelist_label.hide()
        self.engine_changelist_label.hide()
        self.sync_button.hide()

        # Disable the buttons
        self.open_button.setDisabled(True)
        self.sync_button.setDisabled(True)
        self.build_button.setDisabled(True)

    def update_status(self, status, previous_status):
        super().update_status(status, previous_status)

        if status == DeviceStatus.DISCONNECTED:
            self._update_disconnected_ui()
        elif status == DeviceStatus.CLOSED:
            self.open_button.setDisabled(False)
            self.open_button.setChecked(False)
            self.sync_button.setDisabled(False)
            self.build_button.setDisabled(False)
        elif status == DeviceStatus.SYNCING:
            self.open_button.setDisabled(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)
            self.engine_changelist_label.hide()
            self.project_changelist_label.setText('Syncing...')
        elif status == DeviceStatus.BUILDING:
            self.open_button.setDisabled(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)
            self.engine_changelist_label.hide()
            self.project_changelist_label.setText('Building...')
        elif status == DeviceStatus.OPEN:
            self.open_button.setDisabled(False)
            self.open_button.setChecked(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)
        elif status == DeviceStatus.READY:
            self.open_button.setDisabled(False)
            self.open_button.setChecked(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)

        if self.open_button.isChecked():
            self.open_button.setToolTip("Stop Unreal")
        else:
            self.open_button.setToolTip("Start Unreal")

    def update_project_changelist(self, value):
        self.project_changelist_label.setText(f"P: {value}")
        self.project_changelist_label.setToolTip('Project CL')

        self.project_changelist_label.show()
        self.sync_button.show()
        self.build_button.show()

    def update_build_status(self, device, step, percent):
        self.project_changelist_label.setText(f"Building...{percent}")
        self.project_changelist_label.setToolTip(step)

        self.project_changelist_label.show()

    def update_sync_status(self, device, linecount):
        self.project_changelist_label.setText(f"Syncing...{linecount}")
        self.project_changelist_label.setToolTip('Syncing from Version Control')

        self.project_changelist_label.show()

    def update_engine_changelist(self, value):
        self.engine_changelist_label.setText(f"E: {value}")
        self.engine_changelist_label.show()

        self.sync_button.show()
        self.build_button.show()

    def project_changelist_display_warning(self, b):
        if b:
            self.project_changelist_label.setProperty("error", True)
        else:
            self.project_changelist_label.setProperty("error", False)
        self.project_changelist_label.setStyle(self.project_changelist_label.style())

    def engine_changelist_display_warning(self, b):
        if b:
            self.engine_changelist_label.setProperty("error", True)
        else:
            self.engine_changelist_label.setProperty("error", False)
        self.engine_changelist_label.setStyle(self.engine_changelist_label.style())

    def sync_button_clicked(self):
        self.signal_device_widget_sync.emit(self)

    def build_button_clicked(self):
        self.signal_device_widget_build.emit(self)

    def open_button_clicked(self):
        if self.open_button.isChecked():
            self._open()
        else:
            self._close()

    def connect_button_clicked(self):
        if self.connect_button.isChecked():
            self._connect()
        else:
            self._disconnect()

