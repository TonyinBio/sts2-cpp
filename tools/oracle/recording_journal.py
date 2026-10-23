"""Append-only recorder storage and compatible complete JSON exports."""
import contextlib
import hashlib
import os
from pathlib import Path
import shutil
import tempfile
import uuid

from reference_runtime import atomic_write, require, strict_json, validate, wire_json


def sync_directory(directory):
    descriptor = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


class RecordingWriter:
    """Single-owner journal; commit.json is the authority after any interruption."""
    def __init__(self, directory, validator):
        self.directory = Path(directory)
        self.validator = validator
        self.marker = None
        self.failed = False
        self.last_checkpoint = None

    def start(self, actions, initial, reference):
        require(self.marker is None, 'Recording already started')
        require(not actions['actions'], 'New journals require an empty action prefix')
        # Initialization validates setup, provenance and the initial graph once.
        validate(self.validator, 'actions', actions)
        validate(self.validator, 'states', {
            'schema_version': 1, 'profile': 'states-v1',
            'recording_id': actions['recording_id'], 'actions_sha256': '0' * 64,
            'reference': reference, 'initial': initial, 'transitions': []}, actions)
        require(not (self.directory / 'commit.json').exists(), 'Recording already exists')
        for name in ('actions.jsonl', 'states.jsonl'):
            with (self.directory / name).open('xb') as stream:
                stream.flush()
                os.fsync(stream.fileno())
        sync_directory(self.directory.parent)
        self.marker = {'schema_version': 1, 'profile': 'journal-v1',
                       'recording_id': actions['recording_id'], 'actions_bytes': 0,
                       'states_bytes': 0, 'action_count': 0, 'checkpoint_count': 0}
        self._commit({'kind': 'setup', 'actions': actions},
                     {'kind': 'initial', 'reference': reference, 'state': initial})

    def _commit(self, action=None, state=None):
        require(self.marker is not None and not self.failed, 'Journal is not writable')
        candidate = dict(self.marker)
        try:
            for name, record in (('actions', action), ('states', state)):
                if record is None:
                    continue
                raw = (wire_json(record) + '\n').encode('utf-8')
                with (self.directory / (name + '.jsonl')).open('r+b') as stream:
                    stream.truncate(candidate[name + '_bytes'])
                    stream.seek(candidate[name + '_bytes'])
                    require(stream.write(raw) == len(raw), 'Incomplete journal write')
                    stream.flush()
                    os.fsync(stream.fileno())
                candidate[name + '_bytes'] += len(raw)
            if action is not None and 'id' in action:
                candidate['action_count'] += 1
            if state is not None and 'action_id' in state:
                candidate['checkpoint_count'] += 1
            atomic_write(self.directory / 'commit.json', candidate)
            sync_directory(self.directory)
            self.marker = candidate
        except BaseException:
            # A commit may already be visible. Never append using stale offsets.
            # Export/recovery reads the on-disk marker, not this object's cache.
            self.failed = True
            raise

    def append(self, entry, state=None):
        expected = f"action-{self.marker['action_count'] + 1:06d}"
        require(entry['id'] == expected, 'Action IDs must be sequential')
        checkpoint = {'action_id': entry['id'], 'state': state} if state is not None else None
        self._commit(entry, checkpoint)
        if checkpoint is not None:
            self.last_checkpoint = entry['id']

    def checkpoint(self, action_id, state):
        require(self.marker is not None and self.marker['action_count'] > 0, 'No action to checkpoint')
        require(action_id == f"action-{self.marker['action_count']:06d}",
                'Checkpoint must refer to the latest action')
        if action_id != self.last_checkpoint:
            self._commit(state={'action_id': action_id, 'state': state})
            self.last_checkpoint = action_id


def read_recording(directory):
    """Read committed prefixes only. Committed corruption is an error, not a tail."""
    directory = Path(directory)
    marker = strict_json((directory / 'commit.json').read_bytes())
    require(isinstance(marker, dict) and set(marker) == {'schema_version', 'profile', 'recording_id', 'actions_bytes',
                            'states_bytes', 'action_count', 'checkpoint_count'} and
            type(marker['schema_version']) is int and marker['schema_version'] == 1 and
            marker['profile'] == 'journal-v1' and isinstance(marker['recording_id'], str) and
            bool(marker['recording_id']),
            'Unsupported journal commit marker')
    records = {}
    for name, count in (('actions', 'action_count'), ('states', 'checkpoint_count')):
        length = marker[name + '_bytes']
        require(type(length) is int and length > 0 and
                type(marker[count]) is int and marker[count] >= 0, 'Invalid journal offsets/counts')
        path = directory / (name + '.jsonl')
        require(path.stat().st_size >= length, f'Truncated committed {name} history')
        with path.open('rb') as stream:
            raw = stream.read(length)
        require(raw.endswith(b'\n'), f'Incomplete committed {name} record')
        records[name] = [strict_json(line) for line in raw.splitlines()]
        require(len(records[name]) == marker[count] + 1, f'Invalid committed {name} count')
    setup, *entries = records['actions']
    initial, *transitions = records['states']
    require(isinstance(setup, dict) and set(setup) == {'kind', 'actions'} and
            setup['kind'] == 'setup' and isinstance(setup['actions'], dict) and
            setup['actions'].get('actions') == [], 'Invalid journal setup')
    require(isinstance(initial, dict) and set(initial) == {'kind', 'reference', 'state'} and initial['kind'] == 'initial',
            'Invalid journal initial state')
    actions = dict(setup['actions'], actions=entries)
    require(actions.get('recording_id') == marker['recording_id'], 'Journal identity mismatch')
    for index, entry in enumerate(entries, 1):
        require(isinstance(entry, dict) and entry.get('id') == f'action-{index:06d}',
                'Journal action IDs must be sequential')
    return actions, {'initial': initial['state'], 'transitions': transitions}, initial['reference'], marker


def export_recording(directory, validator, recover=False):
    """Materialize an independently validated legacy pair; never execute gameplay."""
    actions, sequence, reference, marker = read_recording(directory)
    validate(validator, 'journal_record', marker)
    writer = _PairWriter(directory, validator)
    result = writer.publish(actions, sequence, reference)
    if recover:
        # Validate the entire committed prefix before changing anything. This
        # command requires an offline journal with no active recorder.
        for name in ('actions', 'states'):
            with (Path(directory) / (name + '.jsonl')).open('r+b') as stream:
                stream.truncate(marker[name + '_bytes'])
                stream.flush()
                os.fsync(stream.fileno())
        sync_directory(Path(directory))
    return result


class _PairWriter:
    """Publish a matched action/state pair by atomically switching one directory link.

    Called only for explicit exports. A previous pair remains readable if writing
    or validation fails; ordinary action commits do not materialize JSON history.
    """
    def __init__(self, directory, validator):
        self.directory = Path(directory)
        self.validator = validator

    def publish(self, actions, sequence, reference):
        raw = (wire_json(actions) + '\n').encode('utf-8')
        states = {'schema_version': 1, 'profile': 'states-v1',
                  'recording_id': actions['recording_id'],
                  'actions_sha256': hashlib.sha256(raw).hexdigest(),
                  'reference': reference, **sequence}
        validate(self.validator, 'actions', actions)
        validate(self.validator, 'states', states, actions)
        exports = self.directory / '.exports'
        exports.mkdir(exist_ok=True)
        temporary = Path(tempfile.mkdtemp(prefix='pair-', dir=exports))
        link = self.directory / ('.current-' + uuid.uuid4().hex)
        current = self.directory / 'current'
        previous = current.resolve() if current.is_symlink() else None
        if previous is not None and (previous.parent != exports.resolve() or
                                     not previous.name.startswith('pair-')):
            previous = None
        durable = False
        try:
            with (temporary / 'actions.json').open('wb') as stream:
                stream.write(raw)
                stream.flush()
                os.fsync(stream.fileno())
            atomic_write(temporary / 'states.json', states)
            # Independently obtained backend identity is also available to the
            # simulator runner without reading it from expected checkpoints.
            atomic_write(temporary / 'reference.json', reference)
            sync_directory(temporary)
            sync_directory(exports)
            os.symlink(str(temporary.relative_to(self.directory)), link, target_is_directory=True)
            os.replace(link, self.directory / 'current')
            sync_directory(self.directory)
            durable = True
        finally:
            if link.is_symlink():
                link.unlink()
            # SIGINT can arrive immediately after os.replace. Detect publication
            # from the link itself so cleanup never removes its target.
            published = ((self.directory / 'current').is_symlink() and
                         (self.directory / 'current').resolve() == temporary.resolve())
            if published:
                # Only retire generations inside the private export directory.
                if durable and previous is not None:
                    with contextlib.suppress(OSError):
                        shutil.rmtree(previous)
            else:
                shutil.rmtree(temporary)
        return self.directory / 'current'

