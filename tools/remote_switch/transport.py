"""Bounded LAN transport. No DNS, public endpoints, retries or raw shell."""
import ipaddress
import socket
import time


def lan_ip(value):
    ip = ipaddress.IPv4Address(value)
    networks = ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16')
    if not any(ip in ipaddress.ip_network(n) for n in networks):
        raise ValueError('Use a literal trusted LAN IPv4 address')
    return str(ip)


class BotTransport:
    def __init__(self, host, port=6000, timeout=5):
        self.host = lan_ip(host)
        if not 1 <= port <= 65535 or not 0 < timeout <= 15:
            raise ValueError('Invalid port or timeout')
        self.port, self.timeout = port, timeout

    def send(self, command, reply=False, limit=4096):
        # Only Controller constructs commands; transport is not exposed by CLI.
        if '\n' in command or '\r' in command:
            raise ValueError('Invalid command framing')
        deadline = time.monotonic() + self.timeout
        with socket.create_connection((self.host, self.port), self.timeout) as conn:
            conn.sendall(command.encode('ascii') + b'\r\n')
            if not reply:
                return None  # TCP delivery is NOT proof of execution.
            result = bytearray()
            while True:
                left = deadline - time.monotonic()
                if left <= 0:
                    raise TimeoutError('Reply deadline exceeded')
                conn.settimeout(left)
                part = conn.recv(min(65536, limit + 1 - len(result)))
                if not part:
                    raise ConnectionError('Disconnected before reply terminator')
                result.extend(part)
                if len(result) > limit:
                    raise ValueError('Reply exceeds size limit')
                if b'\n' in result:
                    line, remainder = bytes(result).split(b'\n', 1)
                    if remainder:
                        raise ValueError('Unexpected trailing response')
                    return line.rstrip(b'\r')
