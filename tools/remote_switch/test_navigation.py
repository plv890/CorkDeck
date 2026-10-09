import unittest
from unittest.mock import patch
from navigation import DBI_TITLE, DBI_FTP_SD, dbi_ftp_sd, close_title


class FakeController:
    def __init__(self, title=DBI_TITLE, change_after=None):
        self.current = title
        self.change_after = change_after
        self.buttons = []
        self.detached = False
        self.transport = self

    def title(self):
        return self.current

    def send(self, command):
        assert command == 'click UNUSED'

    def click(self, button):
        self.buttons.append(button)
        if self.change_after == len(self.buttons):
            self.current = '0000000000000001'
        if button == 'A' and self.buttons == ['X', 'A']:
            self.current = None

    def detach(self):
        self.detached = True

    def home_primed(self):
        pass


@patch('navigation.time.sleep', return_value=None)
class NavigationTests(unittest.TestCase):
    def test_exact_sd_route(self, _sleep):
        c = FakeController()
        dbi_ftp_sd(c)
        self.assertEqual(tuple(c.buttons), DBI_FTP_SD)
        self.assertTrue(c.detached)

    def test_wrong_title_sends_no_input(self, _sleep):
        c = FakeController('0000000000000001')
        with self.assertRaises(ValueError):
            dbi_ftp_sd(c)
        self.assertEqual(c.buttons, [])

    def test_title_change_stops_and_detaches(self, _sleep):
        c = FakeController(change_after=3)
        with self.assertRaises(ValueError):
            dbi_ftp_sd(c)
        self.assertEqual(len(c.buttons), 3)
        self.assertTrue(c.detached)

    def test_close_requires_and_checks_active_title(self, _sleep):
        c = FakeController()
        close_title(c, DBI_TITLE)
        self.assertEqual(c.buttons, ['X', 'A'])
        self.assertIsNone(c.title())
        self.assertTrue(c.detached)
        with self.assertRaises(ValueError):
            close_title(c, None)


if __name__ == '__main__':
    unittest.main()
