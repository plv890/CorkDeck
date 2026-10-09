"""Original sys-botbase ASCII protocol; memory commands deliberately absent."""
import time
BUTTONS = ('A', 'B', 'X', 'Y', 'L', 'R', 'ZL', 'ZR', 'PLUS', 'MINUS',
           'DUP', 'DDOWN', 'DLEFT', 'DRIGHT', 'HOME', 'LSTICK', 'RSTICK')
XINPUT_FACE = {'A': 'B', 'B': 'A', 'X': 'Y', 'Y': 'X'}


class Controller:
    def __init__(self, transport):
        self.transport = transport

    def version(self):
        value = self.transport.send('getVersion', reply=True, limit=128)
        return value.decode('ascii')

    def title(self):
        value = self.transport.send('getTitleID', reply=True, limit=128).decode('ascii')
        if not value:
            return None  # No application metadata on HOME; not a protocol failure.
        title = int(value, 16)
        if not 0 <= title < 2**64:
            raise ValueError('Invalid title ID')
        return f'{title:016X}'

    def screenshot(self):
        encoded = self.transport.send('pixelPeek', reply=True, limit=2*0x7D000+2)
        if not encoded or len(encoded) % 2:
            raise ValueError('No screenshot: capture may be unsupported on this firmware')
        data = bytes.fromhex(encoded.decode('ascii'))
        if not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
            raise ValueError('Invalid JPEG response')
        return data

    def click(self, button):
        if button not in BUTTONS:
            raise ValueError('Unsupported button')
        self.transport.send('click ' + button)

    def detach(self):
        self.transport.send('detachController')

    def click_ready(self, button):
        # Allow applications to discover the virtual device before the press.
        if button not in BUTTONS:
            raise ValueError('Unsupported button')
        try:
            self.transport.send('click UNUSED')
            time.sleep(1.0)
            self.transport.send('press ' + button)
            time.sleep(0.2)
        finally:
            try:
                self.transport.send('release ' + button)
            finally:
                self.detach()

    def album_homebrew(self):
        # One fixed launch chord; no arbitrary hold exposed to the caller.
        try:
            self.transport.send('press R')
            time.sleep(0.15)
            self.click('A')
            time.sleep(1.2)
        finally:
            try:
                self.transport.send('release R')
            finally:
                self.detach()

    def home_primed(self):
        # Upstream UNUSED workaround; keep the same device through HOME.
        try:
            self.transport.send('click UNUSED')
            time.sleep(0.3)
            self.click('HOME')
            time.sleep(0.7)
        finally:
            self.detach()
