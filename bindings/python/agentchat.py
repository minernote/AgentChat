"""
AgentChat Python SDK
Thin ctypes wrapper around the agentchat_c_api shared library.

Build the library first:
    cd /path/to/AgentChat
    mkdir -p build && cd build && cmake .. && make agentchat_c_api -j4
"""
from __future__ import annotations
import ctypes, os, sys, threading
from pathlib import Path
from typing import Callable, Optional

def _find_library() -> str:
    env = os.environ.get('AGENTCHAT_LIB')
    if env: return env
    here = Path(__file__).parent
    candidates = [
        here / 'libagentchat_c_api.dylib',
        here / 'libagentchat_c_api.so',
        here.parent.parent / 'build' / 'libagentchat_c_api.dylib',
        here.parent.parent / 'build' / 'libagentchat_c_api.so',
    ]
    for p in candidates:
        if p.exists(): return str(p)
    raise FileNotFoundError('libagentchat_c_api not found. Build it first or set AGENTCHAT_LIB.')

_lib = ctypes.CDLL(_find_library())

# C function signatures
_lib.agentchat_create.restype  = ctypes.c_void_p
_lib.agentchat_create.argtypes = [
    ctypes.c_char_p,   # host
    ctypes.c_uint16,   # port
    ctypes.c_uint64,   # agent_id
    ctypes.c_char_p,   # identity_priv_hex
    ctypes.c_char_p,   # exchange_priv_hex
]
_lib.agentchat_connect.restype    = ctypes.c_int
_lib.agentchat_connect.argtypes   = [ctypes.c_void_p]
_lib.agentchat_send_text.restype  = ctypes.c_int
_lib.agentchat_send_text.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_char_p, ctypes.c_size_t]
_lib.agentchat_disconnect.argtypes = [ctypes.c_void_p]
_lib.agentchat_destroy.argtypes    = [ctypes.c_void_p]
_lib.agentchat_free_string.argtypes = [ctypes.c_char_p]
_lib.agentchat_generate_keypair.argtypes = [
    ctypes.POINTER(ctypes.c_char_p),
    ctypes.POINTER(ctypes.c_char_p),
    ctypes.c_int,
]

_MSG_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.c_void_p)
_lib.agentchat_on_message.argtypes = [ctypes.c_void_p, _MSG_CB, ctypes.c_void_p]


def generate_keypair(ed25519: bool = True) -> tuple[str, str]:
    """Generate a key pair. Returns (public_hex, private_hex)."""
    pub  = ctypes.c_char_p()
    priv = ctypes.c_char_p()
    _lib.agentchat_generate_keypair(ctypes.byref(pub), ctypes.byref(priv), 1 if ed25519 else 0)
    result = pub.value.decode(), priv.value.decode()
    _lib.agentchat_free_string(pub)
    _lib.agentchat_free_string(priv)
    return result


class AgentChatClient:
    """
    AgentChat client for AI agents.

    Example::

        client = AgentChatClient('localhost', 8765, agent_id=42)
        client.on_message(lambda from_id, data: print(f'{from_id}: {data}'))
        client.connect()
        client.send_text(99, 'Hello from AI!')
        ...
        client.disconnect()
    """

    def __init__(self, host: str, port: int, agent_id: int,
                 identity_priv_hex: str = '', exchange_priv_hex: str = ''):
        self._handle = _lib.agentchat_create(
            host.encode(), port, agent_id,
            identity_priv_hex.encode(),
            exchange_priv_hex.encode(),
        )
        if not self._handle:
            raise RuntimeError('Failed to create AgentChatClient')
        self._cb_ref = None  # keep callback alive

    def connect(self) -> bool:
        return bool(_lib.agentchat_connect(self._handle))

    def send_text(self, to_agent_id: int, text: str) -> bool:
        enc = text.encode('utf-8')
        return bool(_lib.agentchat_send_text(self._handle, to_agent_id, enc, len(enc)))

    def on_message(self, callback: Callable[[int, bytes], None]) -> None:
        """Register callback(from_agent_id: int, payload: bytes)."""
        def _bridge(from_id, payload_ptr, length, userdata):
            data = bytes(payload_ptr[:length])
            callback(from_id, data)
        self._cb_ref = _MSG_CB(_bridge)
        _lib.agentchat_on_message(self._handle, self._cb_ref, None)

    def disconnect(self) -> None:
        _lib.agentchat_disconnect(self._handle)

    def __del__(self):
        if self._handle:
            _lib.agentchat_destroy(self._handle)
            self._handle = None
