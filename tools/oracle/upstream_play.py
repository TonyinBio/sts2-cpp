"""Load the pinned sts2-cli interface with an injectable transport.

The submodule remains clean. Only process ownership and recorder hooks change;
rendering, prompts, input validation, and the room-specific play loop stay upstream.
"""
import sys
import types

from reference_runtime import REPO, require


def load_interface():
    path = REPO / 'sts2-cli/python/play.py'
    source = path.read_text()

    def replace(old, new):
        nonlocal source
        require(source.count(old) == 1, f'Upstream interface changed: {old[:70]}')
        source = source.replace(old, new, 1)

    replace('DOTNET = _find_dotnet()', 'DOTNET = None  # The recorder owns the reference process.')
    replace('load_path=None, native_save_path=None):',
            'load_path=None, native_save_path=None, command_sender=None, checkpoint=None):')
    start = source.index('    proc = subprocess.Popen(', source.index('def play(character='))
    end = source.index('    # Wire send into get_input', start)
    source = source[:start] + '''    if command_sender is None:
        raise RuntimeError("An injected command sender is required")

    def read():
        return {"type": "ready"}

    def send(cmd, record=True):
        result = command_sender(cmd)
        if record and cmd.get("cmd") == "action":
            action_log.append(cmd)
        return result

''' + source[end:]
    replace('                valid["s"] = None  # skip', '                if state.get("can_skip", True):\n                    valid["s"] = None  # skip')
    replace('                if min_sel == 0:\n                    valid["s"] = None',
            '                if min_sel == 0 or state.get("cancelable", False):\n                    valid["s"] = None')
    replace('def _save_game(save_path, character, seed, action_log):',
            'def _save_game(save_path, character, seed, action_log, ascension=0):')
    replace('data = {"character": character, "seed": seed, "actions": action_log}',
            'data = {"character": character, "seed": seed, "actions": action_log, "ascension": ascension}')
    replace('_save_game(save_path, character, actual_seed, action_log)',
            '_save_game(save_path, character, actual_seed, action_log, ascension)')
    replace('if multi_min == 0 and raw == "s" and "s" in valid_options:',
            'if (multi_min == 0 or (state or {}).get("cancelable")) and raw == "s" and "s" in valid_options:')
    replace('    def do_save():\n', '    def do_save():\n        if checkpoint:\n            checkpoint()\n')
    replace('    except KeyboardInterrupt:\n',
            '    except KeyboardInterrupt as error:\n'
            '        if str(error) == "abandon":\n'
            '            send({"cmd": "action", "action": "abandon"})\n')
    cleanup = '''        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()'''
    replace(cleanup, '        # The recording session owns process cleanup.')
    # Import under a distinct name to avoid colliding with our play.py.
    module = types.ModuleType('sts2_recording_interface')
    module.__file__ = str(path)
    sys.path.insert(0, str(path.parent))
    try:
        exec(compile(source, str(path), 'exec'), module.__dict__)
    finally:
        sys.path.pop(0)
    return module


class RecordingTransport:
    """Translate upstream UI commands, retaining every implicit gameplay decision."""
    def __init__(self, session):
        self.session = session
        self.closed = False
        self.language = "en"

    def _action(self, kind, args=None):
        from play import resolve
        args = dict(args or {})
        state = self.session.state
        if kind == 'leave_room':
            kind = 'proceed'
        if kind == 'play_card':
            refs = state['combat']['players'][0]['piles']['Hand']
            args['card_instance'] = refs[args.pop('card_index')].rsplit('/', 1)[1]
        if 'target_index' in args:
            enemies = [ref for ref in state['combat']['enemies']
                       if resolve(state, ref)['monster']['current_hp'] > 0]
            args['target'] = enemies[args.pop('target_index')].rsplit('/', 1)[1]
        if kind == 'skip_select':
            choice = state.get('pending_choice') or (state.get('run_flow') or {}).get('pending_choice')
            if choice and not choice.get('cancelable', False) and choice.get('min_select') == 0:
                kind, args = 'select_cards', {'indices': []}
        if kind == 'select_cards':
            indices = args['indices']
            if isinstance(indices, str):
                indices = [int(token.strip()) for token in indices.split(',') if token.strip()]
            if state.get('pending_choice') is not None:
                refs = state['pending_choice']['candidates']
                args = {'card_instances': [refs[i].rsplit('/', 1)[1] for i in indices]}
            else:
                args['indices'] = indices
        self.session.step(dict(kind=kind, **args))

    def _settle_interface(self):
        # Upstream performs these automatically while detecting the next prompt.
        # Keep them in gameplay processing, and record each decision explicitly.
        for _ in range(100):
            state = self.session.state
            if self.session.finished() or state.get('pending_choice') is not None:
                return
            flow = state.get('run_flow')
            if not flow or flow['pending_choice'] is not None:
                return
            if flow['reward_sets']:
                rewards = flow['reward_sets'][-1]['rewards']
                remaining = [i for i, reward in enumerate(rewards) if not reward['taken']]
                if remaining:
                    self._action('take_reward', {'index': remaining[0]})
                else:
                    self._action('skip_rewards')
                continue
            room = flow['room']
            kind = room['kind']
            if kind == 'treasure' and not room['picked']:
                self._action('pick_treasure', {'index': 0})
            elif (kind in ('combat_end', 'victory') or
                  kind == 'event' and room['finished'] or
                  kind == 'rest_site' and not any(option['enabled'] for option in room['options']) or
                  kind == 'treasure' and room['picked']):
                self._action('proceed')
            else:
                return
        raise RuntimeError('The interface did not reach an input boundary')

    def __call__(self, command):
        name = command['cmd']
        if name == 'start_run':
            self.language = command.get('lang', 'en')
            self.session.start()
        elif name == 'action':
            self._action(command['action'], command.get('args'))
        elif name == 'get_map':
            return self.session.host.send(command)
        elif name == 'quit':
            if self.closed:
                return {'type': 'quit_result'}
            self.session.publish(final=True)
            result = self.session.host.send(command)
            self.closed = result.get('type') == 'quit_result'
            return result
        else:
            raise ValueError(f'Unsupported interface command: {name}')
        self._settle_interface()
        return self.session.host.send({'cmd': 'oracle_display', 'lang': self.language})
