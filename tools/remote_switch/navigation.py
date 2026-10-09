"""Verified, bounded UI routes. Caller confirms the documented starting screen."""
import time

DBI_TITLE = '03DB117EE17B6000'
DBI_FTP_SD = ('DDOWN',) * 12 + ('A', 'DDOWN', 'DDOWN', 'A')


def guard(controller, expected):
    if controller.title() != expected:
        raise ValueError('Active title changed; navigation stopped')


def sequence(controller, expected, buttons):
    """One attachment per sequence; release/detach even on timeout."""
    guard(controller, expected)
    try:
        controller.transport.send('click UNUSED')
        time.sleep(1)
        for button in buttons:
            guard(controller, expected)
            controller.click(button)
            # Autorun's modal transition can discard queued input. Navigation
            # within a list is fast; opening/closing a screen needs settling.
            time.sleep(0.75 if button in ('A', 'B', 'MINUS') else 0.25)
    finally:
        controller.detach()


def dbi_ftp_sd(controller):
    """DBI 905-ru main menu, first row selected -> FTP Browse SD."""
    sequence(controller, DBI_TITLE, DBI_FTP_SD)


def close_title(controller, expected):
    """Active title -> HOME (active title first), X, A -> no active title."""
    if expected is None:
        raise ValueError('An active title is required')
    guard(controller, expected)
    controller.home_primed()
    sequence(controller, expected, ('X', 'A'))
    # HOME's title process can be gone while its UI is still rebuilding.
    time.sleep(2)
    guard(controller, None)


