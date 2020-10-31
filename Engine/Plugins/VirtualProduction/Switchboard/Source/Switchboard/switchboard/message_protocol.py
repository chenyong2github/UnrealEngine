# Copyright Epic Games, Inc. All Rights Reserved.

import base64
import json
import uuid

def create_start_process_message(prog_path, prog_args, prog_name, caller, update_clients_with_stdout):
    cmd_id = uuid.uuid4()
    start_cmd = {
        'command': 'start', 
        'id': str(cmd_id), 
        'exe': prog_path, 
        'args': prog_args, 
        'name':prog_name, 
        'caller':caller,
        'bUpdateClientsWithStdout' : update_clients_with_stdout,
    }
    message = json.dumps(start_cmd).encode() + b'\x00'
    return (cmd_id, message)

def create_kill_process_message(program_id):
    cmd_id = uuid.uuid4()
    kill_cmd = {'command': 'kill', 'id': str(cmd_id), 'uuid': str(program_id)}
    message = json.dumps(kill_cmd).encode() + b'\x00'
    return (cmd_id, message)

def create_vcs_init_message(provider, vcs_settings):
    cmd_id = uuid.uuid4()
    vcs_init_cmd = {'command': 'vcs init', 'id': str(cmd_id), 'provider': provider, 'vcs settings': vcs_settings}
    message = json.dumps(vcs_init_cmd).encode() + b'\x00'
    return (cmd_id, message)

def create_vcs_report_revision_message(path):
    cmd_id = uuid.uuid4()
    vcs_revision_cmd = {'command': 'vcs report revision', 'id': str(cmd_id), 'path': path}
    message = json.dumps(vcs_revision_cmd).encode() + b'\x00'
    return (cmd_id, message)

def create_vcs_sync_message(revision, path):
    cmd_id = uuid.uuid4()
    vcs_sync_cmd = {'command': 'vcs sync', 'id': str(cmd_id), 'revision': revision, 'path': path}
    message = json.dumps(vcs_sync_cmd).encode() + b'\x00'
    return (cmd_id, message)

def create_disconnect_message():
    cmd_id = uuid.uuid4()
    disconnect_cmd = {'command': 'disconnect', 'id': str(cmd_id)}
    message = json.dumps(disconnect_cmd).encode() + b'\x00'
    return (cmd_id, message)

def create_send_file_message(path_to_source_file, destination_path):
    with open(path_to_source_file, 'rb') as f:
        file_content = f.read()
    encoded_content = base64.b64encode(file_content)

    cmd_id = uuid.uuid4()
    transfer_file_cmd = {'command': 'send file', 'id': str(cmd_id), 'destination': destination_path, "content": encoded_content.decode()}
    message = json.dumps(transfer_file_cmd).encode() + b'\x00'
    return (cmd_id, message)

def create_copy_file_from_listener_message(path_on_listener_machine):
    cmd_id = uuid.uuid4()
    copy_file_cmd = {'command': 'receive file', 'id': str(cmd_id), 'source': path_on_listener_machine}
    message = json.dumps(copy_file_cmd).encode() + b'\x00'
    return (cmd_id, message)

def create_keep_alive_message():
    cmd_id = uuid.uuid4()
    keep_alive_cmd = {'command': 'keep alive', 'id': str(cmd_id)}
    message = json.dumps(keep_alive_cmd).encode() + b'\x00'
    return (cmd_id, message)

def create_get_sync_status_message(program_id):
    cmd_id = uuid.uuid4()
    cmd = {'command': 'get sync status', 'id': str(cmd_id), 'uuid': str(program_id), 'bEcho': False}
    message = json.dumps(cmd).encode() + b'\x00'
    return (cmd_id, message)

def decode_message(msg_in_bytes):
    msg_as_str = ''.join(msg_in_bytes)
    msg_json = json.loads(msg_as_str)
    return msg_json
